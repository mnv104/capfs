/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/* NOTES:
 *
 * This is a quick and silly little tool to allow for stat of CAPFS files
 * without the preloaded library or kernel module or whatever.  It
 * is here to make testing easier.  Enjoy.
 */

#include <stdio.h>
#include <stdlib.h>
#include <capfs.h>
#include <capfs_proto.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

int capfs_mode = 1;

int main(int argc, char **argv)
{
	struct stat sbuf;

	if (argc <= 1 || capfs_stat(argv[1], &sbuf) < 0) {
		perror("capfs_stat:");
		exit(1);
	}

	printf("%s:\n", argv[1]);
	printf("st_dev = %d, st_ino = %d, st_mode = %o, st_nlink = %d\n",
	(int) sbuf.st_dev, (int) sbuf.st_ino, (int) sbuf.st_mode, (int) sbuf.st_nlink);
	printf("st_uid = %d, st_gid = %d, st_rdev = %d, st_size = %d\n",
	(int) sbuf.st_uid, (int) sbuf.st_gid, (int) sbuf.st_rdev, (int) sbuf.st_size);
	printf("st_blksize = %d, st_blocks = %d, st_atime = %d\n",
	(int) sbuf.st_blksize, (int) sbuf.st_blocks, (int) sbuf.st_atime);
	printf("st_mtime = %d\n", (int) sbuf.st_mtime);

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
