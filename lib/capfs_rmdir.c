/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/* This file contains CAPFS library call for unlink.   	*/
/* It will determine if file is UNIX or CAPFS and then 	*/
/* make the proper request.										*/

#include <lib.h>
#include <errno.h>

extern int capfs_checks_disabled;

int capfs_rmdir(const char* pathname)
{
	int i;
	mreq request;
	mack ack;
	struct sockaddr *saddr;
	char *fn;
	int64_t fs_ino;
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;

	memset(&request, 0, sizeof(request));
	if (!pathname) {
		errno = EFAULT;
		return(-1);
	}
	
	if (capfs_checks_disabled) return rmdir(pathname);

	/* Check to see if .capfsdir exists in directory  	*/
	if ((i = capfs_detect(pathname, &fn, &saddr, &fs_ino, NULL, FOLLOW_LINK)) < 1) {
		if (i < 0) {
			PERROR(SUBSYS_LIB,"capfs_rmdir: finding dotfile");
			return(-1);
		}	
		return rmdir(pathname);
	}
	if (i == 1) {
		errno = ENOTDIR;
		return(-1);
	}

	/* Prepare request for file system  */
	request.dsize = strlen(fn);
	request.uid = getuid();
	request.gid = getgid();
	request.type = MGR_RMDIR;

	/* Send request to mgr */	
	if (send_mreq_saddr(&opt, saddr, &request, fn, &ack, NULL) < 0) {
		PERROR(SUBSYS_LIB,"capfs_rmdir: send_mreq_saddr - ");
		return(-1);
	}
	else if (ack.status) {
		errno = ack.eno;
		PERROR(SUBSYS_LIB,"capfs_rmdir:");
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
