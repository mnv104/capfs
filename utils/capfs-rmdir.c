/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

#include <capfs.h>
#include <capfs_proto.h>

int capfs_mode = 1;
int usage(int argc, char **argv);

/* NOTES:
 *
 * This is a quick and silly little tool to allow for removal of CAPFS
 * directories without the preloaded library or kernel module or whatever.  It
 * is here to make testing easier.  Enjoy.
 */
int main(int argc, char **argv)
{
	char *filename;

	if ( argc != 2 ) {
		usage(argc, argv);
		return -1;
	}
	filename=strdup(argv[1]);

	if ( capfs_rmdir(filename) == -1 ) {
		perror("capfs_rmdir");
		return -1;
	}
	return 0;
}

int usage(int argc, char **argv)
{
	fprintf(stderr, "usage: %s <filename>\n", argv[0]);
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
