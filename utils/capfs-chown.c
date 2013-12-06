/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#include <capfs.h>
#include <capfs_proto.h>
#include <stdlib.h>

int capfs_mode = 1;

int main(int argc, char **argv)
{
	if (argc > 3) {
		capfs_chown(argv[1], atoi(argv[2]), atoi(argv[3]));
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
