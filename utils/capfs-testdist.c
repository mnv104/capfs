/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/* capfs-testdist.c
 *
 * This is derived from code given to me by Rajeev Thakur.  Dunno where
 * it originated.
 *
 * It's purpose is to produce aggregate bandwidth numbers for varying
 * block sizes, number of processors, an number of iterations.
 *
 * Compile time defines determine whether or not it will use mpi, while
 * all other options are selectable via command line.
 *
 * This code differs from the capfs-test.c code in that it allows for
 * some more interesting distributions to be tested.  Other options,
 * such as correctness testing and number of iterations, have been
 * removed from this version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <error.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include "capfs.h"
#include "capfs_proto.h"
#ifdef __USE_MPI__
#include <mpi.h>
#endif


/* DEFAULT VALUES FOR OPTIONS */
int64_t opt_block     = 1048576*16;
int64_t opt_nrblocks  = 1;
int64_t opt_dist      = 0;
int     opt_iter      = 1; /* should always be 1 */
int     opt_stripe    = -1;
int     opt_read      = 1;
int     opt_write     = 1;
int     amode         = O_RDWR | O_CREAT;
char    opt_file[256] = "/foo/test.out\0";
char    opt_capfstab[256] = "notset\0";
int     opt_capfstab_set = 0;

int     capfs_mode = 0;

/* function prototypes */
int parse_args(int argc, char **argv);
double Wtime(void);

extern fdesc_p pfds[];

/* globals needed for getopt */
extern char *optarg;
extern int optind, opterr;

