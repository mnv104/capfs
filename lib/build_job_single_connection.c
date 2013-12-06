/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */
#include <capfs-header.h>
#include <lib.h>

/* UNIX INCLUDES */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>

/* CAPFS INCLUDES */
#include <build_job.h>
#include <desc.h>
#include <req.h>
#include <jlist.h>
#include <alist.h>
#include <minmax.h>
#include <sockset.h>
#include <sockio.h>
#include <dfd_set.h>
#include <log.h>

/* GLOBALS */
jlist_p active_p = NULL;
sockset socks;

/* DEFINES */
#define REQTYPE  r_p->req.rw.rw_type
#define OFFSET r_p->req.rw.off
#define FSIZE  r_p->req.rw.fsize
#define GSIZE  r_p->req.rw.gsize
#define LSIZE  r_p->req.rw.lsize
#define GCOUNT r_p->req.rw.gcount
#define STRIDE r_p->req.rw.stride

#define PCOUNT ((int64_t)(f_p->fd.meta.p_stat.pcount))

/* FUNCTIONS */

/* BUILD_SIMPLE_JOBS() - builds a set of jobs to send a request
 * to the iods for a file and receive the acks into the iod ack fields
 * for the file
 *
 * Returns 0 on success, -1 on failure
 */
int build_simple_jobs(fdesc_p f_p, ireq_p r_p)
{
	int i;

	if (!active_p) active_p = jlist_new();

	initset(&socks); /* clear out socket set */

	for (i=0; i < PCOUNT; i++) {
		jinfo_p j_p;
		ainfo_p a_p;
		ireq_p  tmpr_p;
		int s;

		f_p->fd.iod[i].ack.status = 0;
		f_p->fd.iod[i].ack.dsize  = 0;
		/* We try to connect to the iod servers inside */
		s = add_iodtable((struct sockaddr *)&f_p->fd.iod[i].addr);
		if (s < 0) { /* error connecting to iod */
			return s;
		}
		/* "sock" member field is now an index into the iod-table! Not the actual socket fd */
		f_p->fd.iod[i].sock = find_iodtable((struct sockaddr *)&f_p->fd.iod[i].addr);
		/* don't check returns; we don't care if this fails */
		if ( get_sockopt(s, SO_SNDBUF) != CLIENT_SOCKET_BUFFER_SIZE) {
			LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "forcing send socket buffer to %d\n", CLIENT_SOCKET_BUFFER_SIZE); 
		}
		if ( get_sockopt(s, SO_RCVBUF) != CLIENT_SOCKET_BUFFER_SIZE) {
			LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "forcing recieve socket buffer to %d\n", CLIENT_SOCKET_BUFFER_SIZE);
		}
		(void) set_sockopt(s, SO_SNDBUF, CLIENT_SOCKET_BUFFER_SIZE);
		(void) set_sockopt(s, SO_RCVBUF, CLIENT_SOCKET_BUFFER_SIZE);
#ifdef CAPFS_USE_NODELAY
		(void) set_tcpopt(s, TCP_NODELAY, 1);
#endif
		if (!(j_p = j_new(PCOUNT))) {
			return(-1);
		}
		dfd_set(s, &j_p->socks);
		if (s >= j_p->max_nr) j_p->max_nr = s+1;

		/* add request */
		if (!(a_p = (ainfo_p) malloc(sizeof(ainfo)))) {
			j_free(j_p);
			return(-1);
		}
		if (!(tmpr_p = (ireq_p) malloc(sizeof(ireq)))) {
			free(a_p); /* don't use a_free; not initialized here */
			j_free(j_p);
			return(-1);
		}
		/* no need to initialize tmpr_p if it's getting *r_p */
		*tmpr_p = *r_p;
		a_p->type = A_REQ;
		a_p->sock = f_p->fd.iod[i].sock;/* this is also an index to the iod table */
		a_p->u.req.size = sizeof(ireq);
		a_p->u.req.cur_p = a_p->u.req.req_p = (char *) tmpr_p;
		if (a_add_start(j_p->al_p, a_p) < 0) {
			free(a_p);
			free(tmpr_p);
			j_free(j_p);
			return(-1);
		}

		/* add ack */
		if (!(a_p = (ainfo_p) malloc(sizeof(ainfo)))) {
			j_free(j_p);
			return(-1);
		}
		a_p->type = A_ACK;
		a_p->sock = f_p->fd.iod[i].sock;/* this is an index to the iod table */
		a_p->u.ack.iod_nr = i;
		a_p->u.ack.size = sizeof(iack);
		a_p->u.ack.cur_p = a_p->u.ack.ack_p = (char *) &f_p->fd.iod[i].ack;
		if (a_add_end(j_p->al_p, a_p) < 0) {
			free(a_p);
			j_free(j_p);
			return(-1);
		}

		/* add job to list */
		if (j_add(active_p, j_p) < 0) /* error adding job to list */ {
			j_free(j_p);
			return(-1);
		}
		addsockwr(s, &socks);
	}
	return(0);
}

