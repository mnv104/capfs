/*
 * Based on code from RobL (robl@mcs.anl.gov)
 * for detecting seq. consistency violations.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <errno.h>

#include "mpi.h"

#include <string.h>
#include <pthread.h>

struct options {
	int block;
	int steps;
	int do_sync;
	int verbose;
	int amode;
	int iterations;
	char * file;
};

static struct options opt = { 0, /* block */
	0, 		/* steps */
	0, 		/* do_sync */
	0,		/* verbose */
	O_RDWR|O_CREAT, /* amode */
	0, 		/* iterations */ 
	"/mnt/capfs/wr1", 		/* file */
};


struct statistics {
	int passes;
	int errors;
};


extern void print_error(int myerrno, char *msg); 
extern int parse_args(int argc, char ** argv );
extern int seed_file(int fd, int value, int processes);
extern int sync_file(int fd);
extern int verify_file(int fd, int mynod, int nprocs);
extern int do_write(int fd, int mynod, int nprocs);
extern int reopen(int fd, char *fname);


int main(int argc, char ** argv ) {

	int nprocs=1, mynod=0;
	int fd, i;
	int numerrors=0, all_errors=0;
	int passes=0, all_passes=0;
	struct stat statbuf;



	/* startup MPI and determine the rank of this process */
	MPI_Init(&argc,&argv);
	MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
	MPI_Comm_rank(MPI_COMM_WORLD, &mynod);

	parse_args(argc, argv);

	/* to help differentiate, each process will write it's rank to file*/
	if (( fd = open(opt.file, opt.amode, 0644 ))  < 0 ) {
			fprintf(stderr, "node %d, open error: %s\n", mynod, 
					strerror(errno));
					return(errno);
	}
	fstat(fd, &statbuf);
	for ( i = 0; i < opt.iterations; i++ ) {
		/* seed values into the file. makes it easier to see where the
		 * holes ended up */
		seed_file(fd, -1, nprocs);
		/* sync-close-write-reopen: the hope is to reliably set up a
		 * read-modify-write condition.  open-write-sync-write didn't
		 * seem to do that  */
		fd=reopen(fd, opt.file );

		/* each writer writes a bunch of chunks
		 * don't write where other writers write */
		do_write(fd, mynod, nprocs);

		/* writing should be done before verifying */
		if ( opt.do_sync != 0 ) sync_file(fd);
		/* verify */
		if ( verify_file(fd, mynod, nprocs) != 0 )
			++numerrors;
		++passes;
		/* let everyone catch up before the next round */
		MPI_Barrier(MPI_COMM_WORLD);
	}

	/* cleanup */

	MPI_Allreduce(&numerrors, &all_errors, 1, 
		MPI_INT, MPI_SUM, MPI_COMM_WORLD);
	MPI_Allreduce(&passes, &all_passes, 1, 
			MPI_INT, MPI_SUM, MPI_COMM_WORLD);
	MPI_Finalize();
	if ( mynod == 0 ) {
			fprintf ( stderr, "passes: %d ; sync: %d,"
					" block: %d, steps: %d, errors: %d\n",
					all_passes, opt.do_sync, opt.block, 
					opt.steps, all_errors);
	}
	return(all_errors);
}	

int do_write(int fd, int mynod, int nprocs) {
	int i;
	off_t offset;
	char *buf;

	if ( ! (buf=(char *)malloc(opt.block + 256))) { 
		print_error(errno, "malloc");
	}
	memset(buf, mynod, opt.block);
	for ( i = 0; i < opt.steps; i++ ) {
		int seek_to = mynod*opt.block + i*nprocs*opt.block;
		if((offset=lseek(fd, seek_to, SEEK_SET))==(off_t)-1){
			print_error(errno, "lseek");
		}
		/* fprintf(stderr, "writing 0x%X at 0x%lX\n", *buf, offset); */
		if ( write(fd, buf, opt.block) == -1 ) {
			print_error(errno, "write");
		}
	}
	free(buf);
	return(0);
}

int sync_file(int fd) {
	MPI_Barrier(MPI_COMM_WORLD);
	fsync(fd);
	MPI_Barrier(MPI_COMM_WORLD);
	return(0);
}

