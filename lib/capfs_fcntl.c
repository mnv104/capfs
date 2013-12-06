/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/* CAPFS INCLUDES */
#include <lib.h>

/* UNIX INCLUDES */
#include <errno.h>
#include <unistd.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

/* GLOBALS/EXTERNS */
extern fdesc_p pfds[];

int capfs_fcntl(int fd, int cmd, long arg)
{
	if (fd < 0 || fd >= CAPFS_NR_OPEN 
	    || (pfds[fd] && pfds[fd]->fs == FS_RESV)) {
		errno = EBADF;
		return(-1);
	}  
	if (!pfds[fd] || pfds[fd]->fs == FS_UNIX) /* not a CAPFS file */ {
		return fcntl(fd, cmd, arg);
	}

	if (pfds[fd]->fs == FS_PDIR) /* CAPFS directory -- just make the call */ {
		return fcntl(fd, cmd, arg);
	}

	/* we could try to lock on the NFS metadata here for F_SETLKW or
	 * F_SETLK, but we need to make sure I didn't just open /dev/null in
	 * that case...
	 */
	switch (cmd) {
	case F_DUPFD :
		errno = EINVAL;
		LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " capfs_fcntl: command not yet implemented\n");
		return(-1);
	case F_GETFD :
		return(1); /* CAPFS files will not remain open on exec */
	case F_SETFD :
		if ((arg & 1) != 0) return(0); /* WHAT IS THIS???? */
		errno = EINVAL;
		LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " capfs_fcntl: command not yet implemented\n");
		return(-1);
	case F_GETFL :
		return(pfds[fd]->fd.flag);
	case F_SETFL :
	case F_GETLK :
	case F_SETLK :
	case F_SETLKW :
	case F_GETOWN :
	case F_SETOWN :
	default :
		errno = EINVAL;
		LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " capfs_fcntl: command not yet implemented\n");
		return(-1);
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
