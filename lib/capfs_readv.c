/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#include <lib.h>
#include <sys/uio.h>

extern fdesc_p pfds[];
extern int capfs_mode;

static int unix_readv(int fd, const struct iovec *vector, size_t count);

int capfs_readv(int fd, const struct iovec *vector, size_t count)
{
	int i, totsize = 0, ret;
	fdesc_p pfd_p = pfds[fd];

	if (fd < 0 || fd >= CAPFS_NR_OPEN 
	    || (pfds[fd] && pfds[fd]->fs == FS_RESV)) 
	{
		errno = EBADF;
		return(-1);
	}  
	if (capfs_mode == 1) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIB, "capfs_readv is unimplemented in CAPFS. Use VFS readv/writev interfaces\n");
		errno = ENOSYS;
		return -1;
	}

	if (!pfd_p || pfd_p->fs == FS_UNIX) return(unix_readv(fd, vector, count));
	if (pfd_p->fs == FS_PDIR) return(unix_readv(fd, vector, count));

	/* CAPFS file -- capfs_read will do the right thing, even with a partition */
	for (i=0; i < count; i++) {
		if (vector[i].iov_len == 0) continue;

		ret = capfs_read(fd, vector[i].iov_base, vector[i].iov_len);
		if (ret > 0) totsize += ret;
		else if (ret == 0) return totsize;
		else return -1;
	}
	return totsize;
}

static int unix_readv(int fd, const struct iovec *vector, size_t count)
{
	fdesc_p fd_p = pfds[fd];

	/* if there's no partition, then we don't have to do any work! */
	if (!fd_p || !fd_p->part_p) return(readv(fd, vector, count));

	errno = ENOSYS;
	return(-1);
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 */