/* BUILD_RW_JOBS() - construct jobs to fulfill an I/O request
 * PARAMETERS:
 *    f_p   - pointer to file descriptor
 *    buf_p - pointer to buffer to be sent or to receive into
 *    size  - size of request (in bytes)
 *    type  - type of jlist request (J_READ or J_WRITE)
 *
 * Returns -1 on error, 0 on success?
 */
int build_rw_jobs(fdesc_p f_p, char *buf_p, int64_t size, int type)
{
	ireq    req;
	ireq_p  r_p = &req; /* nice way to use the same defines throughout */
	ainfo_p a_p;
	jinfo_p j_p;
	int i, atype, sock, myerr;
	int64_t offset, gcount;

	memset(&req, 0, sizeof(req));
	/* build the requests */
	if (!active_p) active_p = jlist_new();
	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "build_jobs: a = %lx, s = %Ld, t = %d\n", (long unsigned) buf_p, size, type);

	build_rw_req(f_p, size, &req, type, 0); /* last # not currently used */
	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "  build_jobs: done building request\n");

	/* sooner or later this will matter... */
	switch (type) {
		case J_READ:
			atype = A_READ; 
			break;
		case J_WRITE:
		default:
			atype = A_WRITE;
			break;
	}

	/* add the accesses */
	offset = r_p->req.rw.off;
	gcount = r_p->req.rw.gcount;
	if (FSIZE) {
		if ((buf_p = add_accesses(f_p, offset, FSIZE, buf_p, atype)) == NULL) {
			return -1;
		}
		offset += FSIZE + (STRIDE - GSIZE);
	}
	while (gcount-- > 0) {
		if ((buf_p = add_accesses(f_p, offset, GSIZE, buf_p, atype)) == NULL) {
			return -1;
		}
		offset += STRIDE;
	}
	if (LSIZE) {
		if ((buf_p = add_accesses(f_p, offset, LSIZE, buf_p, atype)) == NULL) {
			return -1;
		}
	}

	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "  build_jobs: done calling add_accesses\n");

	initset(&socks); /* clear any other jobs out */
	for (i = 0; i < PCOUNT; i++) {
		int slot;
		/* we know the slot into the iod-table */
		slot = f_p->fd.iod[i].sock;
		sock = getfd_iodtable(slot);
		if (sock >= 0 && (j_p = j_search(active_p, sock)) != NULL) {
			/* add reception of ack */
			if ((a_p = (ainfo_p) malloc(sizeof(ainfo))) == NULL) {
				PERROR(SUBSYS_LIB,"build_jobs: malloc (ainfo1)");
				return(-1);
			}
			a_p->type = A_ACK;
			a_p->sock = slot; /* we store the index into the iod table as sock field member */
			a_p->u.ack.size = sizeof(iack);
			a_p->u.ack.ack_p = a_p->u.ack.cur_p =
				(char *)&(f_p->fd.iod[i].ack);
			a_p->u.ack.iod_nr = i;

			switch(type) {
				case J_READ:
					if (a_add_start(j_p->al_p, a_p) < 0) {
						myerr = errno;
						LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " build_jobs: a_add_start failed (ack)\n");
						errno = myerr;
						return(-1);
					}
					break;
				case J_WRITE:
				default:
					if (a_add_end(j_p->al_p, a_p) < 0) {
						myerr = errno;
						LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " build_jobs: a_add_end failed\n");
						errno = myerr;
						return(-1);
					}
					break;
			}
			LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "  build_jobs: added ack recv for iod %d\n", i);
			
			/* copy request */
			if ((r_p = (ireq_p) malloc(sizeof(ireq))) == NULL) {
				myerr = errno;
				PERROR(SUBSYS_LIB,"build_jobs: malloc (ireq)");
				errno = myerr;
				return(-1);
			}
			*r_p = req;

			/* add send of request to start of job */
			if ((a_p = (ainfo_p) malloc(sizeof(ainfo))) == NULL) {
				myerr = errno;
				PERROR(SUBSYS_LIB,"build_jobs: malloc (ainfo2)");
				errno = myerr;
				return(-1);
			}
			a_p->type = A_REQ;
			a_p->sock = slot; /* index into iodinfo table */
			a_p->u.req.size = sizeof(ireq);
			a_p->u.req.cur_p = a_p->u.req.req_p = (char *) r_p;
			if (a_add_start(j_p->al_p, a_p) < 0) {
				myerr = errno;
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " build_jobs: a_add_start failed (req)\n");
				errno = myerr;
				return(-1);
			}
			addsockwr(sock, &socks);
			LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "  build_jobs: added req send for iod %d\n", i);
		}
		/* else there's no job for this socket */
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "build_jobs: done.\n");
	return(0);
}