int main(int argc, char **argv)
{
	char *buf, *tmp = NULL;  
	int fd, mynod=0, nprocs=1, err, myerrno;
	double stim, etim;
	double write_tim = 0;
	double read_tim = 0;
	double read_bw, write_bw;
	double max_read_tim, max_write_tim;
	double min_read_tim, min_write_tim;
	double sum_read_tim, sum_write_tim;
	double ave_read_tim, ave_write_tim;
	double var_read_tim, var_write_tim;
	double sumsq_read_tim = 0.0, sumsq_write_tim = 0.0;
#ifdef __USE_MPI__
	double sq_read_tim = 0.0, sq_write_tim = 0.0;
#endif
	struct capfs_filestat pstat = {0,-1,16384};
	struct fpart logicalpart;

	/* startup MPI and determine the rank of this process */
#ifdef __USE_MPI__
	MPI_Init(&argc,&argv);
	MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
	MPI_Comm_rank(MPI_COMM_WORLD, &mynod);
#endif

	/* parse the command line arguments */
	parse_args(argc, argv);
	pstat.ssize = opt_stripe;
	/* tell capfs that we want to pass in metadata and have tcp
	 * connections setup for us at capfs_open */
	amode = amode | O_META | O_CONN;
	if (mynod == 0)
		printf("# Using native capfs calls.\n");
	
	/* kindof a weird hack- if the location of the capfstab file was 
	 * specified on the command line, then spit out this location into
	 * the appropriate environment variable: */
	
	if	(opt_capfstab_set) {
		if ((setenv("CAPFSTAB_FILE", opt_capfstab, 1)) < 0) {
			perror("setenv");
			goto die_jar_jar_die;
		}
	}
	
	/* this is our logical partition, used to allow for a single request
	 * of the discontiguous data
	 */
	logicalpart.offset = mynod * opt_block;
	logicalpart.gsize = opt_block;
	logicalpart.stride = opt_dist;
	logicalpart.gstride = 0;
	logicalpart.ngroups = 0;

	/* setup a buffer of data to write */
	if (!(tmp = (char *) malloc(opt_block*opt_nrblocks + 256))) {
		perror("malloc");
		goto die_jar_jar_die;
	}
	buf = tmp + 128 - (((long)tmp) % 128);  /* align buffer */

	if (opt_write) {
		/* open the file for writing */
		if ((fd = capfs_open(opt_file, amode, 0644, &pstat, NULL)) < 0) {
			fprintf(stderr, "node %d, open error: %s\n", mynod,
			strerror(errno));
			goto die_jar_jar_die;
		}

		/* set our partition */
		if (capfs_ioctl(fd, SETPART, &logicalpart) < 0) {
			fprintf(stderr, "node %d, partition setting error: %s\n", mynod,
			strerror(errno));
			goto die_jar_jar_die;
		}

		/* discover the starting time of the operation */
#ifdef __USE_MPI__
		MPI_Barrier(MPI_COMM_WORLD);
		stim = MPI_Wtime();
#else
		stim = Wtime();
#endif

		/* write out the data */
		err = capfs_write(fd, buf, opt_block * opt_nrblocks);

		myerrno = errno;

		/* discover the ending time of the operation */
#ifdef __USE_MPI__
		etim = MPI_Wtime();
#else
		etim = Wtime();
#endif
		write_tim += (etim - stim);

		if (err < 0)
			fprintf(stderr, "node %d, write error, loc = %Ld: %s\n",
			mynod, mynod*opt_block, strerror(myerrno));

		/* close the file */
		if (capfs_close(fd) < 0)
			fprintf(stderr, "node %d, close error after write\n", mynod);
		 
		/* wait for everyone to synchronize at this point */
#ifdef __USE_MPI__
		MPI_Barrier(MPI_COMM_WORLD);
#endif
	} /* end of if (opt_write) */

	if (opt_read) {
		/* open the file to read the data back out */
		if ((fd = capfs_open(opt_file, amode, 0644, &pstat, NULL)) < 0) {
			fprintf(stderr, "node %d, open error: %s\n", mynod,
			strerror(errno));
			goto die_jar_jar_die;
		}

		/* set our partition */
		if (capfs_ioctl(fd, SETPART, &logicalpart) < 0) {
			fprintf(stderr, "node %d, partition setting error: %s\n", mynod,
			strerror(errno));
			goto die_jar_jar_die;
		}

		/* discover the start time */
#ifdef __USE_MPI__
		MPI_Barrier(MPI_COMM_WORLD);
		stim = MPI_Wtime();
#else
		stim = Wtime();
#endif

		/* read in the file data */
		err = capfs_read(fd, buf, opt_block * opt_nrblocks);
		myerrno = errno;

		/* discover the end time */
#ifdef __USE_MPI__
		etim = MPI_Wtime();
#else
		etim = Wtime();
#endif
		read_tim += (etim - stim);

		if (err < 0)
			fprintf(stderr, "node %d, read error, loc = %Ld: %s\n", mynod,
			mynod*opt_block, strerror(myerrno));

		/* close the file */
		capfs_close(fd);
	} /* end of if (opt_read) */


#ifdef __USE_MPI__
	MPI_Allreduce(&read_tim, &max_read_tim, 1, MPI_DOUBLE, MPI_MAX,
		MPI_COMM_WORLD);
	MPI_Allreduce(&read_tim, &min_read_tim, 1, MPI_DOUBLE, MPI_MIN,
		MPI_COMM_WORLD);
	MPI_Allreduce(&read_tim, &sum_read_tim, 1, MPI_DOUBLE, MPI_SUM,
		MPI_COMM_WORLD);
	/* calculate our part of the summation used for variance */
	sq_read_tim = read_tim - (sum_read_tim / nprocs);
	sq_read_tim = sq_read_tim * sq_read_tim;
	MPI_Allreduce(&sq_read_tim, &sumsq_read_tim, 1, MPI_DOUBLE, MPI_SUM,
		MPI_COMM_WORLD);
#else
	max_read_tim = read_tim;
	min_read_tim = read_tim;
	sum_read_tim = read_tim;
#endif


#ifdef __USE_MPI__
	MPI_Allreduce(&write_tim, &max_write_tim, 1, MPI_DOUBLE, MPI_MAX,
		MPI_COMM_WORLD);
	MPI_Allreduce(&write_tim, &min_write_tim, 1, MPI_DOUBLE, MPI_MIN,
		MPI_COMM_WORLD);
	MPI_Allreduce(&write_tim, &sum_write_tim, 1, MPI_DOUBLE, MPI_SUM,
		MPI_COMM_WORLD);
	/* calculate our part of the summation used for variance */
	sq_write_tim = write_tim - (sum_write_tim / nprocs);
	sq_write_tim = sq_write_tim * sq_write_tim;
	MPI_Allreduce(&sq_write_tim, &sumsq_write_tim, 1, MPI_DOUBLE, MPI_SUM,
		MPI_COMM_WORLD);
#else
	max_write_tim = write_tim;
	min_write_tim = write_tim;
	sum_write_tim = write_tim;
#endif
	/* calculate the mean from the sum */
	ave_read_tim = sum_read_tim / nprocs; 
	ave_write_tim = sum_write_tim / nprocs; 

	/* calculate variance */
	if (nprocs > 1) {
		var_read_tim = sumsq_read_tim / (nprocs-1);
		var_write_tim = sumsq_write_tim / (nprocs-1);
	}
	else {
		var_read_tim = 0;
		var_write_tim = 0;
	}

	
	/* print out the results on one node */
	if (mynod == 0) {
	   read_bw = ((int64_t)(opt_block*nprocs*opt_nrblocks))/
		(max_read_tim*1000000.0);
	   write_bw = ((int64_t)(opt_block*nprocs*opt_nrblocks))/
		(max_write_tim*1000000.0);
		
		printf("nr_procs = %d, nr_iter = %d, blk_sz = %ld, nr_blks = %ld, dist = %ld\n",
		nprocs, opt_iter, (long)opt_block, (long) opt_nrblocks, (long)opt_dist);
		
		printf("# total_size = %ld\n", (long)(opt_block*nprocs*opt_nrblocks));
		
		if (opt_write)
			printf("# Write:  min_t = %f, max_t = %f, mean_t = %f, var_t = %f\n", 
			min_write_tim, max_write_tim, ave_write_tim, var_write_tim);
		if (opt_read)
			printf("# Read:  min_t = %f, max_t = %f, mean_t = %f, var_t = %f\n", 
			min_read_tim, max_read_tim, ave_read_tim, var_read_tim);
		
		if (opt_write)
			printf("Write bandwidth = %f Mbytes/sec\n", write_bw);
	   if (opt_read)
			printf("Read bandwidth = %f Mbytes/sec\n", read_bw);
	}


die_jar_jar_die:
	free(tmp);
#ifdef __USE_MPI__
	MPI_Finalize();
#endif
	return(0);
}

