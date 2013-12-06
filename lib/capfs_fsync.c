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

#define PCNT pfds[fd]->fd.meta.p_stat.pcount
#define FINO pfds[fd]->fd.meta.u_stat.st_ino

/* capfs_fsync() - sends sync request to iods; uses FDATASYNC request
 */
int capfs_fsync(int fd)
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
		return fsync(fd);
	}

	if (pfds[fd]->fs==FS_PDIR) {
		/* call the syscall for CAPFS directories */
		return fsync(fd);
	}

	return(capfs_fdatasync(fd));
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
