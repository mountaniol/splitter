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

// #define E() do { printf("E: %s +%d\n", __FUNCTION__, __LINE__) ; } while(0)

#define DD(x...) do {} while(0)
#define E(x...) do {} while(0)

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


static int open_and_map(core_t * ps_core)
{
	int i_rv;

	E();
	ps_core->i_origin_fd = open(ps_core->pc_origin_name, O_RDONLY | O_LARGEFILE);

	if ( ps_core->i_origin_fd < 0 )
	{
		perror("Can't open origin file: "); 
		return(errno);
	}

	if ( fstat(ps_core->i_origin_fd, &ps_core->s_origin_stat) || ps_core->s_origin_stat.st_size <= 0 )
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


static int unmap_and_close(core_t * ps_core)
{
	int i_rv = 0;
	if ( ps_core->pc_origin_map ) i_rv = unmap_file(ps_core->pc_origin_map, ps_core->s_origin_stat.st_size);
	if ( ps_core->i_origin_fd >= 0 ) i_rv |= close(ps_core->i_origin_fd);
	return(i_rv);
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
	if ( !pc_filename )
		return(errno);

	while ( i_seq >=0 )
	{
		construct_out_name(pc_filename, pc_name, i_seq--);
		DD("Reverse: removing file %s\n", pc_filename);
		unlink(pc_filename);
	}
	return(0);
}


static int reversing_unlink3(core_t * ps_core, int i_seq)
{
	char * pc_filename;
	E();
	pc_filename = malloc(FILENAME_MAX * sizeof(char));
	if ( !pc_filename )
		return(errno);

	while ( i_seq >=0 )
	{
		construct_out_name(pc_filename, ps_core->pc_origin_name, i_seq--);
		DD("Reverse: removing file %s\n", pc_filename);
		unlink(pc_filename);
	}
	return(0);
}

static char * allocate_buf(off64_t i_buf_size, off64_t * pi_allocated)
{
	char * pc_buf = NULL;
	E();
	do
	{
		pc_buf = malloc(i_buf_size);
		if ( !pc_buf )
			i_buf_size /= 2;
	} while ( !pc_buf && i_buf_size > 1 );

	* pi_allocated = i_buf_size;
	return(pc_buf);
}


/* i_seq: asked part to count offset + size of this segment. */
/* in_ll_offset - variable to keep offset of beginnig
   in_ll_size - variable to keep size of the segment  */

static off64_t count_begin_and_size2(core_t * ps_core, int i_seq, off64_t * in_ll_offset, off64_t * in_ll_size)
{
	int i;
	* in_ll_offset = *in_ll_size = 0;
	DD("count_begin_and_size2: i_seq : %i,ps_core->i_sizes_size: %i\n", i_seq, ps_core->i_sizes_size);

	if ( SIZES_RR == ps_core->c_sizes_repeat )
	{
		for ( i = 0; i <= i_seq ; i++ )
		{
			if (i > 0 ) *in_ll_offset += *in_ll_size;
			*in_ll_size = ps_core->sizes[ (i % ps_core->i_sizes_size) ];
		}
	}
	else
	{
		for ( i = 0; i < ps_core->i_sizes_size && i <= i_seq ; i++ )
		{
			if (i > 0 ) *in_ll_offset += *in_ll_size;
			*in_ll_size = ps_core->sizes[i];
		}

		/* Use last size for the rest of chunks. i_sizes_size begins from 1 */
		if (i_seq >= (ps_core->i_sizes_size - 1))
			* in_ll_offset += ( i_seq - i + 1) * (*in_ll_size);
	}

	DD("count_begin_and_size2: in_ll_size: %lld, in_ll_offset: %lld \n", *in_ll_size, *in_ll_offset);
	return(0);
}

static off64_t write_chunk3(core_t *ps_core)
{
	int i;

	char	*pc_buf;
	char	ac_out_name[FILENAME_MAX];

	off64_t ll_offset_begin = 0;
	off64_t ll_chunk_size = 0;

	int     i_fd_out;

	off64_t ll_rv_read = 0;
	off64_t ll_rv_write = 0;
	off64_t ll_written = 0;

	E();
	off64_t ll_buf_size = MAX_BUF_SIZE;

	i = count_begin_and_size2(ps_core,
					ps_core->i_seq,
					&ll_offset_begin,
					&ll_chunk_size);
	if (i)
		return -EINVAL;

	if (ps_core->s_origin_stat.st_size <= ll_offset_begin) {
		DD("Remain: %lld\n",
			(ps_core->s_origin_stat.st_size - ll_offset_begin));
		return 0;
	}

	DD("Counted: part %d, size %lld, offset %lld\n",
		ps_core->i_seq,
		ll_chunk_size,
		ll_offset_begin);

	pc_buf = allocate_buf(MIN(MAX_BUF_SIZE, ll_chunk_size), &ll_buf_size);
	if (!pc_buf)
		return -EINVAL;

	construct_out_name(ac_out_name,
				ps_core->pc_origin_name,
				ps_core->i_seq);

	i_fd_out = open(ac_out_name, O_CREAT
					| O_WRONLY
					| O_EXCL
					| O_LARGEFILE, 0666);

	if (i_fd_out < 0) {
		perror("Can't open file: ");
		free(pc_buf);
		return -EINVAL;
	}

	printf("Writing: %s\n", ac_out_name);

	do {
		DD("Going to read: %lld\n",
			MIN(ll_buf_size,
			(ps_core->s_origin_stat.st_size - ll_offset_begin)));

		ll_rv_read = pread(ps_core->i_origin_fd, pc_buf,
				MIN(ll_buf_size,
				REMAIN(ll_buf_size, ll_rv_read)),
				ll_offset_begin + ll_rv_read);

		if (ll_rv_read < 1)
			goto write_chunk_end3;

		ll_rv_write = write(i_fd_out, pc_buf, ll_rv_read);

		if (ll_rv_read != ll_rv_write) {
			perror("Can't write file: ");
			goto write_chunk_end3;
		}

		ll_written += ll_rv_write;

	} while (ll_rv_read > 0);

write_chunk_end3:
	close(i_fd_out);
	free(pc_buf);
	return ll_written;
}


static int split_file3(core_t *ps_core)
{
	int         i_rv;
	off64_t     ll_offset = 0;
	off64_t     ll_file_size = 0;
	off64_t     ll_written;

	E();

	i_rv = open_and_map(ps_core);
	if (i_rv)
		return i_rv;

	ll_file_size = ps_core->s_origin_stat.st_size;

	do {
		ll_written = write_chunk3(ps_core);
		ps_core->i_seq++;
		if (ll_written < 0)
			reversing_unlink3(ps_core, ps_core->i_seq);
		ll_offset += ll_written;

	} while (ll_written > 0);

	if (ll_offset < ll_file_size) {
		printf("Error: common output less then source file : %lld < %lld\n",
			ll_offset, ll_file_size);
		reversing_unlink(ps_core->pc_origin_name, ps_core->i_seq - 1);
		unmap_and_close(ps_core);
		return -EINVAL;
	}

	/* Construct file name */
	unmap_and_close(ps_core);
	return 0;
}


static int sprintf_original(char *pc_first, char *pc_original)
{
	char *pc_point;
	E();
	strcpy(pc_original, pc_first);
	pc_point = rindex(pc_original, '.');

	if (!pc_point) {
		printf("Error: can't find point in first part name\n");
		return -EINVAL;
	}

	*pc_point  = '\0';
	return 0;
}

static off_t increase_name_num(char *pc_current, off_t len)
{
	int i_seq;
	int i_rest;
	int i_suffix_len = 0;
	char *pc_rindex;
	E();
	pc_rindex = rindex(pc_current, '.') + 1;
	if (!pc_rindex)
		return -EINVAL;

	i_suffix_len = strlen(pc_rindex);
	i_seq = atoi(pc_rindex + 1);
	i_rest = len - (len - (pc_rindex - pc_current));

	if (i_rest < 1)
		return -EINVAL;

	switch (i_suffix_len) {
	case 1:
		return snprintf(pc_rindex, "%.1d", i_seq + 1, i_rest);
	case 2:
		return snprintf(pc_rindex, "%.2d", i_seq + 1, i_rest);
	case 3:
		return snprintf(pc_rindex, "%.3d", i_seq + 1, i_rest);
	case 4:
		return snprintf(pc_rindex, "%.4d", i_seq + 1, i_rest);
	case 5:
		return snprintf(pc_rindex, "%.5d", i_seq + 1, i_rest);
	default:
		printf("Error: sufix is too long\n");
		return -EINVAL;
	}
	return -EINVAL;
}

/* This function joins previously splitted files */
static int join_files(char *pc_first, off64_t i_buf_size)
{
	off64_t i_fd_in         = -1;
	off64_t i_fd_out        = -1;
	char *pc_buf           = NULL;
	char *pc_name_dst      = NULL;
	char *pc_name_src      = NULL;
	int i_rv                = -1;
	off64_t i_written       = 0;
	off64_t i_read          = 0;
	E();

	pc_name_dst  = calloc(1, FILENAME_MAX);
	pc_name_src  = calloc(1, FILENAME_MAX);

	if (NULL == pc_name_dst || NULL == pc_name_src)
		goto join_end;

	strncpy(pc_name_src, pc_first, FILENAME_MAX-1);
	sprintf_original(pc_first, pc_name_dst);

	i_fd_out = open(pc_name_dst, O_LARGEFILE | O_WRONLY | O_CREAT, 0666);

	if (i_fd_out < 0) {
		printf("Can't open output file\n");
		goto join_end;
	}

	pc_buf = allocate_buf(i_buf_size, &i_buf_size);

	if (!pc_buf) {
		unlink(pc_name_dst);
		goto join_end;
	}

	/* Open a part file, write it into the deastination file */

	do {
		i_fd_in = open(pc_name_src, O_LARGEFILE | O_RDONLY, 0666);
		if (i_fd_in < 0)
			goto join_end;

		printf("Processing: %s -> %s\n", pc_name_src, pc_name_dst);

		do {

			i_written = 0;
			i_read = read(i_fd_in, pc_buf, i_buf_size);
			if (i_read > 0)
				i_written = write(i_fd_out, pc_buf, i_read);

			if (i_read != i_written) {
				printf("Writing error to file: %s read %lld, write %lld\n",
						pc_name_src, i_read, i_written);
				perror("Error:");
				goto join_end;
			}

		} while (i_read > 0);

		close(i_fd_in);

	} while (increase_name_num(pc_name_src, FILENAME_MAX) > 0);

	/* All right, set return value to 0 */
	i_rv = 0;

join_end:
	if (pc_name_src)
		free(pc_name_src);
	if (pc_name_dst)
		free(pc_name_dst);
	close(i_fd_out);
	if (pc_buf)
		free(pc_buf);
	return i_rv;
}


static off64_t size_to_digit(char *pc_size)
{
	int i_strlen;
	off64_t i_size_multi = 1;
	off64_t i_size;
	char ac_str[256];
	E();
	bzero(ac_str, 256);

	i_strlen = strlen(pc_size);
	if (isalpha(pc_size[i_strlen - 1])) {
		if (pc_size[i_strlen - 1] == 'B' ||
			pc_size[i_strlen - 1] == 'b')
			i_size_multi = (1);
		else if (pc_size[i_strlen - 1] == 'K' ||
				pc_size[i_strlen - 1] == 'k')
			i_size_multi = (1<<10);
		else if (pc_size[i_strlen - 1] == 'M' ||
				pc_size[i_strlen - 1] == 'm')
			i_size_multi = (1<<20);
		else if (pc_size[i_strlen - 1] == 'G' ||
				pc_size[i_strlen - 1] == 'g')
			i_size_multi = (1<<30);
		else {
			printf("Unknown size: %c\n", pc_size[i_strlen - 1]);
			return 0;

		}
	}

	memcpy(ac_str, pc_size, i_strlen-1);
	ac_str[i_strlen - 1] = '\0';
	i_size = atoll(ac_str) * i_size_multi;
	return i_size;
}


static core_t *parse_args(int i_arg, char **ppc_arg)
{
	int i;
	core_t *ps_score = calloc(1, sizeof(core_t));
	E();
	if (!ps_score) {
		printf("Can't allocate score struct\n");
		return NULL;
	}

	for (i = 1; i < i_arg ;) {
		if (!strcmp(ppc_arg[i], "-s")) {
			ps_score->c_what = DO_SPLIT;
			ps_score->pc_origin_name = strdup(ppc_arg[i+1]);
			if (!ps_score->pc_origin_name) {
				printf("Can't duplicate origin name: split\n");
				free(ps_score);
				return NULL;
			}
			i += 2;
			continue;
		}

		if (!strcmp(ppc_arg[i], "-j")) {
			ps_score->c_what = DO_JOIN;
			ps_score->pc_origin_name = strdup(ppc_arg[i+1]);
			if (!ps_score->pc_origin_name) {
				printf("Can't duplicate origin name: join\n");
				free(ps_score);
				return NULL;
			}
			i += 2;
			continue;
		}

		/* -b block size. There may be given chain of values.
		 * There i2 2 forms of sizes are allowed:
		 * -b 1024M 512M 200M 2G:
		 *	in such form rest of file will be
		 *	cut to last given size, 2G chunks.
		 * -b 1024M 512M 1G -r:
		 *	-r means "repeat" :
		 *	it will be chunked to these sizes again and again:
		 *	1024M 512M 1G 1024M 512M 1G ...
		 */
		if (!strcmp(ppc_arg[i], "-b")) {
			i++;
			/* Begin parsing until next -something :) */
			while (ppc_arg[i] && ppc_arg[i][0] != '-') {
				ps_score->sizes[ps_score->i_sizes_size]
					= size_to_digit(ppc_arg[i++]);
				if (ps_score->sizes[ps_score->i_sizes_size] > 0)
					ps_score->i_sizes_size++;
			}

			if (ppc_arg[i] && !strcmp(ppc_arg[i++], "-r"))
				ps_score->c_sizes_repeat = SIZES_RR;
			continue;
		}
	}
	return ps_score;
}


int free_core(core_t *ps_core)
{
	if (ps_core->pc_origin_name)
		free(ps_core->pc_origin_name);
	free(ps_core);
	return 0;
}


int main(int i_arg, char **ppc_arg)
{
	core_t *ps_score;
	E();
	ps_score = parse_args(i_arg, ppc_arg);

	if (!ps_score)
		return 0;

	if (DO_SPLIT == ps_score->c_what)
		split_file3(ps_score);
	if (DO_JOIN == ps_score->c_what)
		join_files(ps_score->pc_origin_name, MAX_BUF_SIZE);

	free_core(ps_score);
	return 0;
}


