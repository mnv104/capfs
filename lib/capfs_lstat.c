/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/* This file contains CAPFS library call for lstat.   		*/
/* It will determine if file is UNIX or CAPFS and then 	*/
/* make the proper request.										*/

#include <lib.h>
#include <meta.h>
#include <sys/stat.h>
#include <errno.h>

extern int capfs_checks_disabled;

int capfs_lstat(char* pathname, struct stat *buf)
{
	int i;
	mack ack;
	mreq request;
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

	/* check if CAPFS file */
	if(!capfs_checks_disabled){
		if ((i = capfs_detect(pathname, &fn, &saddr, &fs_ino, &f_ino, NOFOLLOW_LINK)) < 1) {
			if (i < 0) {
				PERROR(SUBSYS_LIB,"Error finding file");
				return(-1);
			}	
			return unix_lstat(pathname, buf);
		}
	}

	/* Prepare request for file system  */
	request.dsize = strlen(fn);
	request.uid = getuid();
	request.gid = getgid();
	request.type = MGR_LSTAT;

	/* Send request to mgr */
	if (send_mreq_saddr(&opt, saddr, &request, fn, &ack, NULL) < 0) {
		PERROR(SUBSYS_LIB,"capfs_lstat: send_mreq_saddr -");
		return(-1);
	}
	else if (ack.status) {
		errno = ack.eno;
		PERROR(SUBSYS_LIB,"capfs_lstat:");
	}
	else {
		COPY_PSTAT_TO_STAT(buf, &ack.ack.stat.meta.u_stat);
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
