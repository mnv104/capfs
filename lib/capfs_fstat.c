/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/* This file contains CAPFS library call for fstat.   		*/
/* It will determine if file is UNIX or CAPFS and then 	*/
/* make the proper request.										*/

#include <lib.h>
#include <errno.h>

extern fdesc_p pfds[];
extern int capfs_mode;

int capfs_fstat(int fd, struct stat *buf)
{
	struct sockaddr saddr;
	mack ack;
	mreq req;
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;

	memset(&req, 0, sizeof(req));
	if (fd < 0 || fd >= CAPFS_NR_OPEN 
	    || (pfds[fd] && pfds[fd]->fs == FS_RESV)) 
	{
		errno = EBADF;
		return(-1);
	}  

	if (!pfds[fd] || pfds[fd]->fs == FS_UNIX) {
		return(unix_fstat(fd, buf));
	}	

	if (pfds[fd]->fs == FS_PDIR) {
		/* CAPFS directory, send a stat request */
		req.uid = getuid();
		req.gid = getgid();
		req.type = MGR_STAT;
		req.dsize = strlen(pfds[fd]->fn_p);

		saddr = pfds[fd]->fd.meta.mgr;
		if (send_mreq_saddr(&opt, &saddr, &req, pfds[fd]->fn_p, &ack, NULL) < 0) {
			PERROR(SUBSYS_LIB,"capfs_fstat: send_mreq_saddr -");
			return(-1);
		}
		if (ack.status == 0) {
			COPY_PSTAT_TO_STAT(buf, &ack.ack.stat.meta.u_stat);
		}
		else {
			errno = ack.eno;
			PERROR(SUBSYS_LIB,"capfs_fstat: ");
			return ack.status;
		}
	}
	else {
		/* otherwise it is a CAPFS file -- prepare request */
		req.uid  = getuid();
		req.gid  = getgid();
		req.type = MGR_FSTAT;
		req.dsize= 0;
		req.req.fstat.meta = pfds[fd]->fd.meta;
		/* send request to mgr */
		saddr = pfds[fd]->fd.meta.mgr;
		if (send_mreq_saddr(&opt, &saddr, &req, NULL, &ack, NULL) < 0) {
			PERROR(SUBSYS_LIB,"capfs_fstat: send_mreq_saddr -");
			return(-1);
		}
		if (ack.status == 0) {
			COPY_PSTAT_TO_STAT(buf, &ack.ack.stat.meta.u_stat);
		}
		else {
			errno = ack.eno;
			PERROR(SUBSYS_LIB,"capfs_fstat: ");
			return ack.status;
		}
	}
	return (0);
}

/* a capfs library user doesn't have access to config.h to know if capfs
 * was configured with --enable-lfs.  so capfs_fstat64() needs to always
 * be in library, in case user defines _LARGEFILE64_SOURCE.
 */

int capfs_fstat64(int fd, struct stat64 *buf)
{
	struct sockaddr saddr;
	mack ack;
	mreq req;
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;

	if (fd < 0 || fd >= CAPFS_NR_OPEN 
	    || (pfds[fd] && pfds[fd]->fs == FS_RESV)) 
	{
		errno = EBADF;
		return(-1);
	}  

	if (!pfds[fd] || pfds[fd]->fs == FS_UNIX) {
#if defined (__ALPHA__) || defined (__IA64__) || !defined (LARGE_FILE_SUPPORT) || !defined (HAVE_FSTAT64)
		int r;
		struct stat sbuf;
		r = fstat(fd, &sbuf);
		COPY_STAT_TO_STAT(buf, &sbuf);
		return r;
#else
		return(fstat64(fd, buf));
#endif
	}	

	if (pfds[fd]->fs == FS_PDIR) {
		/* CAPFS directory, send a stat request */
		req.uid = getuid();
		req.gid = getgid();
		req.type = MGR_STAT;
		req.dsize = strlen(pfds[fd]->fn_p);

		saddr = pfds[fd]->fd.meta.mgr;
		if (send_mreq_saddr(&opt, &saddr, &req, pfds[fd]->fn_p, &ack, NULL) < 0) {
			PERROR(SUBSYS_LIB,"capfs_fstat64: send_mreq_saddr -");
			return(-1);
		}
		if (ack.status == 0) {
			COPY_PSTAT_TO_STAT(buf, &ack.ack.stat.meta.u_stat);
		}
		else {
			errno = ack.eno;
			PERROR(SUBSYS_LIB,"capfs_fstat64: ");
			return ack.status;
		}
	}
	else {
		/* otherwise it is a CAPFS file -- prepare request */
		req.uid  = getuid();
		req.gid  = getgid();
		req.type = MGR_FSTAT;
		req.dsize= 0;
		req.req.fstat.meta = pfds[fd]->fd.meta;

		/* send request to mgr */
		saddr = pfds[fd]->fd.meta.mgr;
		if (send_mreq_saddr(&opt, &saddr, &req, NULL, &ack, NULL) < 0) {
			PERROR(SUBSYS_LIB,"capfs_fstat64: send_mreq_saddr -");
			return(-1);
		}
		if (ack.status == 0) {
			COPY_PSTAT_TO_STAT(buf, &ack.ack.stat.meta.u_stat);
		}
		else {
			errno = ack.eno;
			PERROR(SUBSYS_LIB,"capfs_fstat64: ");
			return ack.status;
		}
	}
	return (0);
}
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 *
 * vim: ts=3
 */

