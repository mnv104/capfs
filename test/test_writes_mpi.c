/*
 * The same program as test_writes, except that this is an MPI
 * version...
 * Written by Jason Ganetsky (ganetsky@psu.edu) for triggering races
 * and correctness issues with capfs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>
#include <time.h>
#include <mpi.h>

#include "test_writes_mpi.h"

#define BYTE_MAX    256

unsigned char memory_data[ MEMORY_CHUNK_SIZE ][ N_MEMORY_CHUNKS ];
unsigned char file_mirror[ FILE_SIZE ];
int test_file_fd;

struct config_t {
    char *fname;
    int seed;
    int no_writes;
    int write_size;
};

static inline void print_error( char *s, int lineno ) 
{
    char str[ 256 ];
    sprintf( str, "%s (line %d)", s, lineno );
    perror( str );
	 return;
}

static inline int rand_number( int max ) 
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
}

/* returns 0 on success, and non-zero with error value */
static int write_mirrored( int mem_offset, int disk_offset, 
                            unsigned char *mem_buf, unsigned char *disk_buf,
                            int mem_amount, int disk_amount ) 
{
    /* in memory */
    memcpy( file_mirror + mem_offset, mem_buf, mem_amount );

    /* on disk */
    if ( lseek( test_file_fd, disk_offset, SEEK_SET ) < 0 ) {
        print_error( "lseek", __LINE__ );       
        return errno;
    }
    
    MPI_Barrier( MPI_COMM_WORLD );
    if ( write( test_file_fd, disk_buf, disk_amount ) < 0 ) {
        print_error( "write", __LINE__ );
        return errno;
    }

    return 0;
}

static int do_random_write( int my_chunk_start, int my_chunk_end, int write_size ) 
{
    unsigned char *from;
    int to;

    from = &memory_data[ rand_number( MEMORY_CHUNK_SIZE - write_size ) ][ rand_number( N_MEMORY_CHUNKS ) ];
    to = rand_number( FILE_SIZE - write_size );

    return write_mirrored( to, to + my_chunk_start,
                            from, from + my_chunk_start,
                            write_size, my_chunk_end - my_chunk_start );                  
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

    if ( memcmp( test_buffer, file_mirror, FILE_SIZE ) != 0 )
        return 0;
    else
        return 1;
    
}

static int init( struct config_t config ) 
{
    srandom( config.seed );
    generate_memory_data();
    test_file_fd = open( config.fname, O_RDWR );
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
    printf( "test_writes -f filename -s seed [-n writes] [-w size] [-h] \n" );
    printf( "   filename = path to test file\n" );
    printf( "   seed     = random number seed, must be greater than zero\n" );
    printf( "   writes   = number of writes performed (default is %d)\n", DEF_NO_WRITES );
    printf( "   size     = size of each write operation (default is %d)\n", DEF_WRITE_SIZE ); 
    printf( "   use -h to print this dialogue\n" );
}

/* returns -1 on error, 0 on success */
static int parse_args( int argc, char *argv[], struct config_t *conf_target ) 
{
    int optval, temp_fd;
    
    memset( conf_target, 0, sizeof( struct config_t ) );
    conf_target->seed = 0;
    conf_target->no_writes = DEF_NO_WRITES;
    conf_target->write_size = DEF_WRITE_SIZE;

    char *optstring = "f:s:n:h:w:";

    while ( ( optval = getopt( argc, argv, optstring ) ) > 0 )
        switch ( optval ) {
            case 'h':
                usage();
                break;
            case 'f':
                conf_target->fname = malloc( 256 );
                strncpy( conf_target->fname, optarg, 256 );
                break;
            case 's':
                conf_target->seed = atoi( optarg );
                break;
            case 'n':
                conf_target->no_writes = atoi( optarg );
                break;
            case 'w':
                conf_target->write_size = atoi( optarg );
                break;
            }

    if ( conf_target->seed <= 0 ) {
        printf( "Please provide a valid random number seed!\n\n" );
        usage();
       return -1;
    }

    if ( conf_target->no_writes <= 0 ) {
        printf( "Illegal value for number of writes!\n\n" );
        usage();
        return -1;
    }

    if ( conf_target->write_size <= 0 ) {
        printf( "Illegal value for write size!\n\n" );
        usage();
        return -1;
    }

    if ( !conf_target->fname ) {
        printf( "Please specify name of file to test on!\n\n" );
        usage();
        return -1;
    }

    if ( temp_fd = creat( conf_target->fname, 0644 ) < 0 ) {
        perror( "Could not create test file" );
        printf( "\n" );
        usage();
        return -1;
    }
    close( temp_fd );

    printf( "Seed:    %d\n", conf_target->seed );
    printf( "Writes:  %d\n", conf_target->no_writes );
    printf( "Size:    %d\n", conf_target->write_size );
    printf( "Fname:   %s\n", conf_target->fname );
    printf( "\n" );
    
    return 0;
}

static void do_test( int no_writes, int write_size, int ring_size, int my_rank ) 
{
    int result, i;
    int since_last_ckpoint = 0;
    int interval = no_writes / TEST_PROGRESS_CKPOINTS, resultlen;
	 char nodename[MPI_MAX_PROCESSOR_NAME];

    int my_chunk_start = ( write_size / ring_size ) * my_rank;
    int my_chunk_end = ( write_size / ring_size ) * ( my_rank + 1 );

    if ( my_rank == ( ring_size - 1 ) )
        my_chunk_end = write_size;

    for ( i = 0; i < no_writes; i++ ) {
        if ( ++since_last_ckpoint == interval ) {
            since_last_ckpoint = 0;
            printf( "%d out of %d writes completed\n", i + 1, no_writes );
        }
        do_random_write( my_chunk_start, my_chunk_end, write_size );
    }
    printf( "Writes completed... comparing...\n" );
    
    MPI_Barrier( MPI_COMM_WORLD );
    result = compare_file_and_memory();

    if ( result >= 0 ) {
		  MPI_Get_processor_name(nodename, &resultlen);
        if ( !result ) {
            printf( "Node %s Test: FAIL\n" , nodename);
		  }
        else {
            printf( "Node %s Test: Pass!!\n", nodename);
		  }
    }
	 return;
}

int main( int argc, char *argv[] ) 
{
    struct config_t config;

    int my_rank, ring_size;
    
    MPI_Init( &argc, &argv );
    MPI_Comm_size( MPI_COMM_WORLD, &ring_size );
    MPI_Comm_rank( MPI_COMM_WORLD, &my_rank );

    if ( parse_args( argc, argv, &config ) < 0 ) {
        MPI_Finalize();
        return -1;
    }

    if ( init( config ) < 0 ) {
        MPI_Finalize();
        return -1;
    }

    do_test( config.no_writes, config.write_size, ring_size, my_rank );  

    MPI_Finalize();

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

