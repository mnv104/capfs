/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#include <lib.h>
#include <errno.h>

extern fdesc_p pfds[];

int capfs_dup2(int old_fd, int new_fd)
{
	int ret;


	if (old_fd < 0 || old_fd >= CAPFS_NR_OPEN 
	    || new_fd < 0 || new_fd >= CAPFS_NR_OPEN
	    || (pfds[old_fd] && pfds[old_fd]->fs == FS_RESV) 
	    || (pfds[new_fd] && pfds[new_fd]->fs == FS_RESV))
	{
		errno = EBADF;
		return(-1);
	}  
	
	/* calling capfs_close() for CAPFS directories now too */
	if (pfds[new_fd]
	    && (pfds[new_fd]->fs == FS_CAPFS || pfds[new_fd]->fs == FS_PDIR))
	{
		capfs_close(new_fd);
	}
	if ((ret = dup2(old_fd, new_fd)) == -1) {
		return(-1);
	}
	else {
		if (pfds[old_fd] 
		    && (pfds[old_fd]->fs == FS_CAPFS 
			|| pfds[old_fd]->fs == FS_PDIR))
		{
			pfds[ret] = pfds[old_fd];
			pfds[ret]->fd.ref++;
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
