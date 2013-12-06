/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/* UNIX INCLUDE FILES */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>	/* bzero and bcopy prototypes */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#ifndef _LARGEFILE64_SOURCE
#include <sys/sendfile.h>
#endif
#include <sys/uio.h>
#include <sys/poll.h>
/* CAPFS INCLUDE FILES */
#include <sockio.h>
#include <log.h>

/* FUNCTIONS */
int new_sock() {
	static int p_num = -1; /* set to tcp protocol # on first call */
	struct protoent *pep;

   if (p_num == -1) {
		if ((pep = getprotobyname("tcp")) == NULL) {
			PERROR(SUBSYS_SHARED, "Kernel does not support tcp");
			return(-1);
		}
		p_num = pep->p_proto;
	}
	return(socket(AF_INET, SOCK_STREAM, p_num));
}

/* service == -1 means try to bind to an available privileged port */
int bind_sock(int sockd, int service)
{
	struct sockaddr_in saddr;
	int port = service == -1 ? 1023 : service;

next_port:
	bzero((char *)&saddr,sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = htons((u_short)port);
	saddr.sin_addr.s_addr = INADDR_ANY;
bind_sock_restart:
	if (bind(sockd,(struct sockaddr *)&saddr,sizeof(saddr)) < 0) {
		if (errno == EINTR) goto bind_sock_restart;
		if (service == -1 &&
			 (errno == EINVAL || errno == EADDRINUSE || errno == EADDRNOTAVAIL)) {
			if (--port > 512) goto next_port;
		}
		return(-1);
	}
	return(sockd);
}

int connect_sock(int sockd, char *name, int service)
{
	struct sockaddr saddr;

	if (init_sock(&saddr, name, service) != 0) return(-1);
connect_sock_restart:
	if (connect(sockd,(struct sockaddr *)&saddr,sizeof(saddr)) < 0) {
		if (errno == EINTR) goto connect_sock_restart;
		return(-1);
	}
	return(sockd);
}

int init_sock(struct sockaddr *saddrp, char *name, int service)
{
	struct hostent *hep;

	bzero((char *)saddrp,sizeof(struct sockaddr_in));
	if (name == NULL) {
		if ((hep = gethostbyname("localhost")) == NULL) {
			return(-1);
		}
	}
	else if ((hep = gethostbyname(name)) == NULL) {
		return(-1);
	}
	((struct sockaddr_in *) saddrp)->sin_family = AF_INET;
	((struct sockaddr_in *)saddrp)->sin_port = htons((u_short)service);
	bcopy(hep->h_addr, (char *)&(((struct sockaddr_in *)saddrp)->sin_addr),
		hep->h_length);
	return(0);
}

/* blocking receive */
/* Returns -1 if it cannot get all len bytes
 * and the # of bytes received otherwise
 */
int brecv(int s, void *buf, int len)
{
	int oldfl, ret, comp = len;
	oldfl = fcntl(s, F_GETFL, 0);
	if (oldfl & O_NONBLOCK) fcntl(s, F_SETFL, oldfl & (~O_NONBLOCK));

	while (comp) {
brecv_restart:
		if ((ret = recv(s, (char *) buf, comp, 0)) < 0) {
			if (errno == EINTR) goto brecv_restart;
			return(-1);
		}
		if (!ret) {
			/* Note: this indicates a closed socket.  However, I don't
			 * like this behavior, so we're going to return -1 w/an EPIPE
			 * instead.
			 */
			errno = EPIPE;
			return(-1);
		}
		comp -= ret;
		buf = (char *) buf + ret;
	}
	return(len - comp);
}

/* blocking vector receive */
int bvrecv(int s, const struct iovec *vec, int cnt)
{
	int tot, comp, ret, oldfl, received = 0;
	struct iovec *cur = (struct iovec *)vec;
#ifdef BVRECV_NO_READV
	char *buf;
#else
   struct iovec *alloced_iovecs = NULL;
#endif

	oldfl = fcntl(s, F_GETFL, 0);
	if (oldfl & O_NONBLOCK) fcntl(s, F_SETFL, oldfl & (~O_NONBLOCK));

#ifdef BVRECV_NO_READV
	for (tot=0; cnt--; cur++) {
		buf = (char *) cur->iov_base;
		comp = cur->iov_len;
		while (comp) {
bvrecv_restart:
			if ((ret = recv(s, buf, comp, 0)) < 0) {
				if (errno == EINTR) goto bvrecv_restart;
				return(-1);
			}
			comp -= ret;
			buf = buf + ret;
		}
		tot += cur->iov_len;
	}
	return(tot);
#else
	while (1) {
bvrecv_restart:
		if ((ret = readv(s, cur, cnt)) < 0) {
			if (errno == EINTR) goto bvrecv_restart;
			if (NULL != alloced_iovecs)
				free(alloced_iovecs);
			return(-1);
		}
		received += ret;
		for(comp=0,tot=0; comp<cnt; comp++) {
			tot += cur[comp].iov_len;
			if (tot > ret) break;
		}
		if (tot == ret && comp == cnt) break;
		if (vec == cur) {
			if (!(cur = (struct iovec *)malloc(cnt*sizeof(struct iovec)))) {
				return(-1);
			}
			alloced_iovecs = cur;
			memcpy(cur, vec, cnt*sizeof(struct iovec));
		}
		cnt -= comp;
		cur += comp;
		cur[0].iov_base = (char*)cur[0].iov_base + cur[0].iov_len-(tot-ret);
		cur[0].iov_len = tot-ret;
	}
	if (NULL != alloced_iovecs)
		free(alloced_iovecs);
	return(received);
#endif
}

/* nonblocking receive */
int nbrecv(int s, void *buf, int len)
{
	int oldfl, ret, comp = len;

	oldfl = fcntl(s, F_GETFL, 0);
	if (!(oldfl & O_NONBLOCK)) fcntl(s, F_SETFL, oldfl | O_NONBLOCK);

	while (comp) {
nbrecv_restart:
		ret = recv(s, buf, comp, 0);
		if (!ret) /* socket closed */ {
			errno = EPIPE;
			return(-1);
		}
		if (ret == -1 && errno == EWOULDBLOCK) {
			LOG(stderr, DEBUG_MSG, SUBSYS_SHARED, "nbrecv: would block\n");
			return(len - comp); /* return amount completed */
		}
		if (ret == -1 && errno == EINTR) {
			goto nbrecv_restart;
		}
		else if (ret == -1) {
			PERROR(SUBSYS_SHARED, "nbrecv: recv");
			return(-1);
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_SHARED, "nbrecv: s = %d, expect = %d, actual = %d\n", s, comp, ret);
		comp -= ret;
		buf = (char *) buf + ret;
	}
	return(len - comp);
}

/* blocking send */
int bsend(int s, void *buf, int len)
{
	int oldfl, ret, comp = len;
	oldfl = fcntl(s, F_GETFL, 0);
	if (oldfl & O_NONBLOCK) fcntl(s, F_SETFL, oldfl & (~O_NONBLOCK));

	while (comp) {
bsend_restart:
		if ((ret = send(s, (char *) buf, comp, 0)) < 0) {
			if (errno == EINTR) goto bsend_restart;
			return(-1);
		}
		comp -= ret;
		buf = (char *) buf + ret;
	}
	return(len - comp);
}

/* blocking vector send */
int bsendv(int s, const struct iovec *vec, int cnt)
{
	int tot, comp, ret, oldfl, sent = 0;
	struct iovec *cur = (struct iovec *)vec;
#ifdef BSENDV_NO_WRITEV
	char *buf;
#else
   struct iovec *alloced_iovecs = NULL;
#endif

	oldfl = fcntl(s, F_GETFL, 0);
	if (oldfl & O_NONBLOCK) fcntl(s, F_SETFL, oldfl & (~O_NONBLOCK));

#ifdef BSENDV_NO_WRITEV
	for (tot=0; cnt--; cur++) {
		buf = (char *) cur->iov_base;
		comp = cur->iov_len;
		while (comp) {
bsendv_restart:
			if ((ret = send(s, buf, comp, 0)) < 0) {
				if (errno == EINTR) goto bsendv_restart;
				return(-1);
			}
			comp -= ret;
			buf = buf + ret;
		}
		tot += cur->iov_len;
	}
	return(tot);
#else
	while (1) {
bsendv_restart:
		if ((ret = writev(s, cur, cnt)) < 0) {
			if (errno == EINTR) goto bsendv_restart;
			if (NULL != alloced_iovecs)
				free(alloced_iovecs);
			return(-1);
		}
		sent += ret;
		for(comp=0,tot=0; comp<cnt; comp++) {
			tot += cur[comp].iov_len;
			if (tot > ret) break;
		}
		if (tot == ret && comp == cnt) break;
		if (vec == cur) {
			if (!(cur = (struct iovec *)malloc(cnt*sizeof(struct iovec))))
				return(-1);
			alloced_iovecs = cur;
			memcpy(cur, vec, cnt*sizeof(struct iovec));
		}
		cnt -= comp;
		cur += comp;
		cur[0].iov_base = (char*)cur[0].iov_base + cur[0].iov_len-(tot-ret);
		cur[0].iov_len = tot-ret;
	}
	if (NULL != alloced_iovecs)
		free(alloced_iovecs);
	return(sent);
#endif
}

/* nonblocking send */
/* should always return 0 when nothing gets done! */
int nbsend(int s, void *buf, int len)
{
	int oldfl, ret, comp = len;
	oldfl = fcntl(s, F_GETFL, 0);
	if (!(oldfl & O_NONBLOCK)) fcntl(s, F_SETFL, oldfl | O_NONBLOCK);

	while (comp) {
nbsend_restart:
		ret = send(s, (char *) buf, comp, 0);
		if (ret == 0 || (ret == -1 && errno == EWOULDBLOCK))
			return(len - comp); /* return amount completed */
		if (ret == -1 && errno == EINTR) {
			goto nbsend_restart;
		}
		else if (ret == -1) return(-1);
		comp -= ret;
		buf = (char *) buf + ret;
	}
	return(len - comp);
}

#if 0
/* NBSENDFILE() - nonblocking (on the socket) send from file
 *
 * Here we are going to take advantage of the sendfile() call provided
 * in the linux 2.2 kernel to send from an open file directly (ie. w/out
 * explicitly reading into user space memory or memory mapping).
 *
 * We are going to set the non-block flag on the socket, but leave the
 * file as is.
 *
 * Returns -1 on error, amount of data written to socket on success.
 */
int nbsendfile(int s, int f, off_t off, int len)
{
	int oldfl, ret, comp = len;
	off_t myoff;

	oldfl = fcntl(s, F_GETFL, 0);
	if (!(oldfl & O_NONBLOCK)) fcntl(s, F_SETFL, oldfl | O_NONBLOCK);

	while (comp) {
nbsendfile_restart:
		myoff = off;
		ret = sendfile(s, f, &myoff, comp);
		if (ret == 0 || (ret == -1 && errno == EWOULDBLOCK))
			return(len - comp); /* return amount completed */
		if (ret == -1 && errno == EINTR) {
			goto nbsendfile_restart;
		}
		else if (ret == -1) return(-1);
		comp -= ret;
		off += ret;
	}
	return(len - comp);
}
#endif

/* routines to get and set socket options */
int get_sockopt(int s, int optname)
{
	int val, len = sizeof(val);
	if (getsockopt(s, SOL_SOCKET, optname, &val, &len) == -1) return(-1);
	else return val;
}

int set_tcpopt(int s, int optname, int val)
{
#ifdef SOL_TCP
	if (setsockopt(s, SOL_TCP, optname, &val, sizeof(val)) == -1) return(-1);
	else return val;
#else
	return val;
#endif
}

int set_sockopt(int s, int optname, int val)
{
	if (setsockopt(s, SOL_SOCKET, optname, &val, sizeof(val)) == -1)
		return(-1);
	else return(val);
}

int get_socktime(int s, int optname)
{
	struct timeval val;
	int len = sizeof(val);
	if (getsockopt(s, SOL_SOCKET, optname, &val, &len) == -1)
		return(-1);
	else return(val.tv_sec * 1000000 + val.tv_usec);
}

int set_socktime(int s, int optname, int size)
{
	struct timeval val;
	val.tv_sec = size / 1000000;
	val.tv_usec = size % 1000000;
	if (setsockopt(s, SOL_SOCKET, optname, &val, sizeof(val)) == -1)
		return(-1);
	else return(size);
}

/* SOCKIO_DUMP_SOCKADDR() - dump info in a sockaddr structure
 *
 * Might or might not work for any given platform!
 */
int sockio_dump_sockaddr(struct sockaddr_in *ptr, FILE *fp)
{
	int i;
	unsigned char *uc;
	struct hostent *hp;
	char abuf[]="xxx.xxx.xxx.xxx\0";

	LOG(fp, WARNING_MSG, SUBSYS_SHARED, "sin_family = %d\n", ptr->sin_family);
	LOG(fp, WARNING_MSG, SUBSYS_SHARED, "sin_port = %d (%d)\n", ptr->sin_port, ntohs(ptr->sin_port));
	/* print in_addr info */
	uc = (unsigned char *) &ptr->sin_addr;
	sprintf(abuf, "%d.%d.%d.%d", uc[0], uc[1], uc[2], uc[3]);
	hp = gethostbyaddr((char *)&ptr->sin_addr.s_addr, sizeof(ptr->sin_addr),
		ptr->sin_family);
	if (hp && hp->h_name)
	{
		LOG(fp, WARNING_MSG, SUBSYS_SHARED, "sin_addr = %s (%s)\n", abuf, hp->h_name);
	}
	else
	{
		LOG(fp, WARNING_MSG, SUBSYS_SHARED, "sin_addr = %s\n", abuf);
	}
	for (i = 0; i < sizeof(struct sockaddr) - sizeof(short int) -
		sizeof(unsigned short int) - sizeof(struct in_addr); i++)
	{
		LOG(fp, INFO_MSG, SUBSYS_SHARED, "%x", ptr->sin_zero[i]);
	}

	return 0;
} /* end of SOCKIO_DUMP_SOCKADDR() */

/* connect_timeout()
 *
 * Attempts to do nonblocking connect() until a timeout occurs.  Useful for
 * recovering from deadlocks...
 *
 * I apologize for all the goto's, but I hate to every syscall in a while()
 * just for the EINTR case!  
 */
int connect_timeout(int s, struct sockaddr *saddrp, int len, int time_secs)
{
	struct pollfd pfds;
	int ret, oldfl, err, val, val_len;

	/* set our socket to nonblocking */
	oldfl = fcntl(s, F_GETFL, 0);
	if (!(oldfl & O_NONBLOCK)) fcntl(s, F_SETFL, oldfl | O_NONBLOCK);

connect_timeout_connect_restart:
	ret = connect(s, saddrp, len);
	if (ret < 0) {
		if (errno == EINTR) goto connect_timeout_connect_restart;
		if (errno != EINPROGRESS) goto connect_timeout_err;

		pfds.fd = s;
		pfds.events = POLLOUT;
connect_timeout_poll_restart:
		ret = poll(&pfds, 1, time_secs * 1000);
		if (ret < 0) {
			if (errno == EINTR) goto connect_timeout_poll_restart;
			else goto connect_timeout_err;
		}
		if (ret == 0) {
			/* timed out */
			err = errno;
			close(s); /* don't want it to keep trying */
			errno = err;
			goto connect_timeout_err;
		}
	}

	/* _apparent_ success -- check if connect really completed */
	val_len = sizeof(val);
	ret = getsockopt(s, SOL_SOCKET, SO_ERROR, &val, &val_len);
	if (val != 0) {
		errno = val;
		goto connect_timeout_err;
	}

	/* success -- make socket blocking again and return */
	fcntl(s, F_SETFL, oldfl & (~O_NONBLOCK));
	return 0;

connect_timeout_err:
	/* save errno, set flags on socket back to what they were, return -1 */
	err = errno;
	fcntl(s, F_SETFL, oldfl);
	errno = err;
	return -1;
}


/* brecv_timeout()
 *
 * Attempts to do nonblocking recv's until a timeout occurs.  Useful for
 * recovering from deadlocks in situations where you want to give up if a
 * message does not finish arriving within a certain time frame.
 *
 * Same args as brecv, but with an additional timeout argument that is an
 * integer number of seconds.   
 * 
 * returns number of bytes received, even if it did not recv all that was
 * asked.  If any error other than timeout occurs, it returns -1 and sets
 * errno accordingly.  */
int brecv_timeout(int s, void *vbuf, int len, int timeout)
{
	char *buf = (char *) vbuf;
	int recv_size = len;    /* amt we hope for each iteration */
	int recv_total = 0;     /* total amt recv'd thus far */
	char * recv_ptr = buf;   /* where to put the data */
	int initial_tries = 8;  /* number of initial attempts to make */
	int i = 0;
	int ret = -1;
	struct pollfd poll_conn;

	/* This determines the backoff times.  It will do a few attempts with the
	 * initial backoff first, then wait and do a final one after the
	 * remainder of the time is up.  Note that the initial backoff will be
	 * zero if a small overall timeout is specified by the caller.
	 */
	int initial_backoff = timeout/initial_tries;
	int final_backoff = timeout - (timeout/initial_tries);

	/* we don't accept -1 for infinite timeout.  That is the job of brecv. */
	if(timeout < 0)
	{
		errno = EINVAL;
		return(-1);
	}

	/* initial attempts */
	do
	{
		/* nbrecv() handles EINTR on its own */
		if((ret = nbrecv(s, recv_ptr, recv_size)) < 0)
		{
			/* bail out at any error */
			return(ret);
		}

		/* update our progress */
		recv_size -= ret;
		recv_total += ret;
		recv_ptr += ret;

		/* if we didn't finish, then poll for a while. */
		if(recv_total < len)
		{
			poll_conn.fd = s;
			poll_conn.events = POLLIN;
brecv_timeout_poll1_restart:
			ret = poll(&poll_conn, 1, (initial_backoff*1000));
			if(ret < 0)
			{
				if (errno == EINTR) goto brecv_timeout_poll1_restart;
				/* bail out on any "real" error */
				return(ret);
			}
		}
		i++;
	}
	while((i < initial_tries) && (recv_total < len));
	
	/* see if we are done */
	if (recv_total == len) {
		return(len);
	}

	/* not done yet, give it one last chance: */
	poll_conn.fd = s;
	poll_conn.events = POLLIN;
brecv_timeout_poll2_restart:
	ret = poll(&poll_conn, 1, (final_backoff*1000));
	if(ret < 0)
	{
		if (errno == EINTR) goto brecv_timeout_poll2_restart;
		/* bail out on any "real" error */
		return(ret);
	}

	if((ret = nbrecv(s, recv_ptr, recv_size)) < 0)
	{
		LOG(stderr, WARNING_MSG, SUBSYS_SHARED, "brecv_timeout: giving up");
		return(ret);
	}

	recv_size -= ret;
	recv_total += ret;

	return(recv_total);
}

/* nbpeek()
 *      
 * performs a nonblocking check to see if the amount of data requested
 * is actually available in a socket.  Does not actually read the
 * data out.     
 *          
 * returns number of bytes available on succes, -1 on failure.
 */     
int nbpeek(int s, void* buf, int len) 
{
	int oldfl, ret, comp = len;

	oldfl = fcntl(s, F_GETFL, 0);
	if (!(oldfl & O_NONBLOCK))
		fcntl(s, F_SETFL, oldfl | O_NONBLOCK);

	while (comp)
	{
		nbpeek_restart:
		ret = recv(s, buf, comp, MSG_PEEK);
		if (!ret)       /* socket closed */
		{
			errno = EPIPE;
			return (-1);
		}
		if (ret == -1 && errno == EWOULDBLOCK)
		{
			return (len - comp);        /* return amount completed */
		}
		if (ret == -1 && errno == EINTR)
		{
			goto nbpeek_restart;
		}
		else if (ret == -1)
		{
			return (-1);
		}
		comp -= ret;
	}
	return (len - comp);
}

int blockingRecv(int sockfd, void* buffer, int size)
{
	int oldFlags, remaining, retVal,error;

	remaining=size;
	oldFlags = fcntl(sockfd,F_GETFL,0);
	if (oldFlags & O_NONBLOCK)
		fcntl(sockfd,F_SETFL, oldFlags & (~O_NONBLOCK));
	while(remaining>0)
	{
		//fprintf(stderr, "About to block on recv for %d bytes from socket %d\n", remaining, sockfd);
		retVal=recv(sockfd,(unsigned char*)buffer,remaining,0);
		error=errno;
		if(!retVal)
		{
			return -1;
		}
		if ((retVal==-1)&& (error!=EINTR))
		{
			PERROR(SUBSYS_SHARED, "blocking recv:");
			return -1;
		}
		else
			if ((retVal==-1)&&(error==EINTR))
				retVal=0;
		remaining -= retVal;
		buffer = (unsigned char*)buffer + retVal;
	}
	//fprintf(stderr, "Finished blocking recv for %d bytes on socket %d\n", size, sockfd);
	return size-remaining;
}

int blockingSend(int sockfd, void* buffer, int size)
{
	int oldFlags, remaining, retVal,error;

	remaining=size;
	oldFlags = fcntl(sockfd,F_GETFL,0);
	if (oldFlags & O_NONBLOCK)
		fcntl(sockfd,F_SETFL, oldFlags & (~O_NONBLOCK));
	while(remaining>0)
	{
		retVal=send(sockfd,(unsigned char*)buffer,remaining,0);
		error=errno;
		if ((retVal==-1)&& (error!=EINTR))
		{
			PERROR(SUBSYS_SHARED, "blocking send:");
			return -1;
		}
		else
			if ((retVal==-1)&& (error==EINTR))
				retVal=0;
		remaining -= retVal;
		buffer = (unsigned char*)buffer + retVal;
	}
	//fprintf(stderr, "Finished sending %d bytes on socket %d\n", size, sockfd);
	return size-remaining;
}

int blockingSendFile(int in_fd, int sockfd, int size)
{
	int oldFlags, remaining, retVal,error;
	off_t offset=0;

	remaining=size;
	oldFlags = fcntl(sockfd,F_GETFL,0);
	if (oldFlags==-1)
	{
		LOG(stderr, INFO_MSG, SUBSYS_SHARED, "fcntl returned 0 on socket");
	}
	if (oldFlags & O_NONBLOCK)
		fcntl(sockfd,F_SETFL, oldFlags & (~O_NONBLOCK));
	while(remaining>0)
	{
		retVal=sendfile(sockfd, in_fd, &offset, remaining);
		error=errno;
		if ((retVal==-1)&& (error!=EINTR))
		{
			PERROR(SUBSYS_SHARED, "blocking sendfile:");
			return -1;
		}
		else
			if ((retVal==-1)&& (error==EINTR))
				retVal=0;
		remaining -= retVal;
		offset = offset + retVal;
	}
	//fprintf(stderr, "Finished sendfiling %d bytes on socket %d\n", size, sockfd);
	return size-remaining;
}

int nb_connect(int sockNum, struct sockaddr *s)
{
	int returnValue, error = 0;
	do
	{
		returnValue = connect(sockNum, s, sizeof(struct sockaddr));
		if (returnValue==0)
			break;
		if (returnValue == -1)
			error=errno;
		if (error == EINTR)
			continue;
		LOG(stderr, CRITICAL_MSG, SUBSYS_SHARED, "connect to %s:%d failed due to %s\n", 
				inet_ntoa(((struct sockaddr_in *)s)->sin_addr), ntohs(((struct sockaddr_in *)s)->sin_port), strerror(errno));
		return 0;
	}while(1);
	if (set_tcpopt(sockNum, TCP_NODELAY, 1) < 0)
	{
		LOG(stderr, WARNING_MSG, SUBSYS_SHARED, "Could not kill Nagle's algorithm...\n");
	}
	return 1;
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
