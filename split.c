#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE    1
#define _XOPEN_SOURCE 500

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <strings.h>
#include <string.h>
#include <ctype.h>

#include "split.h"
#define E() do { printf("E: %s +%d\n", __FUNCTION__, __LINE__) ; } while(0)

/* Maximal buffer size */
#define MAX_BUF_SIZE (1024 * 1024)

/* Good looking way to calculate remain size */
#define REMAIN(size, written) (size - written)
#define MIN(a,b) ( (a<b) ? a : b )

/* Count offset where to read from: number od part (seq), size of every part (size) and already read size (ofset)*/
#define READ_OFFSET(seq, size, offset) ((seq * size) + offset)


static int unmap_file(char * pc_map, size_t i_size)
{
    E();
    return( munmap(pc_map, i_size) );
}


static char * map_file(int i_fd, size_t i_size)
{
    E();
    return mmap(NULL, i_size, PROT_READ, MAP_SHARED, i_fd, 0);
}


int open_and_map(core_t * ps_core)
{
    int i_rv;

    E();
    ps_core->i_origin_fd = open(ps_core->pc_origin_name, O_RDONLY | O_LARGEFILE);

    if ( ps_core->i_origin_fd < 0 )
    {
        perror("Can't open origin file: "); 
        return(errno);
    }

    if ( fstat(ps_core->i_origin_fd, &ps_core->s_origin_stat) || ps_core->s_origin_stat.st_size <= 0)
    {
        i_rv = errno;
        perror("Can't origin stat file: "); 
        close(ps_core->i_origin_fd);
        ps_core->i_origin_fd = -1;
        return(i_rv);
    }

    ps_core->pc_origin_map = (char *) map_file(ps_core->i_origin_fd, (int) ps_core->s_origin_stat.st_size);

    if ( !ps_core->pc_origin_map )
    {
        i_rv = errno;
        perror("Can't mmap file: ");    
        close(ps_core->i_origin_fd);
        return(i_rv);
    }

    return(0);
}

static int construct_out_name(char pc_result_name[], char * pc_name, int i_seq)
{
    E();
    return(sprintf(pc_result_name, "%s.%.3d", pc_name, i_seq));
}


static int reversing_unlink(char * pc_name, int i_seq)
{
    char * pc_filename;
    E();
    pc_filename = malloc(FILENAME_MAX * sizeof(char));
    if (!pc_filename)
        return(errno);

    while (i_seq >=0)
    {
        construct_out_name(pc_filename, pc_name, i_seq--);
        printf("Reverse: removing file %s\n", pc_filename);
        unlink(pc_filename);
    }
    return(0);
}


static int reversing_unlink3(core_t * ps_core, int i_seq)
{
    char * pc_filename;
    E();
    pc_filename = malloc(FILENAME_MAX * sizeof(char));
    if (!pc_filename)
        return(errno);

    while (i_seq >=0)
    {
        construct_out_name(pc_filename, ps_core->pc_origin_name, i_seq--);
        printf("Reverse: removing file %s\n", pc_filename);
        unlink(pc_filename);
    }
    return(0);
}


char * allocate_buf(off64_t i_buf_size, off64_t * pi_allocated)
{
    char * pc_buf = NULL;
    E();
    do
    {
        pc_buf = malloc(i_buf_size);
        if (!pc_buf)
            i_buf_size /= 2;
    } while (!pc_buf && i_buf_size > 1);

    * pi_allocated = i_buf_size;
    return(pc_buf);
}



/* i_fd: file descriptor, i_seq: sequential number of output file, i_size: size of output file, i_buf: maximal allowed size of buffer */
static off64_t write_chunk(char * pc_in_name, int i_fd_in, int i_seq, off64_t i_size, off64_t i_buf_size)
{
    char ac_out_name[FILENAME_MAX];
    char * pc_buf;
    off64_t i_fd_out;
    off64_t i_rv_read;
    off64_t i_rv_write;
    off64_t i_written = 0;
    E();
    pc_buf = allocate_buf(i_buf_size, &i_buf_size);
    if (!pc_buf)
        return(-1);

    construct_out_name(ac_out_name, pc_in_name, i_seq);
    i_fd_out = open(ac_out_name, O_CREAT | O_WRONLY | O_EXCL | O_LARGEFILE, 0666);
    if (i_fd_out < 0)
    {
        perror("Can't open file: ");
        free(pc_buf);
        return(-1);
    }

    printf("Writing: %s\n", ac_out_name);
    do
    {
        i_rv_read = pread(i_fd_in, pc_buf,  MIN(i_buf_size, REMAIN(i_size, i_written) ), READ_OFFSET(i_seq, i_size, i_written));
        if (i_rv_read < 0)
        {
            perror("Can't read file: ");
            goto write_chunk_end;
        }

        if (i_rv_read == 0) goto write_chunk_end;

        i_rv_write = write(i_fd_out, pc_buf, i_rv_read);

        if (i_rv_read != i_rv_write)
        {
            perror("Can't write file: ");
            goto write_chunk_end;
        }
        i_written += i_rv_write;

    }while (i_rv_read > 0);

    write_chunk_end:
    close(i_fd_out);
    free(pc_buf);
    return(i_written);
}


