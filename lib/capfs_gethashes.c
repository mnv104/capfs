/*
 * (C) 2005 Penn State University
 *
 * See LIBRARY_COPYING in top-level directory.
 */
#include <capfs-header.h>
#include <lib.h>
#include <signal.h>
#include <sockio.h>
#include <sockset.h>
#include <stdarg.h>
#include <errno.h>
#include <log.h>
#include "mgr.h"

/* contains routines for getting hashes of a particular CAPFS file */

int capfs_gethashes(const char *path, unsigned char *phashes, int64_t begin_offset, int max_hashes)
{
	int i;
	mreq 		req;
	mack ack;
	struct sockaddr *saddr = NULL;
	char *fn = NULL;
	int64_t fs_ino;
	struct ackdata_c ackdata;
	struct capfs_options opt;

	/* gethashes needs to use TCP */
	opt.tcp = 1; 
	opt.use_hcache = 0;
	/* Check to see if file is a CAPFS file */
	i = capfs_detect2(path, &fn, &saddr, &fs_ino, NULL, FOLLOW_LINK);
	if (i < 0 || i > 2) /* error */ {
		PERROR(SUBSYS_LIB,"Error finding file");
		return(-1);
	}
	else if (i == 0 || i == 2) {
		PERROR(SUBSYS_LIB,"File/directory does not support gethashes");
		return -1;
	}
	req.uid = getuid();
	req.gid = getgid();
	req.type = MGR_GETHASHES;

	req.dsize = strlen(fn);
	req.req.gethashes.begin_chunk = begin_offset;
	req.req.gethashes.nchunks = max_hashes;
	ackdata.type = MGR_GETHASHES;
	ackdata.u.gethashes.nhashes = 0;
	ackdata.u.gethashes.buf = phashes;
	/* send request and receive ack */
	memset(&ack, 0, sizeof(ack));
	if (send_mreq_saddr(&opt, saddr, &req, fn, &ack, &ackdata) < 0) {
		return -1;
	}
	if (ack.status != 0) {
		errno = ack.eno;
		return -1;
	}
	return ackdata.u.gethashes.nhashes;
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




