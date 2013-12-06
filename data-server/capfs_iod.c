/*
 * Copyright (C) October 2004 
 * 	Initial version: Partho Nath (nath@cse.psu.edu)
 *		Rewritten to use RPCs November 2004, Murali Vilayannur (vilayann@cse.psu.edu)	
 *
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <syslog.h>

#include <signal.h>
#include <sys/vfs.h>
#include <sockio.h>
#include <iod.h>
#include <values.h>
#include <iod_config.h>
#include <dirent.h>
#include <utime.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/uio.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <errno.h>
#include <rpc/pmap_clnt.h>

#include <log.h>
#include <cas.h>
#include <tp_proto.h>
#include "capfs_iod.h"
#include "sockio.h"
#include "rpcutils.h"
#include "tp_proto.h"
#include "log.h"
#include "iod_prot.h"
#include "capfs_config.h"

#define  _CAPFS_DISPATCH_FN(x) capfs_iod_ ## x
#define  CAPFS_DISPATCH_FN(x)  _CAPFS_DISPATCH_FN(x)

/* Are we using a sockets based approach or not? */
int use_sockets = 0;
fd_set global_readsock_set, aux_readsock_set;
static int is_daemon = 1;
static char * iod_conf_name;
static struct svc_info info = {
use_thread: 0,
};
static tp_id   id;
static int capfs_init(int argc, char **argv);
static void capfs_cleanup(void);
extern void CAPFS_DISPATCH_FN(iodv1)(struct svc_req *, register SVCXPRT *);

static inline char binTo64(unsigned char* bin, int skipBits)
{
	unsigned char ch1, ch2;
	unsigned int i, j, k, l;
	int bitsToLeft, bitsToRight;
	int bitsFromLeftChar, bitsFromRightChar;

	if (skipBits < 3)
	{
		ch1 = bin[0];
		bitsToLeft = skipBits;
		bitsToRight = 8-6-skipBits;
		i=(int)ch1;
		i>>=bitsToRight;
		j = i & 63; /*get lowest 6 bits */
	}
	else
	{
		ch1 = bin[0];
		ch2 = bin[1];
		bitsFromLeftChar  = (8-skipBits);
		bitsFromRightChar = 6 - bitsFromLeftChar;
		bitsToRight       = 8 - bitsFromRightChar;
		i = (int)ch1;
		k = 1;
		for(l = 0; l< bitsFromLeftChar; l++)
			k = k*2;
		k = k-1;
		j = i & k;
		j = j << bitsFromRightChar;
		i = (int)ch2;
		k = 1;
		for(l = 0; l< bitsFromRightChar; l++)
			k = k*2;
		k = k-1;
		i = i>>bitsToRight;
		l = i & k;
		k = l | j;
		j = k;
	}
	if (j < 10)
		return ('0'+j);
	if (j < 36)
		return ('a'+ (j-10));
	if (j < 62)
		return ('A'+ (j-36));
	if (j==62)
		return '_';
	if (j==63)
		return '.';
	return '.'; /* default case */
}

char* get_fileName(void* binHash)
{
	unsigned char* bin;
	unsigned char* ptr, ch;
	char* fileName;
	int i, j, k, skip;
	int index;

	bin = (unsigned char *)binHash;
	ptr=(bin);
	fileName = malloc(32);
	ch=*ptr;
	j=(int)ch;
	k = j>>4;
	if (k>9)
		fileName[0]='a'+(k-10);
	else
		fileName[0]='0'+(k-0);
	fileName[1]=binTo64(binHash, 4);
	fileName[2]='/';
	index = 3;
	ptr=(bin+1);
	
	for (i = 0, skip = 2; i< 25; i++)
	{
		fileName[index]=binTo64(ptr, skip);
		index++;
		skip+=6;
		if (skip>8)
		{
			skip -= 8;
			ptr++;
		}
	}
	fileName[28]=0;
	return fileName;
}

static int create_capfsiod_file(char *dir)
{
	char buf[32];
	int fd;

	if (dir) {
		snprintf(buf, 32, "%s/.capfsiod", dir);
	}
	else {
		snprintf(buf, 32, ".capfsiod");
	}
	/* Check if the file exists. if not create it */
	if (access(buf, F_OK) < 0) {
		fd = open(buf, O_RDWR | O_CREAT, 0700);
		close(fd);
	}
	return 0;
}

