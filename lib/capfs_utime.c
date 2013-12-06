/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */

#include <lib.h>

#include <utime.h>
#include <sys/types.h>
#include <sys/param.h>
#include <time.h>
#include <errno.h>

extern int capfs_checks_disabled;

int capfs_utime(const char *filename, struct utimbuf *buf)
{
	mreq request;
	mack ack;
	struct sockaddr *saddr;
	char *fn;
	int64_t fs_ino, f_ino;
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;

	memset(&request, 0, sizeof(request));
	/* check if this file is capfs */
	if (capfs_checks_disabled || capfs_detect(filename, &fn, &saddr, &fs_ino, &f_ino, FOLLOW_LINK) < 1)
	{
		return utime(filename, buf);
	}

	/* prepare request for manager */
   request.uid = getuid();
   request.gid = getgid();
   request.type = MGR_UTIME;
	request.dsize = strlen(fn);

	/* if buf is null, set to current time */
	if (buf) {
		request.req.utime.actime = buf->actime; /* access time */
		request.req.utime.modtime = buf->modtime; /* modification time */
	}
	else {
		request.req.utime.actime = request.req.utime.modtime
		  = time(NULL);
	}
		
   /* Send request to mgr */
	if (send_mreq_saddr(&opt, saddr, &request, fn, &ack, NULL) < 0) {
      PERROR(SUBSYS_LIB,"capfs_utime: send_mreq_saddr -");
      return(-1);
   }
	else if (ack.status) {
		errno = ack.eno;
		PERROR(SUBSYS_LIB,"capfs_utime:");
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
