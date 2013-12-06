/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/* CAPFS_LSEEK.C - parallel seek call
 *
 * capfs_lseek() moves the read-write file offset on a parallel file in a 
 * manner similar to the unix lseek() call.  It accepts 3 parameters: a
 * parallel file descriptor, an offset value, and a "whence" parameter 
 * used to specify how to interpret the offset.
 *
 * whence may have 1 of two values, SEEK_SET or SEEK_CUR.  SEEK_SET
 * specifies to set the offset to the value given.  SEEK_CUR specifies that
 * the offset value should be added to the current offset.  Note that the
 * offset value may be negative.
 *
 * update: whence may now have the value of SEEK_END.  I finally got around
 * to fixing it.
 *
 * Upon successful completion, capfs_lseek() returns the current offset, 
 * measured in bytes from the beginning of the file.  If it fails, 
 * -1 is returned and the offset is not changed.
 */

/* CAPFS INCLUDE FILES */
#include <lib.h>

/* UNIX INCLUDE FILES */
#include <errno.h>
#include <unistd.h>

/* GLOBALS */
extern fdesc_p pfds[];

/* FUNCTIONS */
int capfs_lseek(int fd, int off, int whence)
{
	int i;
	struct stat filestat;

	if (fd < 0 || fd >= CAPFS_NR_OPEN 
	    || (pfds[fd] && pfds[fd]->fs == FS_RESV)) {
		errno = EBADF;
		return(-1);
	}  

	/* we're handling directories within CAPFS now, so don't call lseek() */
	/* for them. */
	if (!pfds[fd] || pfds[fd]->fs == FS_UNIX) {
		if (pfds[fd]) /* gotta keep our info up to date */ {
			return(pfds[fd]->fd.off = lseek(fd, off, whence));
		}
		else {
			return lseek(fd, off, whence);
		}
	}

	switch(whence) {
		case SEEK_SET:
			if (off >= 0) return(pfds[fd]->fd.off = off);
			errno = EINVAL;
			return(-1);
		case SEEK_CUR:
			if (pfds[fd]->fd.off + off >= 0) return(pfds[fd]->fd.off += off);
			errno = EINVAL;
			return(-1);
		case SEEK_END:
			/* for directories, this doesn't work yet */
			if (pfds[fd]->fs == FS_PDIR) {
				errno = EINVAL;
				return(-1);
			}
			/* find the actual end of the file */
			if ((i = capfs_fstat(fd, &filestat)) < 0) {
				PERROR(SUBSYS_LIB,"Getting file size");
				return(-1);
			}
			if (off + filestat.st_size >= 0)
				return(pfds[fd]->fd.off = off + filestat.st_size);
		/* let error here and default drop through...behavior is the same */
	}
	errno = EINVAL;
	return(-1);
} /* end of CAPFS_LSEEK() */

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 *
 * vim: ts=3
 */
