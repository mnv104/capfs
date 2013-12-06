
#include "capfs-header.h"
#include "mgr_prot.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <syslog.h>
#include <memory.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <rpc/pmap_clnt.h>

#include "log.h"
#include "rpcutils.h"
#include "tp_proto.h"
#include "fslist.h"
#include "cas.h"
#include "capfs_config.h"
#include "mgr.h"

#define  _CAPFS_DISPATCH_FN(x) capfs_mgr_ ## x
#define  CAPFS_DISPATCH_FN(x)  _CAPFS_DISPATCH_FN(x)

int capfs_mode = 1;
static int is_daemon = 1;
/* Threads does not seem to work well with TCP-based RPC for some reason. Using UDP for now. */
static int use_tpool = 1;
static int mgr_port = MGR_REQ_PORT;
int default_ssize = DEFAULT_SSIZE;
static int num_threads = MGR_NUM_THREADS;
static pthread_attr_t attr;
static pthread_t  tid;
static struct svc_info info = {
use_thread: 0, /* it is important not to change this variable!!!! */
};
static tp_id id;

fslist_p active_p;

extern int send_req(iod_info iod[], int iods, int base, int pcount, ireq_p req_p, 
				void *data_p, iack_p ack_p);
static int capfs_init(int argc, char **argv);
static void capfs_cleanup(void);
extern void CAPFS_DISPATCH_FN(mgrv1)(struct svc_req *, register SVCXPRT *);

int stop_iods(void *v_p) 
{
	int i;
	ireq iodreq;
	fsinfo_p fs_p = (fsinfo_p) v_p;

	memset(&iodreq, 0, sizeof(iodreq));

	iodreq.majik_nr   = IOD_MAJIK_NR;
	iodreq.release_nr = CAPFS_RELEASE_NR;
	iodreq.type       = IOD_SHUTDOWN;
	iodreq.dsize      = 0;
	if (send_req(fs_p->iod, fs_p->nr_iods, 0, fs_p->nr_iods, &iodreq, 
		NULL, NULL) < 0) return(-1);
	for (i = 0; i < fs_p->nr_iods; i++) {
		close(fs_p->iod[i].sock);
		fs_p->iod[i].sock = -1;
	}
	return(0);
} 


static void mgr_shutdown(int sig_nr, siginfo_t *info, void *unused)
{
	if (capfs_mode == 0) {
		forall_fs(active_p, stop_iods);
		LOG(stderr, INFO_MSG, SUBSYS_META,  "Closed all iod connections\n");
	}
	LOG(stderr, INFO_MSG, SUBSYS_META, "Trying to cleanup capfs\n");
	capfs_cleanup();
	exit(1);
}

/* do_signal() - catch signals, reset handler, die on SIGSEGV */
static void do_signal(int sig_nr, siginfo_t *info, void *unused)
{
   char buffer[1024];
   
   LOG(stderr, CRITICAL_MSG, SUBSYS_META,   "Received signal=[%d]\n", sig_nr);
   
	LOG(stderr, CRITICAL_MSG, SUBSYS_META,   "\nOPEN FILES:\n");
	fslist_dump(active_p);

   if (sig_nr == SIGSEGV)
   {
      struct rlimit rlim;

      LOG(stderr, CRITICAL_MSG, SUBSYS_META,   "Current working directory: [%s]\n", 
          getcwd(buffer, sizeof(buffer)));
      LOG(stderr, CRITICAL_MSG, SUBSYS_META,   "pid: [%d]\n", getpid());
      if (getrlimit(RLIMIT_CORE, &rlim))
      {
         LOG(stderr, CRITICAL_MSG, SUBSYS_META,   "getrlimit() failed\n");
      }
      else
      {
         LOG(stderr, CRITICAL_MSG, SUBSYS_META,   "rlim_cur (RLIMIT_CORE): [%Ld]\n", rlim.rlim_cur);
         LOG(stderr, CRITICAL_MSG, SUBSYS_META,   "rlim_max (RLIMIT_CORE): [%Ld]\n", rlim.rlim_max);
      }
   }
	else if (sig_nr != SIGPIPE)
	{
		capfs_cleanup();
		exit(1);
	}
}