int verify_file(int fd, int mynod, int nprocs) {

	char *buf;
	char *buf2;
	char nodename[MPI_MAX_PROCESSOR_NAME];
	int i,nbytes,numerrors=0,resultlen;
	off_t offset;

	if ( ! (buf=(char *)malloc(opt.block + 256))) { 
		print_error(errno, "malloc");
	}
	if ( ! (buf2=(char *)malloc(opt.block + 256))) { 
		print_error(errno, "malloc");
	}
	if (( offset=lseek(fd, 0, SEEK_SET)) == (off_t)-1){
		print_error(errno, "lseek");
	}
	for ( i=0; i < opt.steps; i++ ) {
		memset(buf2, (i%nprocs), opt.block);
		memset(buf, (i%nprocs), opt.block);
		/* XXX: what about when read doesn't read back 
		 * all the requested data? */
		if( (nbytes = read(fd, buf, opt.block)) == -1 ) 
			print_error(errno, "read");
		if ( nbytes != opt.block ) 
			fprintf( stderr, "short read\n" );
		if ( memcmp(buf, buf2, opt.block) != 0 ) {
			numerrors++;
			if ( opt.verbose != 0 ) {
				MPI_Get_processor_name(nodename, &resultlen);
				fprintf(stderr, "inconsistency found by %d(%s)"
						"at 0x%X:"
					" expected: 0x%X found: 0x%X\n", 
					mynod, nodename, i*opt.block, *buf2, *buf);
			}
		}
	}
	if (( offset=lseek(fd, 0, SEEK_SET)) == (off_t)-1){
		print_error(errno, "lseek");
	}
	free(buf);
	free(buf2);
	return (numerrors);
}

int reopen(int fd, char *fname) {
	fsync(fd);
	if ( close(fd) == -1  ) 
		print_error(errno, "close");
	MPI_Barrier(MPI_COMM_WORLD);
	if (( fd = open(opt.file, opt.amode, 0644 ))  < 0 ) {
			fprintf(stderr, "open error: %s\n",
					strerror(errno));
			return(errno);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	return(fd);
}

int parse_args(int argc, char ** argv ) {
	int c;
	while ( (c = getopt( argc, argv, "b:f:n:svi:" )) != -1) {
		switch(c) {
			case 'b': /* block */
				opt.block = atoi(optarg);
				break;
			case 'n': /* number of times to repeat pattern */
				opt.steps = atoi(optarg);
				break;
			case 'i': /* iterations */
				opt.iterations = atoi(optarg);
				break;
			case 's': /* sync after write */
				opt.do_sync = 1;
				break;
			case 'v': /* chatter about what's going on */
				opt.verbose = 1;
				break;
			case 'f':  /* filename */
		 		opt.file = strdup(optarg);
				if ( opt.file == NULL ) 
					print_error(0, "strdup");
				break;
			case 'h': /* help */
			case '?': /* undefined */
			case ':': /* missing parameter */
			default: 
				fprintf(stderr, "usage: %s [-b blksz ] "
						"[ -n number] [-i number] [-s] [-v]"
						"  -f filename "
						" \n-b blksz    access blocks of blksz bytes      (default 0)"
						" \n-n number   number of times to repeat pattern (default 0)"
						" \n-i number   number of test iterations         (default 0)"
						" \n-s          perform a sync after write        (default 0)"
						" \n-v          report where inconsitencies found (default 0)"
						" \n-f filename name of file to access           (default NULL)\n",
						argv[0]);
				exit(0);
				break;
		}
	}
	return 0;
}

void print_error(int myerrno, char *msg) {
		perror(msg);
		exit(myerrno);
}

int seed_file(int fd, int value, int nprocs) {
	char *stuff;
	int file_size, nbytes;

	file_size= opt.block*opt.steps*nprocs; 

	if ( (stuff=malloc(file_size+256)) == NULL ) 
		print_error(errno, "malloc");

	memset(stuff, value, file_size);
	if ( (nbytes = write(fd,  stuff, file_size) == -1 ))
		print_error(errno, "write");
	return(0);
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

