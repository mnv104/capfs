/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/*
 * PVSTAT.C -	prints out information from metadata file
 *					Usage:  pvstat <filename>
 *
 */

#include <stdio.h>
#include <fcntl.h>
#include <capfs.h>

int capfs_mode = 1;

int main (int argc, char *argv[])
{
	int fd;
	capfs_filestat fstat;
	
	if (argc < 2) {
		fprintf(stderr, "usage: %s <filename>\n", argv[0]);
		return 1;
	}

	if ((fd = capfs_open(argv[1], O_RDONLY,0, NULL, NULL)) < 0) {
		perror("capfs_open");
		return 1;
	}

	if (capfs_ioctl(fd, GETMETA, &fstat) < 0) {
		perror("capfs_ioctl");
		return 1;
	}
	printf("%s: base = %d, pcount = %d, ssize = %d\n", argv[1], fstat.base,
	fstat.pcount, fstat.ssize);
	capfs_close(fd);

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