int split_file(char * pc_name, off64_t i_size)
{
    char        ac_outname[FILENAME_MAX];
    int         i_seq = 0;
    int         i_rv;
    off64_t     i_offset = 0;
    off64_t     i_written;
    off64_t     i_remain;
    off64_t     i_write_now;
    int         i_fd;
    int         i_fd_out;
    char *      pc_map;
    struct      stat s_st;
    E();
    i_fd = open(pc_name, O_RDONLY | O_LARGEFILE);

    if ( i_fd < 0 )
    {
        perror("Can't open file: ");    
        return(errno);
    }

    if ( fstat(i_fd, &s_st) )
    {
        i_rv = errno;
        perror("Can't stat file: ");    
        close(i_fd);
        return(i_rv);
    }

    i_remain = s_st.st_size;

    pc_map = (char *) map_file(i_fd, (int) s_st.st_size);

    if ( !pc_map )
    {
        i_rv = errno;
        perror("Can't mmap file: ");    
        close(i_fd);
        return(i_rv);
    }

    do
    {
        bzero(ac_outname, FILENAME_MAX);
        construct_out_name(ac_outname, pc_name, i_seq++);


        i_fd_out = open(ac_outname, O_CREAT | O_WRONLY | O_EXCL | O_LARGEFILE);
        if ( i_fd_out < 0 )
        {
            i_rv = errno;
            /* TODO: On error delete all created files */
            perror("Can't open output file: "); 
            goto an_error;
        }

        printf("Writing into file %s\n", ac_outname);

        i_write_now = (i_remain < i_size) ? i_remain : i_size;

        i_written = write(i_fd_out, pc_map + i_offset, i_write_now);

        if ( i_written < 0 )
        {
            i_rv = errno;
            perror("Can't write output file: ");    
            goto an_error;
        }

        i_offset += i_written;
        i_remain -= i_written;

        close(i_fd_out);

    } while (i_remain > 0 );

    if (i_offset < s_st.st_size)
    {
        printf("Error: common output error less then source file: %lld < %lld \n",i_offset, s_st.st_size );
        reversing_unlink(pc_name,i_seq);
        return(-1);
    }

    /* Construct file name */


    return(0);

    an_error:
    close(i_fd_out);
    unmap_file(pc_map, s_st.st_size);
    close(i_fd);
    reversing_unlink(pc_name, i_seq);
    return(i_rv);
}


int split_file2(char * pc_name, off64_t i_size)
{
    int     i_seq = 0;
    int     i_rv;

    int     i_fd;
    char *      pc_map;
    struct      stat s_st;

    off64_t  i_offset = 0;
    off64_t  i_file_size = 0;
    off64_t i_written;
    E();

    i_fd = open(pc_name, O_RDONLY | O_LARGEFILE);

    if ( i_fd < 0 )
    {
        perror("Can't open file: ");    
        return(errno);
    }

    if ( fstat(i_fd, &s_st) || s_st.st_size <= 0)
    {
        i_rv = errno;
        perror("Can't stat file: ");    
        close(i_fd);
        return(i_rv);
    }

    i_file_size = s_st.st_size;
    printf("i_file_size: %lld \n", i_file_size);

    pc_map = (char *) map_file(i_fd, (int) s_st.st_size);

    if ( !pc_map )
    {
        i_rv = errno;
        perror("Can't mmap file: ");    
        close(i_fd);
        return(i_rv);
    }

    do
    {
        i_written = write_chunk(pc_name, i_fd, i_seq++, i_size, (1025 * 512));
        if (i_written < 0)
        {
            reversing_unlink(pc_name,i_seq);
        }

        i_offset += i_written;

    } while ( i_written > 0 );

    if (i_offset < i_file_size)
    {
        printf("Error: common output less then source file : %lld < %lld \n", i_offset, i_file_size);
        reversing_unlink(pc_name,i_seq);
        return(-1);
    }

    /* Construct file name */

    return(0);
}


