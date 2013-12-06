/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#include <lib.h>
#include <errno.h>
#include <log.h>

extern fdesc_p pfds[];

void *capfs_exit(void)
{
	int i;

	for (i = 0; i < CAPFS_NR_OPEN; i++) {
		if (!pfds[i] || pfds[i]->fs != FS_CAPFS) continue;
		capfs_close(i);
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "Closed open files\n");
	cleanup_iodtable();
	return(0);
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
