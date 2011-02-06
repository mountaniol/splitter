#ifndef _splitdslfkgjhlasdjfhakdjfhalskdjfh_
#define _splitdslfkgjhlasdjfhakdjfhalskdjfh_

#define MAX_SIZES 1024

#define DO_SPLIT 	1
#define DO_JOIN		2

struct split_core
{
	char 			c_what;
	char * 			pc_origin_name;		/* Name of original file */
	off64_t			sizes[MAX_SIZES];	/* sizes of splitted files */
	char 			c_sizes_repeat;		/* this variable keeps mode of sizes: '-r' means "round robin" mode, else it repeat last member */
	int 			i_sizes_size;		/* sizes of "sizes" buffer */

	int 			i_origin_fd;
	char * 			pc_origin_map;
	struct 	stat 	s_origin_stat;
	int 			i_seq;					/* Sequential number of current part */
};

typedef struct split_core core_t;


typedef struct thread_arg
{
	int i_fd;		/* File descriptor */
	int i_size;		/* Size of result file */
	int i_seq;		/* Sequential number, where first is 0 */
} targ_t;


#define SIZES_USE_LAST 0
#define SIZES_RR 1

#endif /* _splitdslfkgjhlasdjfhakdjfhalskdjfh_ */
