/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/* This file contains CAPFS library call for close.   		*/
/* It will determine if file is UNIX or CAPFS and then 	*/
/* make the proper request.										*/
/* Determines if capfs file and take file off 				*/
/* open list; also, close meta file								*/
#include <capfs-header.h>
#include <lib.h>
#include <errno.h>
#include <log.h>

extern fdesc_p pfds[];
extern sockset socks;
extern jlist_p active_p;
extern int capfs_mode;

int capfs_close(int fd)
{
	int i, status=0, myeno=0;
	struct sockaddr saddr;
	mreq req;
	mack ack;
	ireq iodreq;
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;

	memset(&req, 0, sizeof(req));
	memset(&iodreq, 0, sizeof(ireq));
	/* don't need to init ack as it gets filled in by response from server */
	if (fd < 0 || fd >= CAPFS_NR_OPEN 
	    || (pfds[fd] && pfds[fd]->fs == FS_RESV)) 
	{
		errno = EBADF;
		return(-1);
	}

	/* If we don't know anything about the FD, use the system call */
	if (!pfds[fd]) return close(fd);

	/* Two cases here:
	 * - CAPFS file with more references (first case)
	 * - UNIX file
	 * 
	 * NEW CASE:
	 * - CAPFS directory
	 *
	 * In all cases, we want to make sure to eliminate any data
	 * structures we held for the file.  Then we want to close the FD.
	 * For CAPFS files, this will close the meta file, which will free the
	 * FD slot for later use.  For UNIX files, this will close the actual
	 * file, although there may be more open FD references to the same
	 * file.
	 */
	if ((pfds[fd] && --pfds[fd]->fd.ref) || pfds[fd]->fs == FS_UNIX
	|| pfds[fd]->fs == FS_PDIR)
	{
		if (pfds[fd] && !pfds[fd]->fd.ref) {
			if (pfds[fd]->part_p) free(pfds[fd]->part_p);
			if (pfds[fd]->fn_p)  free(pfds[fd]->fn_p);
			free(pfds[fd]);
		}
		pfds[fd] = NULL;
		return close(fd);
	}

#ifdef STRICT_FDESC_CHECK
	if (do_fdesc_check(fd) < 0) {
		errno = EBADF;
		return(-1);
	}
#endif
	if (capfs_mode == 0) 
	{
		/* Now need to send close directly to iods */
		iodreq.majik_nr         = IOD_MAJIK_NR;
		iodreq.release_nr       = CAPFS_RELEASE_NR;
		iodreq.type 		= IOD_CLOSE;
		iodreq.dsize		= 0;
		iodreq.req.close.f_ino	= pfds[fd]->fd.meta.u_stat.st_ino;
		iodreq.req.close.cap	= pfds[fd]->fd.cap;

		/* build job to send reqs and recv acks to/from iods */
		if (!active_p) active_p = jlist_new();
		initset(&socks); /* clear out the socket set */

		if (build_simple_jobs(pfds[fd], &iodreq) < 0) {
			LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " capfs_close: build_simple_jobs failed\n");
			goto capfs_close_try_manager;
		}

		/* call do_job */
		while (!jlist_empty(active_p)) {
			if (do_jobs(active_p, &socks, -1) < 0) {
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " capfs_close: do_jobs failed\n");
				goto capfs_close_try_manager;
			}
		}

		/* look for errors from iods */
		for (i=0; i < pfds[fd]->fd.meta.p_stat.pcount; i++) {
			if (pfds[fd]->fd.iod[i].ack.status) {
				myeno = pfds[fd]->fd.iod[i].ack.eno;
				status = -1;
			}
		}
	}

capfs_close_try_manager:
	/* Prepare request for manager  */
	req.uid 		= getuid();
	req.gid 		= getgid();
	req.type 	= MGR_CLOSE;
	req.dsize 	= 0;
	req.req.close.meta = pfds[fd]->fd.meta;
	if (!(pfds[fd]->fd.flag & O_GADD)) {
		/* Need to connect to mgr, not ready yet */
		saddr = pfds[fd]->fd.meta.mgr;
		if (send_mreq_saddr(&opt, &saddr, &req, NULL, &ack, NULL) < 0)
		{
			myeno = errno;
			status = -1;
			PERROR(SUBSYS_LIB,"capfs_close: send_mreq_saddr failed\n");
		}
		else {
			myeno = ack.eno;
			status = ack.status;
		}
	}
#ifdef STRICT_FDESC_CHECK
	if (do_fdesc_check(fd) < 0) {
		errno = EBADF;
		return(-1);
	}
#endif

	/* dont close any existing sockets. but we may want the option to close themif need be */
	for (i = 0; i < pfds[fd]->fd.meta.p_stat.pcount; i++) {
		if (pfds[fd]->fd.iod[i].sock >= 0) {
			dec_ref_count(pfds[fd]->fd.iod[i].sock);
		}	
	}	

	if (pfds[fd]->part_p) free(pfds[fd]->part_p);
	if (pfds[fd]->fn_p)  free(pfds[fd]->fn_p);
	free(pfds[fd]);
	pfds[fd] = NULL;
	close(fd); /* close the meta file */

	errno = myeno;
	return(status);
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
