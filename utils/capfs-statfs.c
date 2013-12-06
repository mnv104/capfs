/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/* NOTES:
 *
 * This is a quick and silly little tool to allow for statfs of CAPFS
 * without the preloaded library or kernel module or whatever.  It
 * is here to make testing easier.  Enjoy.
 */

#include <stdio.h>
#include <stdlib.h>
#include <capfs.h>
#include <capfs_proto.h>
#include <stdlib.h>
#include <sys/statfs.h>

int capfs_mode = 1;

int capfs_statfs(char *path, struct statfs *buf);

int main(int argc, char **argv)
{
	struct statfs sfs;
	int64_t bsize;

	if (argc <= 1 || capfs_statfs(argv[1], &sfs) < 0) {
		perror("capfs_statfs:");
		exit(1);
	}

	bsize = sfs.f_bsize;

	printf("blksz = %Ld, total (bytes) = %Ld, free (bytes) = %Ld\n",
	bsize, (int64_t) sfs.f_blocks * bsize, (int64_t) sfs.f_bfree * bsize);
	exit(0);
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