/* Count offset of size i_seq in src file */
static off64_t count_begin_and_size(core_t * ps_core, int i_seq, off64_t * in_ll_offset, off64_t * in_ll_size)
{
    int i;
    int i_index = 0; 
    off64_t ll_offset = 0;
    off64_t ll_current_size = 0;

    E();
    /* There is a nusty trick. 	When sizes[N] == 0  it means that every next size should be sizes[N-1] */
    /* 							When sizes[N] == -1 it means that sizes are looped and index should jump to sizes[0] */

    printf("Counting chunk: %d\n", i_seq); 
    for (i = 0 ; i < i_seq ; i++ )
    {
        /* If it 0 - jump to begin of the array */
        if (0 == ps_core->sizes[i_index])
        {
            i_index = 0;
            ll_current_size = ps_core->sizes[i_index];
        }
        /* If it -1 stay where you are and always get sizes[i_index - 1] */
        else if (-1 == ps_core->sizes[i_index])
            ll_current_size = ps_core->sizes[i_index - 1];
        else
            /* Else just get next one  */
            ll_current_size = ps_core->sizes[i_index];

        ll_offset += ll_current_size;
        i_index++;
    }

    /* That's why I need i_current_size */
    // *in_ll_size = ll_current_size;
    printf("size: %d\n", ll_current_size);
    *in_ll_size = (ll_current_size) ? ll_current_size : ps_core->sizes[0];
    *in_ll_offset = ll_offset;

    /* Mission complete. Go home. */
    return(0);
}


off64_t write_chunk3(core_t * ps_core)
{
    int i;

    char *  pc_buf;
    char    ac_out_name[FILENAME_MAX];

    off64_t ll_offset_begin;
    off64_t ll_chunk_size;

    int     i_fd_out;

    off64_t ll_rv_read;
    off64_t ll_rv_write;
    off64_t ll_written = 0;
    E();
    off64_t ll_buf_size = MAX_BUF_SIZE;

    i = count_begin_and_size(ps_core, ps_core->i_seq, &ll_offset_begin ,&ll_chunk_size);
    if (i) return(-1);

    pc_buf = allocate_buf(MAX_BUF_SIZE, &ll_buf_size);
    if (!pc_buf) return(-1);

    construct_out_name(ac_out_name, ps_core->pc_origin_name, ps_core->i_seq);

    i_fd_out = open(ac_out_name, O_CREAT | O_WRONLY | O_EXCL | O_LARGEFILE, 0666);

    if (i_fd_out < 0)
    {
        perror("Can't open file: ");
        free(pc_buf);
        return(-1);
    }

    printf("Writing: %s\n", ac_out_name);
    do
    {
        ll_rv_read = pread(ps_core->i_origin_fd, pc_buf,  MIN(ll_buf_size, REMAIN(ll_chunk_size, ll_written) ), (ll_offset_begin + ll_rv_read));

        if (ll_rv_read < 0)
        {
            perror("Can't read file: ");
            goto write_chunk_end3;
        }

        if (ll_rv_read == 0) goto write_chunk_end3;

        ll_rv_write = write(i_fd_out, pc_buf, ll_rv_read);

        if (ll_rv_read != ll_rv_write)
        {
            perror("Can't write file: ");
            goto write_chunk_end3;
        }

        ll_written += ll_rv_write;

    }while (ll_rv_read > 0);

    write_chunk_end3:
    close(i_fd_out);
    free(pc_buf);
    return(ll_written);

    return(0);
}


int split_file3(core_t * ps_core)
{
    int         i_rv;
    off64_t     ll_offset = 0;
    off64_t     ll_file_size = 0;
    off64_t     ll_written;
    E();

    i_rv = open_and_map(ps_core);
    if (i_rv) return(i_rv);

    ll_file_size = ps_core->s_origin_stat.st_size;

    do
    {
        ll_written = write_chunk3(ps_core);
        ps_core->i_seq++;
        if (ll_written < 0) reversing_unlink3(ps_core, ps_core->i_seq);

        ll_offset += ll_written;

    } while ( ll_written > 0 );

    if (ll_offset < ll_file_size)
    {
        printf("Error: common output less then source file : %lld < %lld \n", ll_offset, ll_file_size);
        reversing_unlink(ps_core->pc_origin_name, ps_core->i_seq - 1 );
        return(-1);
    }

    /* Construct file name */

    return(0);
}