#undef PCOUNT

#define SSIZE ((int64_t) (f_p->fd.meta.p_stat.ssize))
#define PCOUNT ((int64_t) (f_p->fd.meta.p_stat.pcount))

/* ADD_ACCESSES() - adds accesses to handle a portion of a request
 * PARAMETERS:
 *    f_p   - pointer to file descriptor
 *    rl    - location of access relative to start of partition
 *    rs    - size of request
 *    buf_p - pointer to buffer for data (to send or receive)
 *    type  - A_READ or A_WRITE
 *
 * Note: this is called once for each contiguous region in request
 * Returns new buffer pointer location on success, NULL on failure
 */

void *add_accesses(fdesc_p f_p, int64_t rl, int64_t rs, char *buf_p, int type)
{
	int i, pn, sock, myerr; /* partition #, size of access */
	int64_t sz, blk = 0;
	jinfo_p j_p;
	ainfo_p a_p, lasta_p;

	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "  Will access frag(rl = %Ld; rs = %Ld).\n", rl, rs);
	/* compute iod holding rl */
	if (PCOUNT == 1) {
		pn = 0;
		sz = rs;
	}
	else {
		blk = (rl) / SSIZE;
		pn = blk % PCOUNT;
		/* find distance from rl to end of stripe */
		sz = SSIZE - ((rl) % SSIZE);
	}

	while (rs > (int64_t) 0) {
		int slot;
		if (pn > PCOUNT - 1 || pn < 0) {
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " add_accesses: bad pn calculation (rl = %Ld, blk = %Ld, pn = %d)\n", rl, blk, pn);
		}
		/* find socket fd / connect if necessary */
		sock = add_iodtable((struct sockaddr *)&f_p->fd.iod[pn].addr);
		if (sock < 0) {
				myerr = errno;
				PERROR(SUBSYS_LIB,"add_accesses: new_sock");
				errno = myerr;
				return(NULL);
		}
		/* store the index into the iodinfo table */
		slot = f_p->fd.iod[pn].sock = find_iodtable((struct sockaddr *)&f_p->fd.iod[pn].addr);
#ifdef CAPFS_USE_NODELAY
		(void) set_tcpopt(sock, TCP_NODELAY, 1);
#endif
		if (sz > rs) sz = rs; /* request ends before end of stripe */
		LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "    Will read/write %Ld bytes from iod %d.\n", sz, pn);

		/* find appropriate job, note that j_search and j_rem need the sock fd */
		if ((j_p = j_search(active_p, sock)) == NULL) /* need new job */ {
			if (!(j_p = j_new(PCOUNT))) {
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " add_accesses: j_new failed\n");
				return(NULL);
			}

			dfd_set(sock, &j_p->socks);
			if (sock >= j_p->max_nr) j_p->max_nr = sock+1;
			j_p->type = type;
			for (i=0; i < PCOUNT; i++) j_p->psize[i] = 0;

			if (j_add(active_p, j_p) < 0) /* error adding job to list */ {
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " add_accesses: j_add failed\n");
				free(j_p);
				return(NULL);
			}
		}
		/* see if we can coalece with the last entry in the list must
		 * . be of same type
		 * . be for same socket
		 * . start one byte past end of last access */
		lasta_p = a_get_end(j_p->al_p);
		if (lasta_p != NULL &&
							 lasta_p->type == type &&
							 lasta_p->sock == slot &&
							 lasta_p->u.rw.loc + lasta_p->u.rw.size == buf_p ) {
			lasta_p->u.rw.size += sz;
		} else { 
			/* create access */
			if ((a_p = (ainfo_p) malloc(sizeof(ainfo))) == NULL) { 
				myerr = errno;
				PERROR(SUBSYS_LIB,"add_accesses: malloc"); 
				errno = myerr;
				return(NULL); 
			} 
			a_p->type      = type;
			a_p->sock      = slot; /* sock is actually index now */
			a_p->u.rw.loc  = buf_p;
			a_p->u.rw.size = sz;

			/* add access */
			if (a_add_end(j_p->al_p, a_p) < 0) /* error adding to list */ {
				myerr = errno;
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " add_accesses: a_add_end failed\n");
				errno = myerr;
				return(NULL);
			}
		}
		/* update job info */
		j_p->size     += sz;
		j_p->psize[pn]+= sz;

		buf_p += sz;
		rs -= sz;
		sz = SSIZE;
		pn = (pn + 1) % PCOUNT;
	}
	return(buf_p);
} /* end of ADD_ACCESSES() */
#undef SSIZE
		