void restart(int sig_nr, siginfo_t *info, void *unused)
{
	LOG(stderr, INFO_MSG, SUBSYS_META,   "capfsmgr: received HUP signal\n");

	if (capfs_mode == 0) {
		forall_fs(active_p, stop_iods);
		LOG(stderr, INFO_MSG, SUBSYS_META, "Shut down all iods\n");
	}
	fslist_cleanup(active_p);
	active_p = fslist_new(); /* initialize new filesystem list */
	LOG(stderr, INFO_MSG, SUBSYS_META, "Cleared filesystem list\n");
	return;
} 

/*
int stop_idle_iods(void *v_p)
{
	int ret, ret2;
	fsinfo_p fs_p = (fsinfo_p) v_p;

	if (flist_empty(fs_p->fl_p)) {
		LOG(stderr, INFO_MSG, SUBSYS_META, "Closing idle iod connections\n");
		ret = stop_iods(v_p);
		ret2 = fs_rem(active_p, fs_p->fs_ino);
		return(MIN(ret, ret2));
	}
	return(0);
}

int timeout_fns()
{
	forall_fs(active_p, stop_idle_iods);
	return(0);
}
*/

static void usage(char *str)
{
	fprintf(stderr, "Usage: %s -c (don't use a thread pool) -n <number of threads>"
			" -t <timeout> -d {daemonize or not} "
			"-p <port> -b <default stripe size> -l<log level> -s {use sockets for cas servers}"
			"-o {operate in legacy/pvfs mode}\n", str);
	return;
}

