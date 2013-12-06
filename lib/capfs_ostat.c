/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/* This file contains CAPFS library call for stat.   		*/
/* It will determine if file is UNIX or CAPFS and then 	*/
/* make the proper request.										*/

#include <lib.h>
#include <meta.h>
#include <errno.h>

extern int capfs_checks_disabled;

int capfs_ostat(char* pathname, struct stat *buf)
{
	return capfs_stat(pathname, buf);
}

int capfs_stat(char* pathname, struct stat *buf)
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

	if (capfs_checks_disabled) return (unix_stat(pathname, buf));

	if ((i = capfs_detect(pathname, &fn, &saddr, &fs_ino, &f_ino, FOLLOW_LINK)) < 1) {
		if (i < 0) {
			PERROR(SUBSYS_LIB,"Error finding file");
			return(-1);
		}	
		return (unix_stat(pathname, buf));
	}

	/* Prepare request for file system  */
	request.dsize = strlen(fn);
	request.uid = getuid();
	request.gid = getgid();
	request.type = MGR_STAT;

	/* Send request to mgr */
	if (send_mreq_saddr(&opt, saddr, &request, fn, &ack, NULL) < 0) {
		PERROR(SUBSYS_LIB,"capfs_stat: send_mreq_saddr -");
		return(-1);
	}
	else if (ack.status == 0) {
		COPY_PSTAT_TO_STAT(buf, &ack.ack.stat.meta.u_stat);
	}
	else {
		errno = ack.eno;
		PERROR(SUBSYS_LIB,"capfs_stat:");
	}
	return ack.status;
}

/* a capfs library user doesn't have access to config.h to know if capfs
 * was configured with --enable-lfs.  so capfs_stat64() needs to always
 * be in library, in case user defines _LARGEFILE64_SOURCE.
 */

static inline int __unix_stat64(char* p, struct stat64 *buf)
{
#if defined (__ALPHA__) || defined (__IA64__) || !defined (LARGE_FILE_SUPPORT) || !defined (HAVE_STAT64)
        int r;
        struct stat sbuf;
        r = stat(p, &sbuf);
        COPY_STAT_TO_STAT(buf, &sbuf);
        return r;
#else
        return(stat64(p, buf));
#endif
}

int capfs_ostat64(char* pathname, struct stat64 *buf)
{
	return capfs_stat64(pathname, buf);
}

int capfs_stat64(char* pathname, struct stat64 *buf)
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

	if (!pathname) {
		errno = EFAULT;
		return(-1);
	}

	if (capfs_checks_disabled) return (__unix_stat64(pathname, buf));

	if ((i = capfs_detect(pathname, &fn, &saddr, &fs_ino, &f_ino, FOLLOW_LINK)) < 1) {
		if (i < 0) {
			PERROR(SUBSYS_LIB,"Error finding file");
			return(-1);
		}	
		return (__unix_stat64(pathname, buf));
	}

	/* Prepare request for file system  */
	request.dsize = strlen(fn);
	request.uid = getuid();
	request.gid = getgid();
	request.type = MGR_STAT;

	/* Send request to mgr */
	if (send_mreq_saddr(&opt, saddr, &request, fn, &ack, NULL) < 0) {
		PERROR(SUBSYS_LIB,"capfs_stat: send_mreq_saddr -");
		return(-1);
	}
	else if (ack.status == 0) {
		COPY_PSTAT_TO_STAT(buf, &ack.ack.stat.meta.u_stat);
	}
	else {
		errno = ack.eno;
		PERROR(SUBSYS_LIB,"capfs_stat: ");
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
