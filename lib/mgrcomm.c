/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */

#include <capfs-header.h>
#include <lib.h>
#include <stdio.h>
#include <errno.h>
#include <log.h>
#include <capfs_config.h>
#include "mgr.h"

int send_mreq_saddr(struct capfs_options* opt, struct sockaddr *saddr_p, mreq_p req_p,
                    void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	if (!req_p || !ack_p || !saddr_p) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIB,  "Invalid parameter\n");
		errno = EINVAL;
		return(-1);
	}
	req_p->majik_nr = MGR_MAJIK_NR;
	req_p->release_nr = CAPFS_RELEASE_NR;
	if (req_p->dsize > 0 && data_p == NULL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIB,  "Trailing data cannot be NULL\n");
		errno = EINVAL;
		return -1;
	}
	/* talk to the MGR thru the RPC interface. errno is set internally */
	if (encode_compat_req(opt, saddr_p, req_p, ack_p, data_p, recv_p) < 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIB,  "encode_compat_req failed: %s\n", strerror(errno));
		return -1;
	}

	if (ack_p->majik_nr != MGR_MAJIK_NR
			|| ack_p->release_nr != CAPFS_RELEASE_NR) /* serious error -- nonsense ack */ {
		errno = EIO;
		return(-1);
	}
	if (ack_p->status) /* status error */ {
		errno = ack_p->eno;
		return -1;
	}

	return 0;
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