static int startup(int argc, char **argv)
{
	struct sigaction handler;
	struct rlimit    limit;

	if (getuid() != 0 && geteuid() != 0) {
		fprintf(stderr,  "WARNING: mgr_prot_server should be run as root\n");
	}

	if (is_daemon) {
		int logfd, nullfd;
#ifndef CAPFS_LOG_DIR
		char logname[] = "/tmp/mgrlog.XXXXXX";

		if ((logfd = mkstemp(logname)) == -1) 
#else
		/* new, less obtuse log behavior */
		int logflags = O_APPEND|O_CREAT|O_RDWR;
		char logname[4096];		
		snprintf(logname, 4095, "%s/mgr", CAPFS_LOG_DIR);
#ifdef LARGE_FILE_SUPPORT
		logflags |= O_LARGEFILE;
#endif
		if ((logfd = open(logname, logflags, 0700)) == -1) 
#endif
		{
			if ((nullfd = open("/dev/null", O_RDWR)) == -1) {
				PERROR(SUBSYS_META,"capfsmgr: error opening log");
				return -1;
			}
			fprintf(stderr,  "couldn't create logfile [%s].\n", logname);

			/* ensure that 0-2 don't get used elsewhere */
			dup2(nullfd, 0);
			dup2(nullfd, 1);
			dup2(nullfd, 2);
			if (nullfd > 2) close(nullfd);
		}
		else {
			fchmod(logfd, 0755);
			dup2(logfd, 2);
			dup2(logfd, 1);
			close(0);
		}
		if (fork()) exit(0); /* fork() and kill parent */
		setsid();
	}	
	active_p = fslist_new(); /* initialize our file system information list */

	memset(&handler, 0, sizeof(struct sigaction));
	handler.sa_sigaction = (void *) mgr_shutdown;
	handler.sa_flags = SA_SIGINFO;
	/* set up SIGTERM handler to shut things down */
	if (sigaction(SIGTERM, &handler, NULL) != 0) {
		fprintf(stderr, "Could not setup signal handler for SIGTERM: %s\n", strerror(errno));
		return -1;
	}
	/* set up SIGINT handler to shut things down */
	if (sigaction(SIGINT, &handler, NULL) != 0) {
		fprintf(stderr, "Could not setup signal handler for SIGINT: %s\n", strerror(errno));
		return -1;
	}
	/* set up SIGHUP handler to restart the daemon */
	handler.sa_sigaction = (void *) restart;
	if (sigaction(SIGHUP, &handler, NULL) != 0) {
		fprintf(stderr, "Could not setup signal handler for SIGHUP: %s\n", strerror(errno));
		return -1;
	}
	/* catch SIGPIPE and SIGSEGV signals and log them, on SEGV we die */
	handler.sa_sigaction = (void *) do_signal;
	if (sigaction(SIGPIPE, &handler, NULL) != 0) {
		fprintf(stderr, "Could not setup signal handler for SIGPIPE: %s\n", strerror(errno));
		return -1;
	}
	if (sigaction(SIGSEGV, &handler, NULL) != 0) {
		fprintf(stderr, "Could not setup signal handler for SIGSEGV: %s\n", strerror(errno));
		return -1;
	}

	umask(0);
	chdir("/"); /* to avoid unnecessary busy filesystems */

	/* Increase the stack size, make it unlimited */
	limit.rlim_cur = RLIM_INFINITY;
	limit.rlim_max = RLIM_INFINITY;
	if (setrlimit(RLIMIT_STACK, &limit) != 0) {
		fprintf(stderr, "Warning! Could not setup ulimited stack size: %s\n", strerror(errno));
	}

	openlog("mgr", LOG_PID, LOG_ACC_FACILITY);

	return(0);
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
		fprintf(stderr,  "slave thread got invalid arguments!\n");
		capfs_cleanup();
		exit(1);
	}
	rqstp = sl_args->rqstp;
	xprt  = sl_args->transp;
	CAPFS_DISPATCH_FN(mgrv1)(rqstp, xprt);

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
		fprintf(stderr,  "calloc failed. exiting!\n");
		capfs_cleanup();
		exit(1);
	}
	sl_args->rqstp = (struct svc_req *)  calloc(1, sizeof(*rqstp));
	if (sl_args->rqstp == NULL) {
		free(sl_args);
		fprintf(stderr,  "calloc failed. exiting!\n");
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
	/*
	 * sl_args is freed by the slave thread upon its
	 * exit. Please do not free it up here.
	 */
	/* only if it is a udp-based xport we will use a thread pool */
	if (is_it_tcp == 0) {
		/* submit it to the thread pool */
		if (use_tpool) {
			int ret;
			typedef void *(*pthread_fn)(void *);

			if ((ret = tp_assign_work_by_id(id, (pthread_fn) capfs_slave_func, (void *)sl_args)) != 0) {
				fprintf(stderr, "Could not assign work to thread pool! %s\n", tp_strerror(ret));
				capfs_cleanup();
				exit(1);
			}
			return;
		}
		else { /* create a thread each time */
			typedef void *(*pthread_fn)(void *);

			pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
			if (pthread_create(&tid, &attr, (pthread_fn) capfs_slave_func, (void *)sl_args) != 0) {
				fprintf(stderr, "Could not create thread! %s\n", strerror(errno));
				capfs_cleanup();
				exit(1);
			}
			return;
		}
	}
	else {
		/* either tcp or something else, we service it ourselves */
		capfs_slave_func(sl_args);
	}
	return;
}

