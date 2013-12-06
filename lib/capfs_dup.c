/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#include <lib.h>
#include <errno.h>

extern fdesc_p pfds[];

int capfs_dup(int fd)
{
	int ret;

	if (fd < 0 || fd >= CAPFS_NR_OPEN 
	    || (pfds[fd] && pfds[fd]->fs == FS_RESV)) 
	{
		errno = EBADF;
		return(-1);
	}

	if ((ret = dup(fd)) == -1) {
		return(-1);
	}
	else {
		if (pfds[fd] 
		    && (pfds[fd]->fs==FS_CAPFS || pfds[fd]->fs == FS_PDIR)) 
		{
			pfds[ret] = pfds[fd];
			pfds[fd]->fd.ref++;
		}
		return(ret);
	}
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
