/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */

#include <lib.h>
#include <time.h>
#include <log.h>

extern fdesc_p pfds[];
extern jlist_p active_p;
extern sockset socks;
extern int capfs_mode;

static int unix_write(int fd, char *buf, size_t count);

#define PCOUNT pfd_p->fd.meta.p_stat.pcount

int capfs_write(int fd, char *buf, size_t count)
{
	int i;
	int64_t size = 0;
	fdesc_p pfd_p = pfds[fd];

	if (fd < 0 || fd >= CAPFS_NR_OPEN 
	    || (pfds[fd] && pfds[fd]->fs == FS_RESV)) 
	{
		errno = EBADF;
		return(-1);
	}  
	if (capfs_mode == 1) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIB, "capfs_write is unimplemented on CAPFS\n");
		errno = ENOSYS;
		return -1;
	}

	if (!pfd_p || pfd_p->fs == FS_UNIX) return(unix_write(fd, buf, count));
	if (pfd_p->fs == FS_PDIR) return(unix_write(fd, buf, count));
#ifdef STRICT_FDESC_CHECK
	if (do_fdesc_check(fd) < 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIB,  "check failed at beginning of capfs_write()\n");
	}
#endif
	for (i = 0; i < PCOUNT; i++) {
		pfd_p->fd.iod[i].ack.dsize  = 0;
		pfd_p->fd.iod[i].ack.status = 0;
	}	

	/* build jobs, including requests and acks */
	if (build_rw_jobs(pfd_p, buf, count, J_WRITE) < 0) {
		LOG(stderr, WARNING_MSG, SUBSYS_LIB,  "capfs_write: build_rw_jobs failed\n");
		return(-1);
	}
	
	/* send requests; receive data and acks */
	while (!jlist_empty(active_p)) {
		if (do_jobs(active_p, &socks, -1) < 0) {
			PERROR(SUBSYS_LIB,"capfs_write: do_jobs");
			return(-1);
		}
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "capfs_write: finished with write jobs\n");
	for (i=0; i < PCOUNT; i++) {
		if (pfd_p->fd.iod[i].ack.status) {
			LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " capfs_write: non-zero status returned from iod %d\n", i);
			/* this is likely to be a ENOSPC on one node */
			errno = pfd_p->fd.iod[i].ack.eno;
			return(-1);
		}
		size += pfd_p->fd.iod[i].ack.dsize;
	}
	/* update modification time meta data */
	pfd_p->fd.meta.u_stat.mtime = time(NULL);
	pfd_p->fd.off += size;
	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "capfs_write: completed %Ld bytes; new offset = %Ld\n", size,
	 	pfd_p->fd.off);
#ifdef STRICT_FDESC_CHECK
	if (do_fdesc_check(fd) < 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIB,  "check failed at end of capfs_write()\n");
	}
#endif

	return(size);
}

#define OFFSET req.req.rw.off
#define FSIZE  req.req.rw.fsize
#define GSIZE  req.req.rw.gsize
#define LSIZE  req.req.rw.lsize
#define GCOUNT req.req.rw.gcount
#define STRIDE req.req.rw.stride

static int unix_write(int fd, char *buf, size_t count)
{
	fdesc_p fd_p = pfds[fd];
	ireq req;
	int off, ret;
	char *orig_buf = buf;

	if (!fd_p || !fd_p->part_p) return write(fd, buf, count);

	build_rw_req(fd_p, count, &req, J_WRITE, 0);

	off = OFFSET;

	if (FSIZE) {
fsize_lseek_restart:
		if (lseek(fd, off, SEEK_SET) < 0) {
			if (errno == EINTR) goto fsize_lseek_restart;
			return(-1);
		}
fsize_write_restart:
		if ((ret = write(fd, buf, FSIZE)) < 0) {
			if (errno == EINTR) goto fsize_write_restart;
			return(-1);
		}
		buf += ret;
		if (ret < FSIZE) /* this is all we're writing */ {
			fd_p->fd.off += buf-orig_buf;
			return(buf-orig_buf);
		}
		off += FSIZE + STRIDE - GSIZE;
	}
	while (GCOUNT-- > 0) {
gcount_lseek_restart:
		if (lseek(fd, off, SEEK_SET) < 0) {
			if (errno == EINTR) goto gcount_lseek_restart;
			return(-1);
		}
gcount_write_restart:
		if ((ret = write(fd, buf, GSIZE)) < 0) {
			if (errno == EINTR) goto gcount_write_restart;
			return(-1);
		}
		buf += ret;
		if (ret < GSIZE) /* this is all we're writing */ {
			fd_p->fd.off += buf-orig_buf;
			return(buf-orig_buf);
		}
		off += STRIDE;
	}
	if (LSIZE) {
lsize_lseek_restart:
		if (lseek(fd, off, SEEK_SET) < 0) {
			if (errno == EINTR) goto lsize_lseek_restart;
			return(-1);
		}
lsize_write_restart:
		if ((ret = write(fd, buf, LSIZE)) < 0) {
			if (errno == EINTR) goto lsize_write_restart;
			return(-1);
		}
		buf += ret;
	}
	fd_p->fd.off += buf-orig_buf;
	return(buf-orig_buf);
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