#define DSIZE    r_p->dsize

/* BUILD_RW_REQ() - Builds a request for an application based on the
 * current logical partition.
 *
 * Parameters:
 * f_p - pointer to file descriptor
 * size - size of requested data in bytes
 * r_p - pointer to request
 * type - type of jlist request (J_READ or J_WRITE)
 * num - no longer used.
 *
 * Note: num parameter was only used in group I/O requests and corresponds
 * to the number of the process (0..n)
 *
 * NOTE: NUM IS NO LONGER USED.
 *
 * Also: type is in terms of a jlist type; it must be converted to an IOD_RW type
 */
int build_rw_req(fdesc_p f_p, int64_t size, ireq_p r_p, int type, int num)
{
	/* set up request */
	r_p->majik_nr     = IOD_MAJIK_NR;
	r_p->release_nr   = CAPFS_RELEASE_NR;
	r_p->type         = IOD_RW;
	DSIZE             = 0;
	r_p->req.rw.f_ino = f_p->fd.meta.u_stat.st_ino;
	r_p->req.rw.cap   = f_p->fd.cap;

	switch (type) {
		case J_READ:
			REQTYPE = IOD_RW_READ;
			break;
		case J_WRITE:
			REQTYPE = IOD_RW_WRITE;
			break;
		default:
			REQTYPE = IOD_RW_WRITE;
			break;
	}

	if (!f_p->part_p) /* no partition specified! */ {
		OFFSET = f_p->fd.off;
		FSIZE  = size;
		LSIZE = GSIZE = GCOUNT = STRIDE = 0;
	}
	else if (!f_p->part_p->gsize || !f_p->part_p->stride ||
		f_p->part_p->gsize == f_p->part_p->stride)
	{
		OFFSET = f_p->fd.off + f_p->part_p->offset;
		FSIZE  = size;
		LSIZE = GSIZE = GCOUNT = STRIDE = 0;
	}
	else {
		/* file pointer is in terms of logical part.; offset is in terms */
		/* of the entire physical file (if all catenated) */
		OFFSET = ((f_p->fd.off / f_p->part_p->gsize) *
			f_p->part_p->stride) + (f_p->fd.off %
			f_p->part_p->gsize) + f_p->part_p->offset;

		FSIZE  = MIN(f_p->part_p->gsize -
			(f_p->fd.off % f_p->part_p->gsize), size);
		GSIZE  = f_p->part_p->gsize;
		STRIDE = f_p->part_p->stride;
		GCOUNT = MAX((size - FSIZE) / f_p->part_p->gsize, 0);
		LSIZE  = MAX(size - (FSIZE + (GCOUNT * GSIZE)), 0);
	}

   LOG(stderr, INFO_MSG, SUBSYS_LIB,  "offset = %Ld; fsize = %Ld; gsize = %Ld; gcount = %Ld; "
		 "stride = %Ld; lsize = %Ld.\n", OFFSET, FSIZE, GSIZE, GCOUNT, STRIDE,
		LSIZE);

	return 0;
} /* end of BUILD_RW_REQ() */

#undef REQTYPE
#undef CLIENTID
#undef DSIZE
#undef FSIZE
#undef GSIZE
#undef LSIZE
#undef GCOUNT
#undef STRIDE
#undef OFFSET

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
