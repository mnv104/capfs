/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/* NOTES:
 *
 * This is a quick and silly little tool to allow for truncation of CAPFS
 * files without the preloaded library or kernel module or whatever.  It
 * is here to make testing easier.  Enjoy.
 */

#include <capfs.h>
#include <capfs_proto.h>

int capfs_mode = 1;

int main(int argc, char **argv)
{
	if (argc > 1) {
		capfs_truncate(argv[1], 3);
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