int sprintf_original(char * pc_first, char * pc_original)
{
    char * pc_point;
    E();    
    strcpy(pc_original, pc_first);
    pc_point = rindex(pc_original, '.');

    if (!pc_point)
    {
        printf("Error: can't find point in first part name\n");
        return(-1);
    }

    *pc_point  = '\0';
    return(0);
}




int increase_name_num(char * pc_current)
{
    int i_seq;
    int i_suffix_len = 0;
    char * pc_rindex;
    E();    
    pc_rindex = rindex(pc_current, '.') + 1;
    if (!pc_rindex)  return(-1);
    i_suffix_len = strlen(pc_rindex);
    i_seq = atoi(pc_rindex + 1);

    switch (i_suffix_len)
    {
    case 1:
        sprintf( pc_rindex, "%.1d", i_seq + 1 );
        break;
    case 2:
        sprintf( pc_rindex, "%.2d", i_seq + 1 );
        break;
    case 3:
        sprintf( pc_rindex, "%.3d", i_seq + 1 );
        break;
    case 4:
        sprintf( pc_rindex, "%.4d", i_seq + 1 );
        break;
    case 5:
        sprintf( pc_rindex, "%.5d", i_seq + 1 );
        break;
    default:
        printf("Error: sufix too long\n");
        return(-1);
    }
    return(0);
}


int sprintf_next(char * pc_current, char * pc_next)
{
    int i_seq;
    int i_num_len;
    char * pc_rindex;
    char ac_num[6];
    E();    
    pc_rindex = rindex(pc_current, '.') + 1;

    i_num_len = strlen(pc_rindex);

    strncpy(pc_next, pc_current, (pc_rindex - pc_current) );

    i_seq = atoi(pc_rindex + 1);


    if (2 == i_num_len)
    {
        sprintf(ac_num, "%.2d", i_seq+1 );
    }
    else if (3 == i_num_len)
    {
        sprintf(ac_num, "%.3d", i_seq+1 );
    }
    else if (4 == i_num_len)
    {
        sprintf(ac_num, "%.4d", i_seq+1 );
    }
    else if (5 == i_num_len)
    {
        sprintf(ac_num, "%.5d", i_seq+1 );
    }
    else
    {
        printf("Error: sufix too long\n");
        bzero(pc_next, strlen(pc_current));
        return(-1);
    }

    memcpy(pc_next + (pc_rindex - pc_current), ac_num, i_num_len);
    return(0);
}


int join_files(char * pc_first, off64_t i_buf_size)
{
    off64_t i_fd_in         = -1;
    off64_t i_fd_out        = -1;
    char * pc_buf           = NULL;
    char * pc_name_dst      = NULL;
    char * pc_name_src      = NULL;
    int i_rv                = -1;
    off64_t i_written       = 0;
    off64_t i_read          = 0;
    E();
    pc_name_dst  = calloc(1, FILENAME_MAX);
    pc_name_src  = calloc(1, FILENAME_MAX);

    if ( NULL == pc_name_dst || NULL == pc_name_src) goto join_end;

    strcpy(pc_name_src, pc_first);
    sprintf_original(pc_first, pc_name_dst);

    i_fd_out = open(pc_name_dst, O_LARGEFILE | O_WRONLY | O_CREAT, 0666);

    if (i_fd_out < 0)
    {
        printf("Can't open output file\n");
        goto join_end;
    }

    pc_buf = allocate_buf(i_buf_size, &i_buf_size);

    if (!pc_buf)
    {
        unlink(pc_name_dst);
        goto join_end;
    }

    /* Open a part file, write it into the deastination file */

    do
    {
        i_fd_in = open(pc_name_src, O_LARGEFILE | O_RDONLY, 0666);
        if (i_fd_in < 0)
            goto join_end;

        printf("Processing: %s -> %s\n", pc_name_src, pc_name_dst);

        do
        {

            i_written = 0;
            i_read = read(i_fd_in, pc_buf, i_buf_size);

            if (i_read > 0) i_written = write(i_fd_out, pc_buf, i_read);

            if (i_read != i_written)
            {
                printf("Writing error to file: %s read %lld, write %lld \n", pc_name_src, i_read, i_written);
                perror("Error:");
                goto join_end;
            }

        }while (i_read > 0);

        close(i_fd_in);

    } while ( 0 == increase_name_num(pc_name_src) );

    /* All right, set return value to 0 */
    i_rv = 0;

    join_end:
    if (pc_name_src) free(pc_name_src);
    if (pc_name_dst) free(pc_name_dst);
    close(i_fd_out);
    if (pc_buf) free(pc_buf);
    return(i_rv);
}


