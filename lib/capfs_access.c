/*
 * (C) 2005  Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */



/* This file contains CAPFS library call for access.   		*/
/* It will determine if file is UNIX or CAPFS and then 		*/
/* make the proper request.					*/

#include <lib.h>
#include <meta.h>
#include <errno.h>

extern int capfs_checks_disabled;
extern int capfs_mode;

int unix_access(char *path, int mode);

/* access by POSIX standards follows a symbolic link */
int capfs_access(char* pathname, int mode)
{
	int i=0;
	mack ack;
	mreq request;
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

	if (capfs_checks_disabled ||
		(i = capfs_detect(pathname, &fn, &saddr, &fs_ino, NULL, FOLLOW_LINK)) < 1) {
		if (i < 0) {
			PERROR(SUBSYS_LIB,"capfs_access: Error finding file");
			return(-1);
		}	
		return access(pathname, mode);
	}

	/* Prepare request for file system  */
	request.dsize = strlen(fn);
	request.uid = getuid();
	request.gid = getgid();
	request.type = MGR_ACCESS;
	request.req.access.mode = mode;
	request.req.access.to_follow = FOLLOW_LINK;

	/* Send request to mgr */
	if (send_mreq_saddr(&opt, saddr, &request, fn, &ack, NULL) < 0) {
		PERROR(SUBSYS_LIB,"capfs_access: send_mreq_saddr - ");
		return(-1);
	}
	else if (ack.status) {
		errno = ack.eno;
		PERROR(SUBSYS_LIB,"capfs_access: ");
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
