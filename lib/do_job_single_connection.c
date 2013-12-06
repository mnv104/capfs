/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/*
 * DO_JOB.C - performs IOD interaction for application tasks
 *
 */

/* INCLUDES */
#include <capfs-header.h>
#include <lib.h>

#include <sys/socket.h>
#include <netinet/tcp.h>
#include <string.h>
#include <errno.h>
#include <values.h>
#include <minmax.h>
#include <alist.h>
#include <jlist.h>
#include <req.h>
#include <sockio.h>
#include <sockset.h>
#include <prune.h>
#include <sys/stat.h>
#include <log.h> 
#include <errno.h>


/* DO_JOBS_HANDLE_ERROR() - 
 * PARAMETERS:
 * jl_p - list of jobs corresponding to sockets in ss_p
 * ss_p - sockset checked for readiness for access
 * msec - time to wait (in msec) before timing out; -1 blocks
 *        indefinitely
 * badsock - socket that was closed if a fatal socket error occurred
 *
 * Returns -1 on error, 0 on success.
 */
int do_jobs_handle_error(jlist_p jl_p, sockset_p ss_p, int msec, 
	int* badsock)
{
	int s, ret;
	jinfo_p j_p;
	int myerr;

	/* make sure we don't accidentally indicate a failed socket */
	*badsock = -1;

	/* NEED TO BE LOOKING FOR ERRORS... */
	if (check_socks(ss_p, msec) < 0) {
		myerr = errno;
		PERROR(SUBSYS_LIB," do_jobs: check socks failed: %s\n");
		errno = myerr;
		return(-1); /* wait some amount of time */
	}
	while ((s = nextsock(ss_p)) >= 0) /* still sockets ready */ {
		/* get the iod slot for this socket file descriptor */
		int slot = getslot_iodtable(s);
		if (!(j_p = j_search(jl_p, s))) /* no job for socket */ {
			LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " do_jobs: no job for %d (so why are we watching it?)\n", s);
			delsock(s, ss_p);
		}
		else {
			/* do_job now takes in the iod table slot as first parameter */
			if (do_job(slot, j_p, ss_p) < 0) {
				myerr = errno;
				PERROR(SUBSYS_LIB," do_jobs: do_job failed: %s\n");
				*badsock = s;
				delsock(s, ss_p);
				/* remove job associated with socket */
				do{
					ret = j_rem(jl_p, s);
				}while(ret == 0);
				badiodfd(s);
				errno = myerr;
				return(-1);
			}
			if (alist_empty(j_p->al_p)) {
				/* remove job from joblist */
				LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "do_jobs: no more accesses - removing job\n");
				if ((ret = (j_rem(jl_p, s))) < 0) {
					LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " do_jobs: j_rem failed\n");
				}
			}
		}
	}
	return(0);
}


/* DO_JOBS() - 
 * PARAMETERS:
 * jl_p - list of jobs corresponding to sockets in ss_p
 * ss_p - sockset checked for readiness for access
 * msec - time to wait (in msec) before timing out; -1 blocks
 *        indefinitely
 *
 * Returns -1 on error, 0 on success.
 */
int do_jobs(jlist_p jl_p, sockset_p ss_p, int msec)
{
	/* this is now just a wrapper for the do_jobs_handle_error function
	 * which reports more error information */
	int badsock = -1;
	return(do_jobs_handle_error(jl_p, ss_p, msec, &badsock));
}

/* do_job() - works on a given job, not blocking
 *
 * This function has grown ridiculously long, I know.  I apologize.
 *
 * Basically this function looks at the job, finds the first access for
 * a given socket, checks to make sure that this is indeed the first
 * access in the job, and if so starts working.
 *
 * Rules at the bottom decide when to stop if we don't run out of
 * buffers.  They are designed to try to continue working whenever it
 * seems reasonable, and stop whenever we know that there isn't going to
 * be data ready.
 *
 * Returns -1 if an error is encountered, 0 if we don't do anything, and
 * 0 otherwise.
 */
