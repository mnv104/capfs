/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#include <lib.h>

#include <sys/param.h>
#include <meta.h>
#include <errno.h>

extern int capfs_checks_disabled;

int capfs_rename(const char *oldname, const char *newname)
{
	int j, i;
	mreq request;
	mack ack;
	struct sockaddr *saddr;
	char *fn = NULL;
	int64_t newfs_ino, oldfs_ino;
	char bothnames[MAXPATHLEN+MAXPATHLEN+2];
	int len, bothlen = 0;
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;

	memset(&request, 0, sizeof(request));
	if (!oldname || !newname) {
		errno = EFAULT;
		return(-1);
	}

	if (capfs_checks_disabled) return rename(oldname, newname);

	/* check to see if file is a capfs file; if so, we get a pointer to a
	 * static region with the canonicalized name back in fn.  This will
	 * get overwritten on the next call.
	 */
	if ((j = capfs_detect(oldname, &fn, &saddr, &oldfs_ino, NULL, NOFOLLOW_LINK)) < 0) {
		errno = ENOENT;
		return -1;
	}
	if (fn != NULL) strncpy(bothnames, fn, MAXPATHLEN);

	if ((i = capfs_detect(newname, &fn, &saddr, &newfs_ino, NULL, FOLLOW_LINK)) < 0) {
		errno = ENOENT;
		return -1;
	}
	if (j > 0 && i > 0) {
		/* get both strings into one happy buffer */
		len = strlen(bothnames);
		bothlen = strlen(fn) + len + 1; /* don't count final terminator */
		strncpy(&bothnames[len+1], fn, MAXPATHLEN);
	}

	if (i==0 && j==0) {
		/* noncapfs stuff */
		return rename(oldname, newname);
	}
	if (i == 0 || j == 0 || (newfs_ino != oldfs_ino)) {
		/* across file systems, error */
		errno = EXDEV; /* not on same file system! */
		return(-1);
	}

	/* Prepare request for file system  */
	request.uid = getuid();
	request.gid = getgid();
	request.type = MGR_RENAME;
	request.dsize = bothlen;

	/* Send request to mgr */	
	if (send_mreq_saddr(&opt, saddr, &request, bothnames, &ack, NULL) < 0) {
		int myeno = errno;
		PERROR(SUBSYS_LIB,"capfs_rename: send_mreq_saddr -");
		errno = myeno;
		return(-1);
	}
	else if (ack.status) {
		errno = ack.eno;
		PERROR(SUBSYS_LIB,"capfs_rename:");
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
