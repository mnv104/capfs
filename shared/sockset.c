/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/*
 * CHECK_SOCK.C - globals and functions used to translate generic socket
 *					 selection routines into platform specific calls
 */

/* UNIX INCLUDES */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>

/* CAPFS INCLUDES */
#include <sockset.h>
#include <dfd_set.h>
#include <sockio.h>
#include <log.h>

/* GLOBALS */
#ifdef __RANDOM_NEXTSOCK__
static int sockset_randomnextsock = 0;
#endif

enum {CHECK_SOCKS_SELECT_HANG_TIMEOUT = 21};

/* FUNCTIONS */

void addsockrd(int sock, sockset_p set_p)
{
	dfd_set(sock, &set_p->read);
	/* max_index MUST equal (max. sock # + 1) */
	if (sock >= set_p->max_index) set_p->max_index = sock+1;
}

void addsockwr(int sock, sockset_p set_p)
{
	dfd_set(sock, &set_p->write);
	if (sock >= set_p->max_index) set_p->max_index = sock+1;
}

/*
 * delsock(sock, set_p)
 *
 * Notes:
 *
 *	Recall that the max index to a select() call should be one more than the
 *	maximum FD value in the set.
 *
 *	So the condition checks to see if the socket was the highest valued one:
 *
 *	  sock + 1 == set_p->max_index
 *
 *	The > there should be unnecessary (should never happen); it's just a
 *	sanity check.  We could make that an error; maybe we would find something.
 *
 *	If in fact the socket is the highest valued one, then we start at that
 *	value and work our way down, looking for another FD value that has been
 *	set.  When we find one, we break; that's the new highest value.  Then we
 *	save it.
 */

void delsock(int sock, sockset_p set_p)
{
	/* make sure we're not going to check for this any more */
	dfd_clr(sock, &set_p->read);
	dfd_clr(sock, &set_p->write);
	dfd_clr(sock, &set_p->tmp_read);
	dfd_clr(sock, &set_p->tmp_write);

	if (sock + 1 >= set_p->max_index) /* need to reduce max_index */ {
		int i;
		for (i = sock; i >= 0; i--) {
			if (dfd_isset(i, &set_p->read) || dfd_isset(i, &set_p->write)) break;
		}
		set_p->max_index = i+1;
	}
}

int randomnextsock(int i)
{
#ifdef __RANDOM_NEXTSOCK__
	struct timeval curtime;

	sockset_randomnextsock = i;

	gettimeofday(&curtime, NULL);
	srand(curtime.tv_usec);

	return sockset_randomnextsock;
#else
	return 1;
#endif
}

/* nextsock() - find a ready but unserviced socket in our sockset
 *
 * Marks socket as "serviced" before returning, so it won't be returned
 * again until checksock() is used again and it is shown to be ready.
 *
 * Returns socket number on success, -1 on failure.
 */
int nextsock(sockset_p set_p)
{
	int i = 0, startval = 0;
	int max_index;

	max_index = set_p->max_index;

	if (max_index <= 0) return -1;

#ifdef __RANDOM_NEXTSOCK__
	if (sockset_randomnextsock) {
		startval = rand() % max_index;
	}

	for (i = startval; i < max_index; i++) {
		if (dfd_isset(i, &set_p->tmp_read) || dfd_isset(i, &set_p->tmp_write)) {
			dfd_clr(i, &set_p->tmp_read);
			dfd_clr(i, &set_p->tmp_write);
			return i;
		}
	}
	/* now i = max_index; test the low region */
#else
	/* we want to avoid using two loops in the normal case, so we check
	 * the entire range with this last one.
	 */
	startval = max_index;
#endif
	for (i = 0; i < startval; i++) {
		if (dfd_isset(i, &set_p->tmp_read) || dfd_isset(i, &set_p->tmp_write)) {
			dfd_clr(i, &set_p->tmp_read);
			dfd_clr(i, &set_p->tmp_write);
			return i;
		}
	}
	return -1;
} /* end of nextsock() */

int dumpsocks(sockset_p set_p)
{
	int i;
	LOG(stderr, DEBUG_MSG, SUBSYS_NONE, "max_index = %d\n", set_p->max_index);
	for (i = 0; i < set_p->max_index; i++) {
		if (dfd_isset(i, &set_p->read)) LOG(stderr, DEBUG_MSG, SUBSYS_NONE, "%d:\tREAD\n", i);
		if (dfd_isset(i, &set_p->write)) LOG(stderr, DEBUG_MSG, SUBSYS_NONE, "%d:\tWRITE\n", i);
	} 
	return(0);
} /* end of DUMPSOCKS() -- select() version */


