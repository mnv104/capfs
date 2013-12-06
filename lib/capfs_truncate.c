/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#include <lib.h>
#include <meta.h>
#include <errno.h>

extern int capfs_checks_disabled;

int capfs_truncate(const char *path, size_t length)
{
	int i;
	mreq request;
	mack ack;
	struct sockaddr *saddr;
	char *fn;
	int64_t fs_ino, f_ino;
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;

	memset(&request, 0, sizeof(request));
	if (!path) {
		errno = EFAULT;
		return(-1);
	}

	if (capfs_checks_disabled) return truncate(path, length);

	/* check to see if file is a capfs file */
	if ((i = capfs_detect(path, &fn, &saddr, &fs_ino, &f_ino, FOLLOW_LINK)) != 1) {
		if (i < 0) {
			PERROR(SUBSYS_LIB,"Error finding file");
			return(-1);
		}	
		if (i == 2) {
			errno = EISDIR;
			return(-1);
		}
		return truncate(path, length);
	}

	/* Prepare request for file system  */
	request.uid = getuid();
	request.gid = getgid();
	request.type = MGR_TRUNCATE;
	request.dsize = strlen(fn);
	request.req.truncate.length = length;

	/* Send request to mgr */	
	if (send_mreq_saddr(&opt, saddr, &request, fn, &ack, NULL) < 0) {
		PERROR(SUBSYS_LIB,"capfs_truncate: send_mreq_saddr -");
		return(-1);
	}
	else if (ack.status) {
		errno = ack.eno;
		PERROR(SUBSYS_LIB,"capfs_truncate:");
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
