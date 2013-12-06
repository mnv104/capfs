/*
 * Test if our file system is indeed sequentially consistent.
 * Written by Jason Ganetsky (ganetsky@psu.edu) for CAPFS.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <time.h>

#include "test_writes.h"

#define	BYTE_MAX	256

unsigned char memory_data[ MEMORY_CHUNK_SIZE ][ N_MEMORY_CHUNKS ];
unsigned char file_mirror[ FILE_SIZE ];
int test_file_fd;

struct config_t {
	char *fname;
	int seed;
	int no_writes;
	int verbose;
} config;

static inline void print_error( char *s, int lineno ) 
{
	char str[ 256 ];
	sprintf( str, "%s (line %d)", s, lineno );
	perror( str );
	return;
}

static inline int rand_number(int max ) 
{
	return ( random() % max );
}

static void generate_memory_data(void) 
{
	int i_chunk, i_byte;

	for ( i_chunk = 0; i_chunk < N_MEMORY_CHUNKS; i_chunk++ ) {
		printf( "Generating random data chunk #%d...\n", i_chunk + 1 );
		for ( i_byte = 0; i_byte < MEMORY_CHUNK_SIZE; i_byte++ )
			memory_data[ i_byte ][ i_chunk ] = rand_number( BYTE_MAX );
	}

	printf( "Random data generation complete!\n\n" );
	return;
}

/* returns 0 on success, and non-zero with error value */
static int write_mirrored( int offset, unsigned char *buf, int amount ) {
	/* in memory */
	memcpy( file_mirror + offset, buf, amount );

	/* on disk */
	if ( lseek( test_file_fd, offset, SEEK_SET ) < 0 ) {
		print_error( "lseek", __LINE__ );		
		return errno;
	}
	
	if ( write( test_file_fd, buf, amount ) < 0 ) {
		print_error( "write", __LINE__ );
		return errno;
	}

	return 0;
		
}

static int do_random_write(void)
{
	char *from;
	int to;

	from = &memory_data[ rand_number( MEMORY_CHUNK_SIZE - WRITE_SIZE ) ][ rand_number( N_MEMORY_CHUNKS ) ];
	to = rand_number( FILE_SIZE - WRITE_SIZE );

	return write_mirrored( to, from, WRITE_SIZE );					
}

/* returns 0 on unequal, 1 on equal, and negative on error */
static int compare_file_and_memory(void) 
{
	unsigned char test_buffer[ FILE_SIZE ];
	memset( test_buffer, 0, FILE_SIZE );

	if ( lseek( test_file_fd, 0, SEEK_SET ) < 0 ) {
		print_error( "lseek", __LINE__ );
		return -errno;
	}

	if ( read( test_file_fd, test_buffer, sizeof( test_buffer ) ) < 0 ) {
		print_error( "read", __LINE__ );
		return -errno;
	}

	if (config.verbose == 1)
	{
		int i, retval = 0, flag = 0;
		for (i = 0; i < FILE_SIZE; i++) {
			if (test_buffer[i] != file_mirror[i]) {
				if (flag == 0) {
					flag = 1;
				}
				printf("Byte %d differs in file (%d) and memory (%d)\n", i, test_buffer[i], file_mirror[i]);
			}
		}
		retval = (flag == 1) ? 0 : 1;
		return retval;
	}
	else
	{
		if ( memcmp( test_buffer, file_mirror, FILE_SIZE ) != 0 )
			return 0;
		else
			return 1;
	}
}

static int init(void) 
{
	srandom( config.seed );
	generate_memory_data();
	test_file_fd = open( config.fname, O_CREAT | O_RDWR, 0644 );
	if ( test_file_fd < 0 ) {
		print_error( "creat", __LINE__ );
		return -1;
	}
	
	memset( file_mirror, 0, sizeof( file_mirror ) );
	write( test_file_fd, file_mirror, sizeof( file_mirror ) );
	return 0;
}

static void usage(void) 
{
	printf( "USAGE:\n" );
	printf( "test_writes -f filename [-s seed] [-n writes] [-h]\n" );
	printf( "   filename = path to test file\n" );
	printf( "   seed     = random number seed (default is initialized by time)\n" );
	printf( "   writes   = number of writes performed (default is 100)\n" );
	printf( "   use -h to print this dialogue\n" );
}

/* returns -1 on error, 0 on success */
static int parse_args(int argc, char *argv[], struct config_t *conf_target ) 
{
	int optval, temp_fd;
	
	memset( conf_target, 0, sizeof( struct config_t ) );
	conf_target->seed = time( NULL );
	conf_target->no_writes = DEFAULT_NO_WRITES;
	conf_target->verbose = 0;

	char *optstring = "f:s:n:vh";

	while ( ( optval = getopt( argc, argv, optstring ) ) > 0 )
		switch ( optval ) {
			case 'h':
				usage();
				return -1;
			case 'v':
				conf_target->verbose = 1;
				break;
			case 'f':
				conf_target->fname = optarg;
				break;
			case 's':
				conf_target->seed = atoi( optarg );
				break;
			case 'n':
				conf_target->no_writes = atoi( optarg );
				break;
			}

	if ( conf_target->seed < 0 )
		conf_target->seed = -conf_target->seed;

	if ( conf_target->no_writes <= 0 ) {
		printf( "Illegal value for number of writes!\n\n" );
		usage();
		return -1;
	}

	if ( !conf_target->fname ) {
		printf( "Please specify name of file to test on!\n\n" );
		usage();
		return -1;
	}

	if ((temp_fd = creat( conf_target->fname, 0644 )) < 0 ) {
		perror( "Could not create test file" );
		printf( "\n" );
		usage();
		return -1;
	}

	close( temp_fd );

	printf( "Seed:    %d\n", conf_target->seed );
	printf( "Writes:  %d\n", conf_target->no_writes );
	printf( "Fname:   %s\n", conf_target->fname );
	printf( "\n" );
	return 0;
}

static void do_test(void)
{
	int result, i;
	int since_last_ckpoint = 0;
	int interval = config.no_writes / TEST_PROGRESS_CKPOINTS;
	for ( i = 0; i < config.no_writes; i++ ) {
		if ( ++since_last_ckpoint == interval ) {
			since_last_ckpoint = 0;
			printf( "%d out of %d writes completed\n", i + 1, config.no_writes );
		}
		do_random_write();
	}
	printf( "Writes completed... comparing...\n" );
	result = compare_file_and_memory();

	if ( result >= 0 ) {
		if ( !result ) 
			printf( "Test: FAIL\n" );
		else
			printf( "Test: Pass!!\n" );
	}
}

int main( int argc, char *argv[] ) 
{
	if ( parse_args( argc, argv, &config ) < 0 )
		return -1;

	
	if ( init() < 0 )
		return -1;

	do_test();	
	return 0;
}
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 *
 * vim: ts=3
 */