static int capfs_init(int argc, char **argv)
{
	int nLogLevel = CRITICAL_MSG | WARNING_MSG; 
	int opt, ret, timeout;
	extern int random_base;
	struct cas_options cas_options = {
doInstrumentation:0,
use_sockets:0,
	};
	static tp_info tinfo = {
tpi_count:MGR_NUM_THREADS,
tpi_name :NULL,
tpi_stack_size:-1, /* default stack size is good enough */
	};
#ifdef DEBUG
	nLogLevel |= INFO_MSG;
	nLogLevel |= DEBUG_MSG;
#endif
#ifdef __RANDOM_BASE__
	random_base = 1;
#endif

	while ((opt = getopt(argc, argv, "csn:t:dp:b:l:o")) != EOF) {
		switch (opt) {
			case 's':
				cas_options.use_sockets = 1;
				break;
			case 'c':
				/* create a thread each time. dont start up a tpool */
				use_tpool = 0;
				break;
			case 'n':
				num_threads = atoi(optarg);
				if (num_threads > 0) {
					tinfo.tpi_count = num_threads;
					/* force the use of a thread pool if possible */
					use_tpool = 1;
				}
				break;
			case 't':
				timeout = atoi(optarg);
				break;
			case 'd':
				is_daemon = 0;
				break;
			case 'p':
				mgr_port = atoi(optarg);
				break;
			case 'b':
				ret = atoi(optarg);
				if (ret > 0) default_ssize = ret;
				break;
			case 'l':
				nLogLevel = atoi(optarg);
				break;
			case 'o':
				capfs_mode = 0;
				break;
			default:
				usage(argv[0]);
				return -1;
		}
	}
	if (startup(argc, argv) < 0) {
		return -1;
	}
   set_log_level( nLogLevel );

	if (use_tpool) {
		fprintf(stderr, "---------- Starting CAPFS meta-server servicing RPCs using a thread pool [%d threads] ---------- \n", tinfo.tpi_count);
	}
	else {
		fprintf(stderr, "---------- Starting CAPFS meta-server servicing RPCs by creating a thread each time  ---------- \n");
	}
	/* Initialize the callback hash tables etc */
	if (cb_init() < 0) {
		fprintf(stderr,  "Could not initialize callback hash tables\n");
		return -1;
	}
	if (use_tpool) {
		/* Fire up a thread pool for servicing future requests */
		id = tp_init(&tinfo);
		if (id < 0) {
			cb_finalize();
			fprintf(stderr,  "Could not fire up thread pool!\n");
			return -1;
		}
	}
	/*
	 * Initialize the engine to talk to the CAS servers. Eventually, we would
	 * like to tunnel the garbage cleaner stuff through this interface.
	 */
	clnt_init(&cas_options, 1, CAPFS_CHUNK_SIZE);
	/* Start up the local RPC service on both TCP and UDP */
	if (setup_service(CAPFS_MGR /* prog# */,
				mgrv1 /* version */,
				-1 /* both tcp & udp */,
				mgr_port /* port */,
				capfs_dispatch, /* dispatch function */
				&info) < 0) 
	{
		cb_finalize();
		clnt_finalize();
		if (use_tpool) {
			tp_cleanup_by_id(id);
		}
		fprintf(stderr,  "Could not fire up RPC service!\n");
		return -1;
	}
	/* Should not return */
	fprintf(stderr,  "Panic! setup_service returned!\n");
	cb_finalize();
	clnt_finalize();
	if (use_tpool) {
		tp_cleanup_by_id(id);
	}
	return 0;
}

static void capfs_cleanup(void)
{
	cb_finalize();
	LOG(stderr, INFO_MSG, SUBSYS_META, "About to turn off RPC service\n");
	/* cleanup the RPC service */
	cleanup_service(&info);
	LOG(stderr, INFO_MSG, SUBSYS_META, "About to cleanup CAS Engine\n");
	/* cas engine cleanup */
	clnt_finalize();
	LOG(stderr, INFO_MSG, SUBSYS_META, "About to cleanup tpool\n");
	if (use_tpool) {
		/* Clean up the thread pool */
		tp_cleanup_by_id(id);
	}
	return;
}

/* Main function where the meta data server is fired up from */
int main (int argc, char **argv)
{
	srand(time(NULL));
	pmap_unset (CAPFS_MGR, mgrv1);
	/* Start up the thread pool and register a service with the portmapper */
	if (capfs_init(argc, argv) < 0) {
		exit(1);
	}
	/* NOTREACHED */
	exit (0);
}