static int check_dir(char c1, char c2)
{
	char buf[64], dir[64];
	int fd, error;
	sprintf(buf, "%c%c/testopen", c1, c2);
	/* try to touch a file in the directory */
	/* see if we can write into our working directory */
	if ((fd = open(buf, O_RDWR | O_CREAT, 0777)) < 0) {
		error = -errno;
		if (error != -ENOENT) return error;
		else {
			/* create the directory if possible */
			sprintf(dir, "%c%c", c1, c2);
			if (mkdir(dir, 0700) < 0) {
				error = -errno;
				return error;
			}
		}
	}
	close(fd);
	unlink(buf);
	sprintf(buf, "%c%c", c1, c2);
	create_capfsiod_file(buf);
	return 0;
}

/* verify_dirs()
 *
 * Ensures that all subdirectories for file data storage are present,
 * creating any which are not.
 *
 * Also creates a file called .capfsiod file that indicates if the sub-directory
 * is really a CAPFS IOD subdirectory or not.
 *
 * Returns 0 on success, -errno on failure.
 * MODIFIED from original: PARTHO
 */
static int verify_dirs(void)
{
	int  error;
	char c1, c2;

	create_capfsiod_file(NULL);
	/* the file name format is described in capfs.readme */
	for(c1='0';c1<='9';c1++)
	{
		for(c2='0';c2<='9';c2++)
		{
			error = check_dir(c1, c2);
			if (error < 0)
				return error;
		}

		for(c2='a';c2<='z';c2++)
		{
			error = check_dir(c1, c2);
			if (error < 0)
				return error;
		}

		for(c2='A';c2<='Z';c2++)
		{
			error = check_dir(c1, c2);
			if (error < 0)
				return error;
		}

		c2='_';
		error = check_dir(c1, c2);
		if (error < 0)
			return error;

		c2='.';
		error = check_dir(c1, c2);
		if (error < 0)
			return error;
	}

	for(c1='a';c1<='f';c1++)
	{
		for(c2='0';c2<='9';c2++)
		{
			error = check_dir(c1, c2);
			if (error < 0)
				return error;
		}

		for(c2='a';c2<='z';c2++)
		{
			error = check_dir(c1, c2);
			if (error < 0)
				return error;
		}

		for(c2='A';c2<='Z';c2++)
		{
			error = check_dir(c1, c2);
			if (error < 0)
				return error;
		}

		c2='_';
		error = check_dir(c1, c2);
		if (error < 0)
			return error;

		c2='.';
		error = check_dir(c1, c2);
		if (error < 0)
			return error;
	}
	return 0;
}

static void do_signal(int s, siginfo_t* info, void *un) 
{
	capfs_cleanup();
	exit(1);
}

struct slave_args {
	struct svc_req *rqstp;
	SVCXPRT *transp;
};

static void *capfs_slave_func(void *args)
{
	struct slave_args *sl_args = (struct slave_args *) args;
	struct svc_req *rqstp;
	register SVCXPRT *xprt;

	if (sl_args == NULL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "slave thread got invalid arguments!\n");
		capfs_cleanup();
		exit(1);
	}
	rqstp = sl_args->rqstp;
	xprt  = sl_args->transp;

	/* call back into the RPC layer dispatch function */
	CAPFS_DISPATCH_FN(iodv1)(rqstp, xprt);

	free(sl_args);
	free(rqstp);
	return NULL;
}

/*
 * Determines if "sock" is a TCP/UDP based socket.
 * This is a pretty nasty way to do it, but I don't
 * know if there is any other way to do this.
 */
static int is_tcp(int sock)
{
	static int level = -1;
	struct tcp_info info;
	int len = sizeof(info);

	if (level < 0) {
		level = (getprotobyname("tcp"))->p_proto;
	}
	if (getsockopt(sock, level, TCP_INFO, &info, &len) != 0) {
		if (errno == ENOPROTOOPT || errno == EOPNOTSUPP) {
			return 0;
		}
		return -1;
	}
	return 1;
}