off64_t size_to_digit(char * pc_size)
{
    int i_strlen;
    off64_t i_size_multi = 1;
    off64_t i_size;
    char ac_str[256];
    E();
    bzero(ac_str, 256);

    i_strlen = strlen(pc_size);
    if (isalpha(pc_size[i_strlen - 1]))
    {
        if (pc_size[i_strlen - 1] == 'B' || pc_size[i_strlen - 1] == 'b'  )
            i_size_multi = (1);
        else if (pc_size[i_strlen - 1] == 'K' || pc_size[i_strlen - 1] == 'k'  )
            i_size_multi = (1<<10);
        else if (pc_size[i_strlen - 1] == 'M' || pc_size[i_strlen - 1] == 'm'  )
            i_size_multi = (1<<20);
        else if (pc_size[i_strlen - 1] == 'G' || pc_size[i_strlen - 1] == 'g'  )
            i_size_multi = (1<<30);
        else
        {
            printf("Unknown size: %c\n", pc_size[i_strlen - 1]);
            return(0);

        }
    }

    memcpy(ac_str, pc_size, i_strlen -1 );
    ac_str[i_strlen - 1] = '\0';
    i_size = atoll(ac_str) * i_size_multi;
    return(i_size);
}

core_t * parse_args(int i_arg, char ** ppc_arg)
{
    int i;
    core_t * ps_score = calloc(1, sizeof(core_t));
    E();
    if (! ps_score)
    {
        printf("Can't allocate score struct\n");
        return(NULL);
    }

    for (i = 1; i < i_arg ; )
    {
        if (!strcmp(ppc_arg[i], "-s"))
        {
            ps_score->c_what = DO_SPLIT;
            ps_score->pc_origin_name = strdup(ppc_arg[i+1]);
            if (!ps_score->pc_origin_name)
            {
                printf("Can't duplicate origin name: split\n");
                free(ps_score);
                return(NULL);
            }
            i+=2;
            continue;
        }

        if (!strcmp(ppc_arg[i], "-j"))
        {
            ps_score->c_what = DO_JOIN;
            ps_score->pc_origin_name = strdup(ppc_arg[i+1]);
            if (!ps_score->pc_origin_name)
            {
                printf("Can't duplicate origin name: join\n");
                free(ps_score);
                return(NULL);
            }
            i+=2;
            continue;
        }

        /* -b block size. There may be given chain of values. */
        /* There i2 2 forms of sizes are allowed: */
        /* -b 1024M 512M 200M 2G : 		in such form rest of file will be cut to last given size, 2G chunks. */
        /* -b 1024M 512M 1G -r 			-r means "repeat" : it will be chunked to these sizes again and again: 1024M 512M 1G 1024M 512M 1G ...  */
        if (! strcmp(ppc_arg[i], "-b"))
        {
            printf("Going to parse sizes\n");
            i++;
            /* Begin parsing until next -something :) */
            while (ppc_arg[i] && ppc_arg[i][0] != '-')
            {
                ps_score->sizes[ps_score->i_sizes_index] = size_to_digit(ppc_arg[i++]);
                if (ps_score->sizes[ps_score->i_sizes_index] > 0)
                    ps_score->i_sizes_index++;
            }

            if (ppc_arg[i] && ! strcmp(ppc_arg[i++], "-r"))
                ps_score->sizes[ps_score->i_sizes_index] = -1;

            continue;
        }
    }
    return(ps_score);
}


int main(int i_arg, char ** ppc_arg)
{

    char * pc_name;
    char ac_next[1024];
    off64_t i_size;

    core_t * ps_score;
    E();
    ps_score = parse_args(i_arg, ppc_arg);

    if (!ps_score)
    {
        printf("NULL\n");
        return(0);
    }

    i_size = 0;
    while(ps_score->sizes[i_size] >0)
    {
        printf("size: %d\n", ps_score->sizes[i_size++]);
    }
    if ( DO_SPLIT == ps_score->c_what )
        split_file3(ps_score);
    if ( DO_JOIN == ps_score->c_what )
        join_files(ps_score->pc_origin_name, MAX_BUF_SIZE);

    return(0);

    bzero(ac_next, 1024);

    /* Arg 1 = -s / -j (split / join)
     * Arg 2 = filename
     * Arg 3 = size of chunk */

    i_size = atoll(ppc_arg[3]);
    pc_name = ppc_arg[2];

    if ( ! strcmp(ppc_arg[1], "-s")  )
        return split_file2(pc_name, i_size);

    if ( ! strcmp(ppc_arg[1], "-j") )
        return join_files(pc_name, i_size);

    return(-1);

}


