/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/* u2p - copies a file on a unix filesystem to a parallel filesystem
 */

/* UNIX INCLUDES */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <log.h>

/* CAPFS INCLUDES */
#include <capfs.h>
#include <capfs_proto.h>

/* DEFINES */
#define MAX_BUF_SIZE 1024*1024*16
#define GET_TIMING

/* PROTOTYPES */
void usage(int argc, char **argv);

/* GLOBALS */
extern int optind;
extern char *optarg;

int capfs_mode = 0;

/* FUNCTIONS */
int main(int argc, char **argv)
{
	int c, ssize=-1, base=-1, pcount=-1;
	capfs_filestat metadata={0,0,0};
	int src, dest;
	char *buf;
	int64_t bufsize, readsize;
	struct stat filestat;

#ifdef GET_TIMING
	int total_size = 0;
	struct timeval start, end;
	struct timezone zone;
	long runtime = 0;
#endif

	while ((c = getopt(argc, argv, "s:n:b:")) != EOF) {
		switch(c) {
		case 's':
			ssize = atoi(optarg);
			break;
		case 'b':
			base = atoi(optarg);
			break;
		case 'n':
			pcount = atoi(optarg);
			break;
		case '?':
			usage(argc, argv);
			exit(0);
		}
	} /* end of options */
	if (optind >= (argc - 1)) {
		usage(argc, argv);
		exit(0);
	}

	/* open files */
	if ((src = capfs_open(argv[argc-2], O_RDONLY, 0, 0, 0)) == -1) {
		PERROR(SUBSYS_NONE,"capfs_open");
		exit(-3);
	}

	metadata.base = base;
	metadata.pcount = pcount;
	metadata.ssize = ssize;
	if ((dest = capfs_open(argv[argc-1], O_TRUNC|O_WRONLY|O_CREAT|O_META,
		0777, &metadata, 0)) == -1)
	{
		fprintf(stderr, "capfs_open: unable to open destfile %s: %s\n",
			argv[argc-1], strerror(errno));
		exit(-2);
	}
	capfs_ioctl(dest, GETMETA, &metadata);

	/* get a buffer of some (reasonably) intelligent size */
	capfs_fstat(src, &filestat);
	bufsize = ((MAX_BUF_SIZE) / (metadata.ssize * metadata.pcount)) *
		(metadata.ssize * metadata.pcount);
	
	if (bufsize == 0 || bufsize > (int64_t) MAX_BUF_SIZE) {
		bufsize = MAX_BUF_SIZE;
	}

	if (filestat.st_size < bufsize) {
		bufsize = filestat.st_size;
	}
	buf = (char *)malloc(bufsize);

	/* read & write until done */
	while ((readsize = capfs_read(src, buf, bufsize)) > 0) {
#ifdef GET_TIMING
		/* start timing */
		gettimeofday(&start, &zone);
#endif
		if (capfs_write(dest, buf, readsize) < readsize) {
			fprintf(stderr, "capfs_write: short write\n");
		}
#ifdef GET_TIMING
		/* stop timing */
		gettimeofday(&end, &zone);
		runtime += (end.tv_usec-start.tv_usec) + 
			1000000*(end.tv_sec-start.tv_sec);
		total_size += readsize;
#endif
	} /* end of while loop */
#ifdef GET_TIMING
	printf("%d node(s); ssize = %d; buffer = %d; %3.3fMBps (%d bytes total)\n",
			 metadata.pcount,
			 metadata.ssize,
			 (int) bufsize,
			 ((double)total_size / 1048576.0) / runtime * 1000000.0, total_size);
#endif
	/* close files and free memory */
	capfs_close(src);
	capfs_close(dest);
	free(buf);
	exit(0);
} /* end of main() */

void usage(int argc, char **argv)
{
	fprintf(stderr, "usage: %s [-s stripe] [-b base] [-n (# of nodes)]" 
		" srcfile destfile\n", argv[0]);
} /* end of usage() */
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