/* called by the RPC layer when a request arrives */
static void capfs_dispatch(struct svc_req *rqstp, SVCXPRT *xprt)
{
	/*
	 * what we do here is assign this request to the slave thread
	 * from the pool, and continue back to the RPC layer.
	 */
	struct slave_args *sl_args = NULL;
	int is_it_tcp;

	sl_args = (struct slave_args *) calloc(1, sizeof(struct slave_args));
	if (sl_args == NULL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "calloc failed. exiting!\n");
		capfs_cleanup();
		exit(1);
	}
	sl_args->rqstp = (struct svc_req *)  calloc(1, sizeof(*rqstp));
	if (sl_args->rqstp == NULL) {
		free(sl_args);
		LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "calloc failed. exiting!\n");
		capfs_cleanup();
		exit(1);
	}
	memcpy(sl_args->rqstp, rqstp, sizeof(*rqstp));
	sl_args->transp = xprt;
	/*
	 * At this point determine if the connection is on the UDP/TCP transport.
	 * We have had problems with multi-threading when xport was TCP,
	 * so we will dynamically determine that and either dispatch it
	 * to the thread pool or we will service it directly here.
	 */
	is_it_tcp = is_tcp(rqstp->rq_xprt->xp_sock);
	/* only if it is a udp-based xport we will use a thread pool */
	if (is_it_tcp == 0) {
		typedef void *(*pthread_fn)(void *);
		tp_assign_work_by_id(id, (pthread_fn) capfs_slave_func, (void *)sl_args);
	}
	else {
		/* either tcp or something else, we service it ourselves */
		capfs_slave_func(sl_args);
	}
	return;
}

pthread_spinlock_t fdset_spinlock;

static inline void fd_lock(void)
{
	pthread_spin_lock(&fdset_spinlock);
}

static inline void fd_unlock(void)
{
	pthread_spin_unlock(&fdset_spinlock);
}

