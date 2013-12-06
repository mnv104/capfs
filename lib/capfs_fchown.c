/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/* This file contains CAPFS library call for chown.   		*/
/* It will determine if file is UNIX or CAPFS and then 	*/
/* make the proper request.										*/

#include <lib.h>
#include <meta.h>
#include <errno.h>

extern fdesc_p pfds[];
extern int capfs_mode;

int capfs_fchown(int fd, uid_t owner, gid_t group)
{
	struct sockaddr saddr;
	mreq request;
	mack ack;
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;

	memset(&request, 0, sizeof(request));
	/* check for badness */
	if (fd < 0 || fd >= CAPFS_NR_OPEN 
	    || (pfds[fd] && pfds[fd]->fs == FS_RESV)) {
		errno = EBADF;
		return(-1);
	}  

	/* check for UNIX file */
	if (!pfds[fd] || pfds[fd]->fs == FS_UNIX) {
		return fchown(fd, owner, group);
	}

	/* CAPFS file or directory -- Prepare request for file system  */
	request.uid = getuid();
	request.gid = getgid();
	request.type = MGR_FCHOWN;
	request.dsize = 0;
	request.req.fchown.owner = (int)owner;
	request.req.fchown.group = (int)group;
	request.req.fchown.file_ino = pfds[fd]->fd.meta.u_stat.st_ino;
	request.req.fchown.fs_ino = pfds[fd]->fd.meta.fs_ino;

	/* Send request to mgr */	
	saddr = pfds[fd]->fd.meta.mgr;
	if (send_mreq_saddr(&opt, &saddr, &request, NULL, &ack, NULL) < 0) {
		PERROR(SUBSYS_LIB,"send_mreq_saddr error: ");
		return(-1);
	}
	else if (ack.status) {
		errno = ack.eno;
		PERROR(SUBSYS_LIB,"ack.status error: ");
	}
	return ack.status;
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
