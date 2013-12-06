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

#include <string.h>
#include <meta.h>
#include <resv_name.h>
#include <errno.h>

extern int capfs_checks_disabled;

int capfs_unlink(const char* pathname)
{
	int i=0 /* important that this is 0 */;
	mreq request;
	mack ack;
	struct sockaddr *saddr;
	char *fn;
	int64_t fs_ino, f_ino;
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;

	memset(&request, 0, sizeof(request));
	if (!pathname) {
		errno = EFAULT;
		return(-1);
	}
	
	/* Check to see if file is a CAPFS file */
	if (capfs_checks_disabled
	|| (i = capfs_detect(pathname, &fn, &saddr, &fs_ino, &f_ino, NOFOLLOW_LINK)) != 1)
	{
		if (i == 2) /* CAPFS directory */ {
			errno = EISDIR;
			return(-1);
		}
		/* check for .capfsdir and .iodtab, reported back as UNIX files */
		if (resv_name(pathname) != 0) {
			/* pretend everything is ok if we try to remove a reserved
			 * name file, but don't really do anything.  This is here to
			 * make recursive removes work without error.
			 */
			return 0;
		}

		/* otherwise let UNIX handle giving back an error */
		return unlink(pathname);
	}
	/* Prepare request for file system  */
	request.dsize = strlen(fn);
	request.uid = getuid();
	request.gid = getgid();
	request.type = MGR_UNLINK;

	/* Send request to mgr */	
	if (send_mreq_saddr(&opt, saddr, &request, fn, &ack, NULL) < 0) {
		PERROR(SUBSYS_LIB,"capfs_unlink: send_mreq_saddr -");
		return(-1);
	}
	else if (ack.status) {
		errno = ack.eno;
		PERROR(SUBSYS_LIB,"capfs_unlink:");
	}
	return ack.status;
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