static int capfs_init(int argc, char **argv)
{
	int fd = 0, port = CAPFS_IOD_BASE_PORT, i, c;
	int nLogLevel = CRITICAL_MSG | WARNING_MSG; 
	char logname[] = "iod";
	int logflags = O_APPEND|O_CREAT|O_RDWR|O_LARGEFILE, nthreads = -1;
	char logfile[1024];
	struct sigaction handle;
	static tp_info tinfo;

	while ((c = getopt(argc, argv, "f:n:sdh")) != EOF) {
		switch (c) {
			case 's':
				use_sockets = 1;
				break;
			case 'n':
				nthreads = atoi(optarg);
				break;
			case 'f':
				iod_conf_name = strdup(optarg);
				break;
			case 'd':
				is_daemon = 0;
				break;
			case 'h':
			case '?':
			default:
				fprintf(stderr, "Usage: %s -n <number of threads> -f <iod conf file> -d {don't daemonize} -h {this message} -s {Use sockets based server}\n", argv[0]);
				return -1;
		}
	}

	if (iod_conf_name == NULL)
	{
		/* use default config file name */
		iod_conf_name = strdup("./iod.conf");
	}
	if(!iod_conf_name)
	{
		printf("No config file found\n");
		return -EINVAL;
	}

	if (parse_config(iod_conf_name) < 0) {
		fprintf(stderr, "error parsing config file %s: %s\n", iod_conf_name, strerror(errno));
		fprintf(stderr, "Usage: %s -f <iod conf file> -d {don't daemonize} -h {this message}\n", argv[0]);
		return(-1);
	}

	/* open up log file and redirect stderr to it */
	snprintf(logfile, 1024, "%s/%s", get_config_logdir(), logname);

	/* Read the logging level and set the current logging level to that state */
	nLogLevel = get_config_log_level();
#ifdef DEBUG
	nLogLevel |= INFO_MSG;
	nLogLevel |= DEBUG_MSG;
#endif

	if (is_daemon) {
		/* new behavior is nice and uses the same name */
		if ((fd = open(logfile, logflags, 0700)) == -1) return -1;
		fchmod(fd, 0755);
		dup2(fd, 2);
		dup2(fd, 1);
	}

   set_log_level(nLogLevel); 
	port = get_config_port();

	/* setup socket for listening here */
	if (use_sockets == 1)
	{
		/* grab a socket for receiving requests */
		/* new_sock is defined in shared/sockio.c */
		if ((fd = new_sock()) == -1) return(-1);

		/* set up for fast restart */
		set_sockopt(fd, SO_REUSEADDR, 1);

		if (bind_sock(fd, port) == -1) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, " error binding to port\n");
			return(-1);
		}

		if (listen(fd, IOD_BACKLOG) == -1) return(-1);
	}

	/* be rude about scheduling */
	if (!geteuid()) nice(-20);

	/* set up environment */
	if (set_config() < 0) {
		FILE *fp;
		fprintf(stderr, " error setting up environment from config file\n");
		fp = fdopen(1, "w");
		dump_config(fp);
		return(-1);
	}

	/* become a daemon */
	if (is_daemon) {
		close(0);
		if (fork()) exit(0);
		setsid();
	}
	/* Setup handlers for SIGHUP, SIGSEGV, SIGPIPE, SIGUSR2 */
	memset(&handle, 0, sizeof(handle));
	handle.sa_flags |= SA_SIGINFO;
	handle.sa_sigaction = do_signal;
	sigaction(SIGHUP, &handle, NULL);
	sigaction(SIGSEGV, &handle, NULL);
	sigaction(SIGPIPE, &handle, NULL);
	sigaction(SIGTERM, &handle, NULL);
	sigaction(SIGINT, &handle, NULL);
 
	/* see if we can write into our working directory */
	if ((i = open("testopen", O_RDWR | O_CREAT, 0777)) < 0) {
		PERROR(SUBSYS_DATA,"cannot open files in working directory");
		return(-1);
	}
	close(i);
	unlink("testopen");

	/* ensure all data subdirectories are present and accessable */
	if ((i = verify_dirs()) < 0) {
		errno = -i;
		PERROR(SUBSYS_DATA,"error verifying data subdirectories");
		return -1;
	}

	if (is_daemon) {
		openlog("iod", LOG_PID, LOG_ACC_FACILITY);
	}
	if (use_sockets == 1)
	{
		fprintf(stderr, "----------- Starting CAPFS CAS server servicing on socket %d using a thread pool [%d threads] -----------\n", fd, (nthreads <= 0) ? __iod_config.num_threads : nthreads);
	}
	else
	{
		fprintf(stderr, "----------- Starting CAPFS CAS server servicing RPCs using a thread pool [%d threads] -----------\n", (nthreads <= 0) ? __iod_config.num_threads : nthreads);
	}
	tinfo.tpi_name = NULL;
	tinfo.tpi_stack_size=-1;
	tinfo.tpi_count = (nthreads <= 0) ? __iod_config.num_threads : nthreads;
	id = tp_init(&tinfo);
	if (id < 0) {
		printf("Could not initialize thread pool\n");
		return -EINVAL;
	}

	if (use_sockets == 0)
	{
		/*
		 * setup a local RPC server at the first available program number for the specified port 
		 * NOTE: This kind of implies that the CAPFS_IOD program number is kind of meaningless
		 * because we may want to be able to run multiple I/O servers on the same node on different
		 * ports in which case only one of the I/O service would be visible to the world.
		 * Consequently, the way the system works is to get any program number for a specific 
		 * port and the clients query the portmapper for the program number based on the requested
		 * port,
		 */
		if (setup_service(-1 /* first available prog# */,
					iodv1 /* version */,
					-1 /* both tcp and udp */,
					port /* specified port */,
					capfs_dispatch /* dispatch function */,
					&info) < 0) 
		{
			tp_cleanup_by_id(id);
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Could not fire up RPC service!\n");
			return -1;
		}
		/* Should not return */
		LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Panic! setup_service returned!\n");
		return 0;
	}
	else /* use a sockets based server */
	{
		fd_set readsock_set;
		int max, new_max;

		FD_ZERO(&aux_readsock_set);
		FD_ZERO(&global_readsock_set);
		FD_ZERO(&readsock_set);
		FD_SET(fd, &aux_readsock_set);
		FD_SET(fd, &global_readsock_set);
		FD_SET(fd, &readsock_set);
		new_max = 1 + fd;
		while (1) /* loop till killed */
		{
			int s, myerr, ret;
			struct sockaddr_in sanew;
			socklen_t salen = sizeof(sanew);
			struct hostent *hp;

			/* protect global_readsock_set */
			fd_lock();
			memcpy(&readsock_set, &global_readsock_set, sizeof(fd_set));
			fd_unlock();
			max = new_max;
			if ((ret = select(max, &readsock_set, NULL, NULL, NULL)) < 0)
			{
				if (errno != EBADF)
				{
					PERROR(SUBSYS_DATA, "select: CAS server bailing out...!");
					close(fd);
					return -1;
				}
			}
			else if (ret == 0 && errno == EINTR)
			{
				continue;
			}
			for (i = 0; i < max; i++)
			{
				if (FD_ISSET(i, &readsock_set))
				{
					/* new connection */
					if (i == fd)
					{
						if ((s = accept(fd,(struct sockaddr *) &sanew, &salen)) == -1) {
							myerr = errno;
							PERROR(SUBSYS_DATA, "new_request: accept");
							return -myerr;
						}
						if (s + 1 > new_max)
							new_max = s + 1;
						hp = gethostbyaddr((char *)&sanew.sin_addr.s_addr, 
												 sizeof(sanew.sin_addr),
												 sanew.sin_family);
						LOG(stderr, INFO_MSG, SUBSYS_DATA, "New connection on socket = [%d]. IP = [%s] host = [%s]\n",
								s, inet_ntoa(sanew.sin_addr), (hp == NULL) ? "Unknown" : hp->h_name);

						/* kill Nagle */
						if (set_tcpopt(s, TCP_NODELAY, 1) < 0) {
							myerr = errno;
							PERROR(SUBSYS_DATA, "set_tcpopt");
							close(s);
							return -myerr;
						}
						/* add them only if there is caching of client-side sockets */
						if (CAPFS_CAS_CACHE_HANDLES == 1)
						{
							fd_lock();
							/* set s in both aux_readsock_set and global_readsock_set in this critical section */
							FD_SET(s, &aux_readsock_set);
							FD_SET(s, &global_readsock_set);
							fd_unlock();
						}
						/* hand it over to one of our threads */
						tp_assign_work_by_id(id, capfs_iod_worker, (void *)s);
					}
					else /* data on a previously opened connection */
					{
						int status;

						fd_lock();
						status = FD_ISSET(i, &aux_readsock_set);
						fd_unlock();
						/* if no-one else is servicing req on this socket hand it off */
						if (!status)
						{
							int peek;
							char header[16];
							/* but still make sure there is data on the socket before handing it off */
							if ((peek = nbpeek(i, header, sizeof(struct cas_header)))  < 0)
							{
								fd_lock();
								FD_CLR(i, &aux_readsock_set);
								FD_CLR(i, &global_readsock_set);
								close(i);
								fd_unlock();
								LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [peek failed] %d due to %s\n",
										i, strerror(errno));
								continue;
							}
							else if (peek == 0) /* No data on this. so don't hand it off */
							{
								continue;
							}
							else
							{
								/* simply hand it off to one of our threads if no thread is servicing this currently */
								fd_lock();
								FD_SET(i, &aux_readsock_set);
								fd_unlock();
								tp_assign_work_by_id(id, capfs_iod_worker, (void *)i);
							}
						}
					}
				} /* end if-else-FD_ISSET */
			} /* end-for */
		} /* end while(1) */
		return -1; /* should never get here */
	} /* end else */
}

static void capfs_cleanup(void)
{
	if (use_sockets == 0)
	{
		/* cleanup the RPC service */
		cleanup_service(&info);
	}
	/* Clean up the thread pool */
	tp_cleanup_by_id(id);
	return;
}

int main(int argc, char **argv)
{
	/* initialize a spinlock */
	pthread_spin_init(&fdset_spinlock, 0);
	pmap_unset(CAPFS_IOD, iodv1);
	/* Start up the thread pool and register a service with the portmapper */
	if (capfs_init(argc, argv) < 0) {
		exit(1);
	}
	/* NOTREACHED */
	exit (0);
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