int do_job(int slot, jinfo_p j_p, sockset_p ss_p)
{
	ainfo_p a_p;
	int smallsize;
	int comp, oldtype, myerr, sock;

	sock = getfd_iodtable(slot);

	/* find first access in job for socket (if at top of list) */
	if (!(a_p = a_get_start(j_p->al_p))) {
		LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " do_job: something bad happened finding top access\n");
		delsock(sock, ss_p);
		badiodfd(sock);
		errno = EINVAL;
		return(-1);
	}
	if (a_p->sock != slot) /* top access not for this socket */ {
		LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "top sock(%d) != current sock(%d)\n", a_p->sock, slot);
		return(0);
	}
	oldtype = a_p->type;

	/* while there are accesses for this socket and we don't block */
	while (a_p) {
		/* Use this sockfd for the accesses */
		int apsock = getfd_iodtable(a_p->sock);/* FIXME: RobR is this is same as sock? */
		/* try to perform the access (read/write/ack/whatever) */
		switch (a_p->type) {
			case A_READ:
			case A_WRITE:
				/* can't try to send more than MAXINT...thus smallsize */
				smallsize = MIN((uint64_t)MAXINT, a_p->u.rw.size);
				LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "read/write (s = %d, ad = %lx, sz = %d) ", apsock,
					(long unsigned) a_p->u.rw.loc, smallsize);
				if (a_p->type == A_READ) comp = nbrecv(apsock,
					a_p->u.rw.loc, smallsize);
				
				else comp = nbsend(apsock, a_p->u.rw.loc, smallsize);

				if (comp < 0) /* error */ {
					myerr = errno;
					PERROR(SUBSYS_LIB,"do_job: nbsend/nbrecv");
					/* check the error and try to decide what to do */
					a_ptr_rem(j_p->al_p, a_p);
					errno = myerr;
					return(-1);
				}
				else if ((a_p->u.rw.size -= comp) <= 0) /* done with access */ {
					LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "done!\n");
					a_ptr_rem(j_p->al_p, a_p);
				}
				else /* partially completed */ {
					LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "partial\n");
					a_p->u.rw.loc  += comp; /* subtract from size above */
					return(0);
				}
				break;
			case A_ACK:
				LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "ack (s = %d) ", apsock);
				comp = nbrecv(apsock, a_p->u.ack.cur_p, a_p->u.ack.size);
				if (comp < 0) {
					myerr = errno;
					PERROR(SUBSYS_LIB,"do_job: nbrecv (ack)");
					/* check the error and try to decide what to do */
					a_ptr_rem(j_p->al_p, a_p);
					errno = myerr;
					return(-1);
				}
				else if ((a_p->u.ack.size -= comp) > 0) /* partially completed */ {
					LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "partial\n");
					a_p->u.ack.cur_p += comp;
					return(0);
				}
				LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "done!\n");

				if (((iack_p)a_p->u.ack.ack_p)->majik_nr != IOD_MAJIK_NR) {
					LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " do_job: IOD_MAJIK_NR does not match\n");
					a_ptr_rem(j_p->al_p, a_p);
					errno = EINVAL;
					return -1;
				}
				if (((iack_p)a_p->u.ack.ack_p)->release_nr != CAPFS_RELEASE_NR) {
					LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " do_job: CAPFS_RELEASE_NR does not match\n");
					a_ptr_rem(j_p->al_p, a_p);
					errno = EINVAL;
					return -1;
				}

				/* check if requested data size > actual size for J_READ */
				if (j_p->type == J_READ
				&& j_p->psize[a_p->u.ack.iod_nr] >
					((iack_p)a_p->u.ack.ack_p)->dsize)
				{
					LOG(stderr, CRITICAL_MSG, SUBSYS_LIB,  " do_job: getting less data than expected\n");
					if (prune_alist_and_zero_mem(j_p->al_p, A_READ, a_p->sock,
					j_p->psize[a_p->u.ack.iod_nr],
					((iack_p)a_p->u.ack.ack_p)->dsize) < 0) {
						LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " do_job: prune failed\n");
					}
				}
				/* if ack value smaller, prune the access list */
				a_ptr_rem(j_p->al_p, a_p); /* remove ack access from list */
				break;
			case A_REQ:
				LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "req (s = %d) ", apsock);
				comp = nbsend(apsock, a_p->u.req.cur_p, a_p->u.req.size);
				if (comp < 0) {
					myerr = errno;
					PERROR(SUBSYS_LIB,"do_job: nbsend (req)");
					/* check the error and try to decide what to do */
					a_ptr_rem(j_p->al_p, a_p);
					errno = myerr;
					return(-1);
				}
				else if ((a_p->u.req.size -= comp) <= 0) {
					LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "done!\n");
					a_ptr_rem(j_p->al_p, a_p);
				}
				else {
					LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "partial\n");
					a_p->u.req.cur_p += comp;
					return(0);
				}
				break;
			default:
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " do_job: invalid access type (%d)\n", a_p->type);
				errno = EINVAL;
				return(-1);
		} /* end of switch */
		/* finished this access; get the next one */
		if ((a_p = a_search(j_p->al_p, slot))) /* more work for this socket */ {
			/* update socket set */
			if (oldtype != a_p->type) {
				switch (a_p->type) {
					case A_ACK:
#if defined(TCP_CORK) && ! defined(CAPFS_USE_NODELAY)
						if (j_p->type == J_WRITE) {
							int val = 0;
							/* turn off TCP_CORK; write is finished */
							setsockopt(sock, SOL_TCP, TCP_CORK, &val, sizeof(val));
						}
#endif
					case A_READ:
						delsock(sock, ss_p);
						addsockrd(sock, ss_p);
						break;
					case A_WRITE:
#if defined(TCP_CORK) && ! defined(CAPFS_USE_NODELAY)
						if (j_p->type == J_WRITE) {
							int val = 1;
							/* turn on TCP_CORK; write is starting */
							setsockopt(sock, SOL_TCP, TCP_CORK, &val, sizeof(val));
						}
#endif
					case A_REQ:
						delsock(sock, ss_p);
						addsockwr(sock, ss_p);
						break;
					default:
						break;
				}
			}
			switch (oldtype) {
				case A_ACK:
					/* if new request is a READ, continue processing.
					 * Otherwise stop here.
					 */
					if (a_p->type != A_READ) return 0;
					break;
				case A_READ:
					/* just keep going for now */
					break;
				case A_WRITE:
					/* if new request is an ACK, stop.  Otherwise continue
					 * processing.
					 */
					if (a_p->type == A_ACK) return 0;
					break;
				case A_REQ:
					/* if the new request is a WRITE, continue processing.
					 * Otherwise stop here.
					 */
					if (a_p->type != A_WRITE) return 0;
					break;
				default:
					break;
			}
			/* see if the next access is the next one for this socket */
			if (a_get_start(j_p->al_p) != a_p) return(0);
			oldtype = a_p->type;
		}
		else /* done with this socket */ {
			delsock(sock, ss_p);
			if (alist_empty(j_p->al_p)) /* done w/job */ {
				/* job is removed one function depth up in do_jobs */
				return(0);
			}
			/* remove socket from job's set */
         dfd_clr(sock, &j_p->socks);
			if (sock + 1 >= j_p->max_nr) /* need to reduce max_nr */ {
				int i;
				for (i=sock; i >= 0; i--) if (dfd_isset(i, &j_p->socks)) break;
				j_p->max_nr = i+1;
			}
		}
	} /* end of while(a_p) */
	return(0);
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