void finalize_set(sockset_p set_p)
{
	dfd_finalize(&set_p->read);
	dfd_finalize(&set_p->write);
	dfd_finalize(&set_p->tmp_read);
	dfd_finalize(&set_p->tmp_write);
}

void initset(sockset_p set_p)
{
	set_p->max_index = 0;
	/* call dfd_init() in case the sets have never been initialized;
	 * if that fails then call dfd_zero() to clean everything out
	 */

	if (dfd_init(&set_p->read, 0) < 0) dfd_zero(&set_p->read);
	if (dfd_init(&set_p->write, 0) < 0) dfd_zero(&set_p->write);
	if (dfd_init(&set_p->tmp_read, 0) < 0) dfd_zero(&set_p->tmp_read);
	if (dfd_init(&set_p->tmp_write, 0) < 0) dfd_zero(&set_p->tmp_write);
}

/* time given in msec */
/* Returns # of sockets in descriptor set (?), or -1 on error
 */
int check_socks(sockset_p set_p, int time)
{
	int ret;
	int s;
	char tmp_buf;

check_socks_restart:
	/* copy the real structures into the temporary ones */
	dfd_copy(&set_p->tmp_read, &set_p->read);
	dfd_copy(&set_p->tmp_write, &set_p->write);

	if (time == -1) {
		struct timeval to;
		to.tv_sec = CHECK_SOCKS_SELECT_HANG_TIMEOUT;
		to.tv_usec = 0;
		ret = dfd_select(set_p->max_index, &set_p->tmp_read,
		&set_p->tmp_write, NULL, &to);
		if (ret == 0) {
      /*
		 * It can sometimes happen that select() will hang forever waiting
		 * for input on a socket even when data seem to be available.  In
		 * the cases I have seen, the iod on the other end hangs up the
		 * socket connection without completing the transfer, leaving select()
		 * on this end hanging forever.  This seems like a bug in the kernel
		 * tcp stack, and there are reports of similar problems with select()
		 * on 2.4.18 and 2.4.20 on the linux kernel mailing lists.  However
		 * in all cases that I know, the problem traces back to an iod node
		 * that was found to have intermittent memory errors.  Even so, it
		 * would be better for the failed read() to return to the client with
		 * an i/o error than to hang forever in select().  If something has
		 * happened to the socket, we need to attempt some action to make the
		 * code realize it because under Linux 2.4.18 select() is oblivious.
		 *
		 * If the timeout occurs then we need another way to simulate the
		 * proper behavior of select() and return with an error.  I do this
		 * by a recv() call with MSG_PEEK (to prevent loss of data) on each
		 * socket being watched for reading.  On a healthy socket this recv()
		 * will block until data are available, simulating the correct behavior
		 * of select() except that it blocks on the first watched socket until
		 * instead of the OR.  If I have understood the capfs library usage of
		 * check_socks, this is OK.  But unlike select(), the recv will return
		 * immediately with an error if the socket has experienced a hangup.
		 * As long as the connections remain up, this mod produces no change
		 * in the behavior of check_socks.
		 *
		 * Richard.T.Jones@uconn.edu
		 * May 20, 2003
		 */
		/* UPDATE: Phil Carns, June 2003 
		 * The general concept above is seems to good.  However, we will
		 * make two changes:
		 * 1) use a nonblocking peek operation to check socket status after
		 *    select timeout, rather than blocking (this also
		 *    catches EINTR)
		 * 2) report the error by simply setting the right bit in
		 *    our socket set and returning.  Code elsewhere will
		 *    then poke the socket and decide what needs to be done
		 */
			for (s = 0; s < set_p->max_index; s++) {
				if (dfd_isset(s, &set_p->tmp_read)) {
					ret = nbpeek(s, &tmp_buf, 1);
					if(ret != 0){
						/* either there is data ready on the socket, or
						 * else the socket has failed.  In either case, set
						 * the appropriate read bit in our set and return
						 */
						dfd_zero(&set_p->tmp_read);
						dfd_zero(&set_p->tmp_write);
						dfd_set(s, &set_p->tmp_read);
						return(1);
					}
				}
			}
			if (ret == 0) goto check_socks_restart;
		}
	}
	else {
		set_p->timeout.tv_sec = time / 1000;
		set_p->timeout.tv_usec = (time % 1000) * 1000;
		ret = dfd_select(set_p->max_index, &set_p->tmp_read,
		&set_p->tmp_write, NULL, &set_p->timeout);
	}
	if (ret < 0 && errno == EINTR) goto check_socks_restart;
	return(ret);
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
