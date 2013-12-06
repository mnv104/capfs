/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */

#include <capfs-header.h>
#include <lib.h>
#include <meta.h>
#include <errno.h>

extern fdesc_p pfds[];
extern sockset socks;
extern jlist_p active_p;
extern int capfs_mode;

#define PCNT pfds[fd]->fd.meta.p_stat.pcount
#define FINO pfds[fd]->fd.meta.u_stat.st_ino

int capfs_ftruncate(int fd, size_t length)
{
	int i;
	ireq req;

	memset(&req, 0, sizeof(req));
	/* check for badness */
	if (fd < 0 || fd >= CAPFS_NR_OPEN 
		 || (pfds[fd] && pfds[fd]->fs == FS_RESV)) 
	{
		errno = EBADF;
		return(-1);
	}  

	/* check for UNIX */
	if (!pfds[fd] || pfds[fd]->fs==FS_UNIX) {
		return(ftruncate(fd, length));
	}

	if (pfds[fd]->fs == FS_PDIR) {
		errno = EISDIR;
		return(-1);
	}

	if (capfs_mode == 0)
	{
		req.majik_nr             = IOD_MAJIK_NR;
		req.release_nr           = CAPFS_RELEASE_NR;
		req.type                 = IOD_FTRUNCATE;
		req.dsize                = 0;
		req.req.ftruncate.f_ino  = FINO;
		req.req.ftruncate.cap    = pfds[fd]->fd.cap;
		req.req.ftruncate.length = length;

		/* build job to send reqs and recv acks */
		if (build_simple_jobs(pfds[fd], &req) < 0) {
			LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " capfs_ftruncate: build_simple_jobs failed\n");
			return(-1);
		}

		/* call do_job */
		while (!jlist_empty(active_p)) {
			if (do_jobs(active_p, &socks, -1) < 0) {
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " capfs_ftruncate: do_jobs failed\n");
				return(-1);
			}
		}

		/* check acks from iods */
		for (i=0; i < PCNT; i++) {
			if (pfds[fd]->fd.iod[i].ack.status) {
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " capfs_ftruncate: non-zero status returned from iod %d\n", i);
				errno = pfds[fd]->fd.iod[i].ack.eno;
				return(-1);
			}
		}
	}
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