int parse_args(int argc, char **argv)
{
	int c;
	
	while ((c = getopt(argc, argv, "s:b:d:n:f:p:wr")) != EOF) {
		switch (c) {
			case 's': /* stripe */
				opt_stripe = atoi(optarg);
				break;
			case 'd': /* distance between start of blocks */
				opt_dist = atoi(optarg);
				break;
			case 'n': /* number of blocks */
				opt_nrblocks = atoi(optarg);
				break;
			case 'b': /* block size */
				opt_block = atoi(optarg);
				break;
			case 'f': /* filename */
				strncpy(opt_file, optarg, 255);
				break;
			case 'p': /* capfstab file */
				strncpy(opt_capfstab, optarg, 255);
				opt_capfstab_set = 1;
				break;
			case 'r': /* read only */
				opt_write = 0;
				opt_read = 1;
				break;
			case 'w': /* write only */
				opt_write = 1;
				opt_read = 0;
				break;
			case '?': /* unknown */
			default:
				fprintf(stderr, "usage: %s [-b blksz] [-n nrblks] [-f file] "
				"[-p capfstab] [-s stripe] [-d stride] [-w] [-r]\n\n"
				"  -b blksz      access blocks of blksz bytes (default=16MB)\n"
				"  -f file       name of file to access\n"
				"  -p capfstab    location of capfstab file (default=/etc/capfstab)\n"
				"  -s stripe     stripe size for output file if created\n"
				"  -w            write only\n"
				"  -r            read only\n"
				"  -n nrblks     number of blocks to access per task\n"
				"  -d dist       distance between start of one block and next\n",
				argv[0]);
				exit(1);
				break;
		}
	}
	return(0);
}

/* Wtime() - returns current time in sec., in a double */
double Wtime()
{
	struct timeval t;
	
	gettimeofday(&t, NULL);
	return((double)t.tv_sec + (double)t.tv_usec / 1000000);
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
