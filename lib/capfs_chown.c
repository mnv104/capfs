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

extern int capfs_checks_disabled;
extern int capfs_mode;

int capfs_chown(const char* pathname, uid_t owner, gid_t group)
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
	if (capfs_checks_disabled) {
		return chown(pathname, owner, group);
	}

	/* Check to see if .capfsdir exists in directory  	*/
	if ((i = capfs_detect(pathname, &fn, &saddr, &fs_ino, NULL, FOLLOW_LINK)) < 1) {
		if (i < 0) {
			PERROR(SUBSYS_LIB,"capfs_chown: finding file");
			return(-1);
		}	
		return chown(pathname, owner, group);
	}

	/* Prepare request for file system  */
	/* here's this hack again! */
	request.dsize = strlen(fn);
	request.uid = getuid();
	request.gid = getgid();
	request.type = MGR_CHOWN;
	request.req.chown.owner = (int)owner;
	request.req.chown.group = (int)group;

	/* Send request to mgr */	
	if (send_mreq_saddr(&opt, saddr, &request, fn, &ack, NULL) < 0) {
		PERROR(SUBSYS_LIB,"capfs_chown: send_mreq_saddr - ");
		return(-1);
	}
	else if (ack.status) {
		errno = ack.eno;
		PERROR(SUBSYS_LIB,"capfs_chown:");
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
