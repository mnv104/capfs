/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#include <lib.h>
#include <meta.h>
#include <errno.h>

extern fdesc_p pfds[];

int capfs_flock(int fd, int operation)
{
	/* check for badness */
	if (fd < 0 || fd >= CAPFS_NR_OPEN 
	    || (pfds[fd] && pfds[fd]->fs == FS_RESV)) 
	{
		errno = EBADF;
		return(-1);
	}  
	/* check for UNIX */
	if (!pfds[fd] || pfds[fd]->fs==FS_UNIX) {
		return flock(fd, operation);
	}
	/* NOT YET IMPLEMENTED! */
	errno = ENOSYS;
	return(-1);
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
