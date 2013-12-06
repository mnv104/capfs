/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/* capfs-testrandom.c
 *
 * This is derived from code given to me by Rajeev Thakur.  Dunno where
 * it originated.  It has changed quite a bit since then.
 *
 * It's purpose is to produce aggregate bandwidth numbers for varying
 * block sizes, number of processors, an number of iterations.
 *
 * Compile time defines determine whether or not it will use mpi, while
 * all other options are selectable via command line.
 *
 * This code differs from the capfs-test.c code in that it
 * randomly doles out blocks for the applications to access.  This gives
 * us another workload to play around with.  Other options,
 * such as correctness testing and number of iterations, have been
 * removed from this version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <error.h>
#include <string.h>
#include <sys/time.h>
#include <capfs.h>
#include <capfs_proto.h>
#include <time.h>
#include <errno.h>
#ifdef __USE_MPI__
#include <mpi.h>
#endif


/* DEFAULT VALUES FOR OPTIONS */
int64_t opt_block     = 1048576*16;
int64_t opt_nrblocks  = 1;
int     opt_iter      = 1; /* should always be 1 for this version */
int     opt_stripe    = -1;
int     opt_seed      = 0;
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
int init_blklist(int *blklist, int size);

extern fdesc_p pfds[];

/* globals needed for getopt */
extern char *optarg;
extern int optind, opterr;

int main(int argc, char **argv)
{
	char *buf, *tmp = NULL;
	int fd, i, mynod=0, nprocs=1, err, myerrno;
	int *blklist;
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
	double sq_read_tim, sq_write_tim;
#endif
	struct capfs_filestat pstat = {0,-1,16384};

	/* startup MPI and determine the rank of this process */
#ifdef __USE_MPI__
	MPI_Init(&argc,&argv);
	MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
	MPI_Comm_rank(MPI_COMM_WORLD, &mynod);
#endif

	/* parse the command line arguments */
	opt_seed = time(NULL);
	parse_args(argc, argv);

	srandom(opt_seed);

	pstat.ssize = opt_stripe;

	/* tell capfs that we want to pass in metadata and have tcp
	 * connections setup for us at capfs_open
	 */
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
	
	/* setup a buffer of data to write */
	if (!(tmp = (char *) malloc(opt_block + 256))) {
		perror("malloc");
		goto die_jar_jar_die;
	}
	buf = tmp + 128 - (((long)tmp) % 128);  /* align buffer */

	/* setup buffer for block list */
	if (!(blklist = (int *)malloc(opt_nrblocks * nprocs * sizeof(int)))) {
		perror("malloc(2)");
		goto die_jar_jar_die;
	}
#ifdef __USE_MPI__
	if (mynod == 0) {
		init_blklist(blklist, opt_nrblocks * nprocs);
	}
	MPI_Bcast(blklist, opt_nrblocks * nprocs, MPI_INT, 0, MPI_COMM_WORLD);
#else
	init_blklist(blklist, opt_nrblocks * nprocs);
#endif

	if (opt_write) {
		/* open the file for writing */
		if ((fd = capfs_open(opt_file, amode, 0644, &pstat, NULL)) < 0) {
			fprintf(stderr, "node %d, open error: %s\n", mynod,
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

		/* write out the data blocks, seeking appropriately */
		for (i=0; i < opt_nrblocks; i++) {
			/* seek to the right spot for this block */
			capfs_lseek(fd, blklist[mynod*opt_nrblocks + i]*opt_block, SEEK_SET);

			/* write out the block */
			err = capfs_write(fd, buf, opt_block);
			myerrno = errno;
			if (err < 0)
				fprintf(stderr, "node %d, write error, loc = %Ld: %s\n",
				mynod, mynod*opt_block, strerror(myerrno));
		}

		/* discover the ending time of the operation */
#ifdef __USE_MPI__
		etim = MPI_Wtime();
#else
		etim = Wtime();
#endif
		write_tim += (etim - stim);


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

		/* discover the start time */
#ifdef __USE_MPI__
		MPI_Barrier(MPI_COMM_WORLD);
		stim = MPI_Wtime();
#else
		stim = Wtime();
#endif

		/* read in the file data, seeking appropriately */
		for (i=0; i < opt_nrblocks; i++) {
			/* seek to the right spot for this block */
			capfs_lseek(fd, blklist[mynod*opt_nrblocks + i]*opt_block, SEEK_SET);

			/* read the block */
			err = capfs_read(fd, buf, opt_block);
			myerrno = errno;
			if (err < 0)
				fprintf(stderr, "node %d, read error, loc = %Ld: %s\n", mynod,
				mynod*opt_block, strerror(myerrno));
		}

		/* discover the end time */
#ifdef __USE_MPI__
		etim = MPI_Wtime();
#else
		etim = Wtime();
#endif
		read_tim += (etim - stim);

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
		
		printf("nr_procs = %d, nr_iter = %d, blk_sz = %ld, nr_blks = %ld\n",
		nprocs, opt_iter, (long)opt_block, (long) opt_nrblocks);
		
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
	
	while ((c = getopt(argc, argv, "s:b:n:f:p:R:wr")) != EOF) {
		switch (c) {
			case 'R': /* random seed */
				opt_seed = atoi(optarg);
				break;
			case 's': /* stripe */
				opt_stripe = atoi(optarg);
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
				"  -R            random seed\n"
				"  -n nrblks     number of blocks to access per task\n",
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


int init_blklist(int *blklist, int size)
{
	int i, j, tmpval, tmpindex;

	/* get the right numbers in the list first */
	for (i=0; i < size; i++) blklist[i] = i;

	/* now randomize the list:
	 * - for each element in the list, move it to a random place
	 * - do this three times (arbitrarily picked number)
	 */
	for (j=0; j < 3; j++) {
		for (i=0; i < size; i++) {
			tmpval = blklist[i];
			tmpindex = random() % size;
			blklist[i] = blklist[tmpindex];
			blklist[tmpindex] = tmpval;
		}
	}

	return 0;
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
