/*
 * capfsd.c copyright (c) 1999 Rob Ross and Phil Carns, all rights reserved.
 *
 * Heavily modified by Murali Vilayannur for CAPFS.
 * Copyright (C) vilayann@cse.psu.edu
 *
 * Written by Rob Ross and Phil Carns, funded by Scyld Computing.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Contact:  Rob Ross   rbross@parl.clemson.edu
 *           Phil Carns pcarns@parl.clemson.edu
 */

/* UNIX INCLUDES */
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <syscall.h>
#include <utime.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/sysmacros.h>

/* CAPFS INCLUDES */
#include "capfs_config.h"
#include "capfs_kernel_config.h"
#include "capfsd.h"
#include "ll_capfs.h"
#include "capfs_v1_xfer.h"

#include <pthread.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include "mgr_prot.h"
#include "rpcutils.h"
#include "capfsd_prot.h"
#include "hashes.h"
#include "cas.h"
#include "dcache.h"
#include "log.h"
#include "plugin.h"

#define  _CAPFS_DISPATCH_FN(x) capfs_capfsd_ ## x
#define  CAPFS_DISPATCH_FN(x)  _CAPFS_DISPATCH_FN(x)

/* PROTOTYPES */
static void do_signal(int signr, siginfo_t *, void *);
static void exiterror(char *string);
static int startup(int argc, char **argv);
static void cleanup(void);
static int open_capfsdev(void);
static int setup_capfsdev(void);
static int read_capfsdev(int fd, struct capfs_upcall *up, int timeout);
static int write_capfsdev(int fd, struct capfs_downcall *down, int timeout);
static void close_capfsdev(int fd);
static void init_downcall(struct capfs_downcall *down, struct capfs_upcall *up);
static int read_op(struct capfs_upcall *up, struct capfs_downcall *down);
static int write_op(struct capfs_upcall *up, struct capfs_downcall *down);
static void usage(void);
static int parse_devices(const char *targetfile, const char *devname, 
	int *majornum);

#ifdef MORE_FDS
static int get_filemax(void);
static int set_filemax(int max);
#endif

/* operate as CAPFS by default */
int capfs_mode = 1;

/* GLOBALS */
#define CAPFSD_NUM_THREADS 5
static int is_daemon = 1;
static int dev_fd;
char *orig_iobuf = NULL, *big_iobuf = NULL;
int capfs_debug = CAPFS_DEFAULT_DEBUG_MASK;
static int num_threads = CAPFSD_NUM_THREADS;
/* Local RPC service must be a separate thread */
static struct svc_info info = {
use_thread: 1,
};

extern void CAPFS_DISPATCH_FN(clientv1)(struct svc_req *, register SVCXPRT *);
extern int check_for_registration;

/* plugin interface functions */
extern void capfsd_plugin_init(void);
extern void capfsd_plugin_cleanup(void);

static int capfs_opt_io_size = 0, capfs_dent_size = 0, capfs_link_size = 0;

static inline long ROUND_UP(long size)
{
	static int page_size = 0, page_mask = 0;

	if (page_size == 0) {
		page_size = sysconf(_SC_PAGE_SIZE);
		page_mask = ~(page_size - 1);
	}
	return (size + page_size - 1) & page_mask;
}

int main(int argc, char **argv)
{
	int err;
	struct capfs_upcall up;
	struct capfs_downcall down;
	struct capfs_dirent *dent = NULL;
	char *link_name = NULL;
	int opt = 0;
	int capfsd_log_level = CRITICAL_MSG | WARNING_MSG;
	char options[256];
	struct cas_options cas_options = {
doInstrumentation:0,
use_sockets:0,
	};

#ifdef DEBUG
	capfsd_log_level |= INFO_MSG;
	capfsd_log_level |= DEBUG_MSG;
#endif
	set_log_level(capfsd_log_level);
	/* capfsd must register a callback with the meta-data server at the time of mount */
	check_for_registration = 1;
	while((opt = getopt(argc, argv, "dhsn:p:")) != EOF) {
		switch(opt){
			case 's':
				cas_options.use_sockets = 1;
				break;
			case  'd':
				is_daemon = 0;
				break;
			case 'p':
				err = sscanf(optarg, "%x", &capfs_debug);
				if(err != 1){
					usage();
					exiterror("bad arguments");
					exit(1);
				}
				break;
			case 'n':
				num_threads = atoi(optarg);
				break;
			case 'h':
				usage();
				exit(0);
			case '?':
			default:
				usage();
				exiterror("bad arguments");
				exit(1);
		}
	}
		
	if (getuid() != 0 && geteuid() != 0) {
		exiterror("must be run as root");
		exit(1);
	}

	if (setup_capfsdev() < 0) {
		exiterror("setup_capfsdev() failed");
		exit(1);
	}

	if ((dev_fd = open_capfsdev()) < 0) {
		exiterror("open_capfsdev() failed");
		exit(1);
	}
	
	startup(argc, argv);
	/* Initialize the plugin interface */
	capfsd_plugin_init();

	capfs_comm_init();


	/* allocate a 64K, page-aligned buffer for small operations */
	capfs_opt_io_size = ROUND_UP(CAPFS_OPT_IO_SIZE);
	if ((orig_iobuf = (char *) valloc(capfs_opt_io_size)) == NULL) {
		exiterror("calloc failed");
		capfsd_plugin_cleanup();
		exit(1);
	}
	memset(orig_iobuf, 0, capfs_opt_io_size);
	capfs_dent_size = ROUND_UP((FETCH_DENTRY_COUNT * sizeof(struct capfs_dirent)));
	/* allocate a suitably large dent buffer for getdents speed up */
	if ((dent = (struct capfs_dirent *) valloc(capfs_dent_size)) == NULL) {
		exiterror("calloc failed");
		capfsd_plugin_cleanup();
		exit(1);
	}
	memset(dent, 0, capfs_dent_size);
	/* maximum size of a link target cannot be > 4096 */
	capfs_link_size = ROUND_UP(4096);
	link_name = (char *) valloc(capfs_link_size);
	if(!link_name) {
		exiterror("calloc failed");
		capfsd_plugin_cleanup();
		exit(1);
	}
	memset(link_name, 0, capfs_link_size);
	
	fprintf(stderr, "------------ Starting client daemon servicing VFS requests using a thread pool [%d threads] ----------\n",
			num_threads);
	/*
	 * Start up the local RPC service on both TCP/UDP 
	 * for callbacks.
	 */
	pmap_unset(CAPFS_CAPFSD, clientv1);
	if (setup_service(CAPFS_CAPFSD /* program number */,
				clientv1 /* version */,
				-1 /* both tcp/udp */,
				-1 /* any available port */,
				CAPFS_DISPATCH_FN(clientv1) /* dispatch routine */,
				&info) < 0) {
		exiterror("Could not setup local RPC service!\n");
		capfsd_plugin_cleanup();
		exit(1);
	}
	/*
	 * Initialize the hash cache.
	 * Note that we are using default values of cache sizes,
	 * and this should probably be an exposed knob to the user.
	 * CMGR_BSIZE is == CAPFS_MAXHASHLENGTH for SHA1-hash. So we dont need to set
	 * that. We use environment variables to communicate the parameters
	 * to the caches.
	 */
	snprintf(options, 256, "%d", CAPFS_CHUNK_SIZE);
	setenv("CMGR_CHUNK_SIZE", options, 1);
	snprintf(options, 256, "%d", CAPFS_HCACHE_COUNT);
	setenv("CMGR_BCOUNT", options, 1);
	init_hashes();
#if 0
	/*
	 * Initialize the client-side data cache.
	 * Note that we are not using this layer
	 * right now. It is getting fairly complicated already.
	 */
	snprintf(options, 256, "%d", CAPFS_DCACHE_BSIZE);
	setenv("CMGR_BSIZE", options, 1);
	snprintf(options, 256, "%d", CAPFS_DCACHE_COUNT);
	setenv("CMGR_BCOUNT", options, 1);
#endif
	/*
	 * Initialize the client-side data server communication
	 * stuff.
	 */
	clnt_init(&cas_options, num_threads, CAPFS_CHUNK_SIZE);
	
	/* loop forever, doing:
	 * - read from device
	 * - service request
	 * - write back response
	 */
	for (;;) {
		struct timeval begin, end;

		err = read_capfsdev(dev_fd, &up, 30);
		if (err < 0) {
			/* cleanup the hash cache */
			cleanup_hashes();
			/* Cleanup the RPC service */
			cleanup_service(&info);
			capfs_comm_shutdown();
			close_capfsdev(dev_fd);
			/* cleanup the plugins */
			capfsd_plugin_cleanup();
			/* cleanup the client-side stuff */
			clnt_finalize();
			exiterror("read failed\n");
			exit(1);
		}
		if (err == 0) {
			/* timed out */
			capfs_comm_idle();
			continue;
		}
		gettimeofday(&begin, NULL);
		/* the do_capfs_op() call does this already; can probably remove */
		init_downcall(&down, &up);

		err = 0;
		switch (up.type) {
			/* all the easy operations */
		case GETMETA_OP:
		case SETMETA_OP:
		case LOOKUP_OP:
		case CREATE_OP:
		case REMOVE_OP:
		case RENAME_OP:
		case SYMLINK_OP:
		case MKDIR_OP:
		case RMDIR_OP:
		case STATFS_OP:
		case HINT_OP:
		case FSYNC_OP:
		case LINK_OP:
		{
			PDEBUG(D_UPCALL, "read upcall; type = %d, name = %s\n", up.type,
					 up.v1.fhname);
			err = do_capfs_op(&up, &down);
			if (err < 0) {
				PDEBUG(D_LIB, "do_capfs_op failed for type %d\n", up.type);
			}
			break;
			/* the more interesting ones */
		}
		case GETDENTS_OP:
			/* need to pass location and size of buffer to do_capfs_op() */
			up.xfer.ptr = dent;
			up.xfer.size = capfs_dent_size;
			err = do_capfs_op(&up, &down);
			if (err < 0) {
				PDEBUG(D_LIB, "do_capfs_op failed for getdents\n");
			}
			break;
		case READLINK_OP:
			/* need to pass location and size of buffer to hold the target name */
			up.xfer.ptr = link_name;
			up.xfer.size = capfs_link_size;
			err = do_capfs_op(&up, &down);
			if(err < 0) {
				PDEBUG(D_LIB, "do_capfs_op failed for readlink\n");
			}
			break;
		case READ_OP:
			err = read_op(&up, &down);
			if (err < 0) {
				PDEBUG(D_LIB, "read_op failed\n");
			}
			break;
		case WRITE_OP:
			err = write_op(&up, &down);
			if (err < 0) {
				PDEBUG(D_LIB, "do_capfs_op failed\n");
			}
			break;
			/* things that aren't done yet */
		default:
			err = -ENOSYS;
			break;
		}
		gettimeofday(&end, NULL);
		/* calculate the total time spent servicing this call */
		if (end.tv_usec < begin.tv_usec) {
			end.tv_usec += 1000000;
			end.tv_sec--;
		}
		end.tv_sec -= begin.tv_sec;
		end.tv_usec -= begin.tv_usec;
		down.total_time = (end.tv_sec * 1000000 + end.tv_usec);
		down.error = err;

		switch(up.type)
		{
		case HINT_OP:
			/* this is a one shot hint, we don't want a response in case of HINT_OPEN/HINT_CLOSE */
			if (up.u.hint.hint == HINT_CLOSE || up.u.hint.hint == HINT_OPEN) {
				err = 0;
				break;
			}
			/* fall through */
		default:
			/* the default behavior is to write a response to the device */
			err = write_capfsdev(dev_fd, &down, -1);
			if (err < 0) {
				/* cleanup the hash cache */
				cleanup_hashes();
				/* Cleanup the RPC service */
				cleanup_service(&info);
				capfs_comm_shutdown();
				close_capfsdev(dev_fd);
				/* Cleanup the plugins */
				capfsd_plugin_cleanup();
				/* cleanup the client-side stuff */
				clnt_finalize();
				exiterror("write failed");
				exit(1);
			}
			break;
		}

		/* If we used a big I/O buffer, free it after we have successfully
		 * returned the downcall.
		 */
		if (big_iobuf != NULL) {
			free(big_iobuf);
			big_iobuf = NULL;
		}
	}
	/* Not reached */
	/* cleanup the hash cache */
	cleanup_hashes();
	/* Cleanup the RPC service */
	cleanup_service(&info);
	capfs_comm_shutdown();
	close_capfsdev(dev_fd);
	/* cleanup the plugins */
	capfsd_plugin_cleanup();
	/* cleanup the client-side stuff */
	clnt_finalize();
	exit(1);
}

/* read_op()
 *
 * Returns 0 on success, -errno on failure.
 */
static int read_op(struct capfs_upcall *up, struct capfs_downcall *down)
{
	int err;

	if (up->u.rw.io.type != IO_CONTIG) return -EINVAL;

	if (up->u.rw.io.u.contig.size <= CAPFS_OPT_IO_SIZE) {
		/* use our standard little buffer */
		up->xfer.ptr = orig_iobuf;
		up->xfer.size = up->u.rw.io.u.contig.size;
	}
	else {
		/* need a big buffer; this is freed in main */
		if ((big_iobuf = (char *) valloc(up->u.rw.io.u.contig.size)) == NULL)
			return -errno;
		memset(big_iobuf, 0, up->u.rw.io.u.contig.size);

		up->xfer.ptr = big_iobuf;
		up->xfer.size = up->u.rw.io.u.contig.size;
	}

	err = do_capfs_op(up, down);
	if (err < 0) {
		PDEBUG(D_LIB, "do_capfs_op failed\n");
	}
	
	return err;
}

/* write_op()
 *
 * Returns 0 on success, -errno on failure.
 */
static int write_op(struct capfs_upcall *up, struct capfs_downcall *down)
{

	int err;
	struct capfs_downcall write_down;

	if (up->u.rw.io.type != IO_CONTIG) return -EINVAL;

	if (up->u.rw.io.u.contig.size <= CAPFS_OPT_IO_SIZE) {
		/* use our standard little buffer */
		up->xfer.ptr = orig_iobuf;
		up->xfer.size = up->u.rw.io.u.contig.size;
	}
	else {
		/* need a big buffer; this is freed in main */
		if ((big_iobuf = (char *) valloc(up->u.rw.io.u.contig.size)) == NULL)
			return -errno;
		memset(big_iobuf, 0, up->u.rw.io.u.contig.size);
		up->xfer.ptr = big_iobuf;
		up->xfer.size = up->u.rw.io.u.contig.size;
	}

	/* NOTE:
	 * The write process requires an extra downcall, sent here, to tell
	 * the kernel where to put the data (in our user-space) so we can
	 * write it to the file system.  Here we setup the downcall to indicate
	 * the location of the capfsd write buffer, then send it away.
	 *
	 * The second downcall is performed back up in main() after we return from
	 * this function, and it indicates the result of the CAPFS request.  
    */
	init_downcall(&write_down, up);
	write_down.xfer.ptr = up->xfer.ptr;
	write_down.xfer.size = up->xfer.size;
	
	err = write_capfsdev(dev_fd, &write_down, -1);
	if (err < 0) {
		PDEBUG(D_DOWNCALL, "write_capfsdev for write buffer failed\n");
		return err;
	}

	err = do_capfs_op(up, down);
	if (err < 0) {
		PDEBUG(D_LIB, "do_capfs_op failed\n");
	}

	return err;
}

/* startup()
 *
 * Handles mundane tasks of setting up logging, becoming a daemon, and
 * initializing signal handlers.
 */
static int startup(int argc, char **argv)
{
	struct sigaction handler;
#ifdef MORE_FDS
	int filemax;
	struct rlimit lim;
#endif

	if (is_daemon) {
		int logfd;
#ifndef CAPFS_LOG_DIR
		/* old behavior */
		char logname[] = "/tmp/capfsdlog.XXXXXX";

		if ((logfd = mkstemp(logname)) == -1) 
#else
		/* new, less obtuse behavior */
		char logname[4096];

		snprintf(logname, 4095, "%s/capfsd", CAPFS_LOG_DIR);
		if ((logfd = open(logname, O_APPEND|O_CREAT|O_RDWR, 0700)) == -1) 
#endif
		{
			PDEBUG(D_SPECIAL, "couldn't create logfile...continuing...\n");
			close(0); close(1); close(2);
		}
		else {
			fchmod(logfd, 0755);
			dup2(logfd, 2);
			dup2(logfd, 1);
			close(0);
		}
		if (fork()) {
			exit(0); /* fork() and kill parent */
		}
		setsid();
	}	

#ifdef MORE_FDS
	/* Try to increase number of open FDs.
	 *
	 * NOTE:
	 * The system maximum must be increased in order for us to be able to
	 * take advantage of an increase for this process.  This value is
	 * stored in /proc/sys/fs/file-max and is manipulated here with the
	 * get_filemax() and set_filemax() functions.
	 *
	 * NONE OF THIS CODE IS ANY GOOD UNTIL THE UNDERLYING TRANSPORT IS
	 * BETTER.  Specifically the sockset code needs to utilize larger
	 * numbers of FDs, as well as the code that associates sockets with
	 * files in the job code.  I'm going to leave this code here, but
	 * it's useless for the moment.
	 */
	if ((filemax = get_filemax()) < 0) {
		PERROR( "warning: get_filemax failed\n");
	}
	/* let's make sure there are plenty of FDs to go around */
	else if (filemax < 2*CAPFSD_NOFILE) {
		if ((filemax = set_filemax(2*CAPFSD_NOFILE)) < 0) {
			PERROR( "warning: set_filemax failed\n");
		}
	}
	/* now we take care of the per-process limits */
	if (getrlimit(RLIMIT_NOFILE, &lim) < 0) {
		PERROR( "warning: getrlimit failed\n");
	}
	else {
		lim.rlim_cur=(lim.rlim_cur<CAPFSD_NOFILE) ? CAPFSD_NOFILE : lim.rlim_cur;
		lim.rlim_max=(lim.rlim_max<CAPFSD_NOFILE) ? CAPFSD_NOFILE : lim.rlim_max;
		if (setrlimit(RLIMIT_NOFILE, &lim) < 0) {
			PERROR( "warning: setrlimit failed\n");
		}
	}
#endif

	/* change working dir to avoid unnecessary busy file systems */
	if (chdir("/") != 0) {
		exiterror("could not change working directory to /\n");
		exit(1);
	}

	memset(&handler, 0, sizeof(struct sigaction));
	handler.sa_sigaction = (void *) do_signal;
	handler.sa_flags = SA_SIGINFO;
	/* set up SIGINT handler to shut things down */
	if (sigaction(SIGINT, &handler, NULL) != 0) {
		exiterror("Could not setup signal handler for SIGINT");
		exit(1);
	}
	/* set up SIGTERM handler to shut things down */
	if (sigaction(SIGTERM, &handler, NULL) != 0) {
		exiterror("Could not setup signal handler for SIGTERM");
		exit(1);
	}
	/* set up SIGHUP handler to restart the daemon */
	if (sigaction(SIGHUP, &handler, NULL) != 0) {
		exiterror("Could not setup signal handler for SIGHUP");
		exit(1);
	}
	/* catch SIGPIPE and SIGSEGV signals and log them, on SEGV we die */
	if (sigaction(SIGPIPE, &handler, NULL) != 0) {
		exiterror("Could not setup signal handler for SIGPIPE");
		exit(1);
	}
	if (sigaction(SIGSEGV, &handler, NULL) != 0) {
		exiterror("Could not setup signal handler for SIGSEGV");
		exit(1);
	}

	return 0;
} /* end of startup() */

static void do_signal(int signr, siginfo_t *sinfo, void *unused)
{
	printf("%ld Got signal %d\n", pthread_self(), signr);
	switch (signr) {
		case SIGINT:
		case SIGTERM:
			/* cleanup the hash cache */
			cleanup_hashes();
			/* Cleanup the RPC service */
			cleanup_service(&info);
			/* Clean up the plugins */
			capfsd_plugin_cleanup();
			/* cleanup the client-side stuff */
			clnt_finalize();
			cleanup();
			exiterror("caught SIGTERM. exiting gracefully\n");
			exit(1);
		case SIGHUP:
			PERROR( "caught SIGHUP;  Reinitializing and/or adding new plugins\n");
			/* This should hopefully find out if there are any plugins and initialize them... */
			capfsd_plugin_init();
			break;
		case SIGPIPE:
			PERROR( "caught SIGPIPE; continuing\n");
			break;
		case SIGSEGV:
			/* cleanup the hash cache */
			cleanup_hashes();
			/* Cleanup the RPC service */
			cleanup_service(&info);
			capfs_comm_shutdown();
			close_capfsdev(dev_fd);
			/* Clean up the plugins */
			capfsd_plugin_cleanup();
			/* cleanup the client-side stuff */
			clnt_finalize();
			exiterror("caught SIGSEGV\n");
			exit(1);
		default:
			/* cleanup the hash cache */
			cleanup_hashes();
			/* Cleanup the RPC service */
			cleanup_service(&info);
			capfs_comm_shutdown();
			close_capfsdev(dev_fd);
			/* Clean up the plugins */
			capfsd_plugin_cleanup();
			/* cleanup the client-side stuff */
			clnt_finalize();
			exiterror("caught unexpected signal\n");
			exit(1);
	}
}

static void exiterror(char *string)
{
	fprintf(stderr, "capfsd exiting: %s\n", string);
}

static void cleanup(void)
{
	/* two calls to capfs_comm_idle() will close everything */
	capfs_comm_idle();
	capfs_comm_idle();
} /* end of cleanup() */


#ifdef MORE_FDS
/* SYSTEM-WIDE OPEN FILE MAXIMUM MANIPULATION CODE */

/* get_filemax(void)
 *
 * Returns the maximum number of files open for the system
 */
static int get_filemax(void)
{
	FILE *fp;
	int max;
	char lnbuf[1024];

	if ((fp = fopen("/proc/sys/fs/file-max", "r")) < 0)
		return -errno;

	fgets(lnbuf, 1023, fp);
	fclose(fp);
	max = atoi(lnbuf);
	return max;
}


/* set_filemax(int max)
 *
 * Returns new maximum on success.
 */
static int set_filemax(int max)
{
	FILE *fp;

	if ((fp = fopen("/proc/sys/fs/file-max", "w")) < 0)
		return -errno;

	if (max > 1048575) max = 1048575; /* keep things a little sane */
	fprintf(fp, "%d\n", max);
	fclose(fp);
	return get_filemax();
}
#endif



/* DEVICE MANIPULATION CODE */

/* open_capfsdev() - opens the CAPFS device file
 *
 * Returns FD on success, -1 on failure.
 */
static int open_capfsdev(void)
{
	int fd;
	char fn[] = "/dev/capfsd";

	if ((fd = open(fn, O_RDWR)) < 0) {
		if (errno == ENOENT) {
			PERROR( "Could not open /dev/capfsd\n");
		}
		else if (errno == ENODEV) {
			PERROR( "need to load capfs module\n");
		}
		else if (errno == EBUSY) {
			PERROR( "capfsd already running?\n");
		}
		else {
			PDEBUG(D_PSDEV, "failed to open device file\n");
		}
		return -1;
	}

	return fd;
}

/* read_capfsdev(fd, up, timeout) - reads a capfs_upcall from the CAPFS device
 *
 * timeout - number of seconds to wait for ready to read.  If timeout is
 *   less than zero, then the call will block until data is available.
 *
 * Returns sizeof(*up) on success, 0 on timeout, -1 on failure.
 *
 * NOTES:
 * This implementatio uses a select() first to block until the device is
 * ready for reading.  Currently the read() function implementation for
 * the device is nonblocking, so we must do this.
 */
static int read_capfsdev(int fd, struct capfs_upcall *up, int timeout)
{
	fd_set readfds;
	int ret;
	struct timeval tv;

read_capfsdev_select_restart:
	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);
	if (timeout >= 0) {
		tv.tv_sec = timeout;
		tv.tv_usec = 0;
		ret = select(fd+1, &readfds, NULL, NULL, &tv);
	}
	else ret = select(fd+1, &readfds, NULL, NULL, NULL);

	if (ret < 0) {
		if (errno == EINTR) goto read_capfsdev_select_restart;

		PERROR( "fatal error occurred selecting on device, errno = %d\n", errno);
		return -1;
	}
	
	if (ret == 0) return 0; /* timed out */

	PDEBUG(D_UPCALL, "reading from device\n");
	
read_capfsdev_read_restart:
	if ((ret = read(fd, up, sizeof(*up))) < 0) {
		if (errno == EINTR) goto read_capfsdev_read_restart;

		PERROR( "fatal error occurred reading from device, errno = %d\n", errno);
		return -1;
	}

	if(ret == 0)
	{
		goto read_capfsdev_read_restart;
	}

	if (up->magic != CAPFS_UPCALL_MAGIC) {
		PDEBUG(D_UPCALL, "magic number not valid in upcall\n");
		return -1;
	}

	return sizeof(*up);
}

/* write_capfsdev() - writes a capfs_downcall to the CAPFS device
 *
 * Returns sizeof(*down) on success, 0 on timeout, -1 on failure.
 *
 * TODO: IMPLEMENT TIMEOUT FEATURE IF IT IS EVER NEEDED.
 * 
 * NOTES:
 * Timeout isn't implemented at this time.
 */
static int write_capfsdev(int fd, struct capfs_downcall *down, int timeout)
{
	int ret;


	if (down->magic != CAPFS_DOWNCALL_MAGIC) {
		PDEBUG(D_DOWNCALL, "magic number not valid in downcall\n");
		return -1;
	}

	PDEBUG(D_PSDEV, "writing to device\n");

write_capfsdev_restart:
	if ((ret = write(fd, down, sizeof(*down))) < 0) {
		if (errno == EINTR) goto write_capfsdev_restart;

		PERROR( "fatal error occurred writing to device, errno = %d\n", errno);
		return -1;
	}

	return sizeof(*down);
}

/* close_capfsdev() - closes the CAPFS device file
 *
 */
static void close_capfsdev(int fd)
{
	close(fd);

	return;
}


/* init_downcall() - initialize a downcall structure
 */
static void init_downcall(struct capfs_downcall *down, struct capfs_upcall *up)
{
	memset(down, 0, sizeof(*down));
	down->magic = CAPFS_DOWNCALL_MAGIC;
	down->seq = up->seq;
	down->type = up->type;
}

static void usage(void){
	printf("\n");
	printf("Usage: capfsd [OPTIONS]\n");
	printf("Starts CAPFS client side daemon.\n");
	printf("\n");
	printf("\t-s {Use sockets to connect to data-servers\n");
	printf("\t-d {dont run as daemon}\n");
	printf("\t-p <client/vfs interaction debugging level in hex>   (increases amount of capfsd logging)\n");
	printf("\t-n <number of threads in the thread pool>\n");
	printf("\t-h                            (show this help screen)\n");
	printf("\n");
	return;
}

/* setup_capfsdev()
 *
 * sets up the capfsd device file
 *
 * returns 0 on success, -1 on failure
 */
static int setup_capfsdev(void)
{
	int majornum = -1;
	int ret = -1;
	struct stat dev_stat;
	char dev_name[] = "/dev/capfsd";

	ret = parse_devices("/proc/devices", "capfsd", &majornum);
	if(ret < 0){
		PERROR( "Unable to parse device file.\n");
		return(-1);
	}
	if(majornum == -1)
	{
		PERROR( "Could not setup device %s.\n", dev_name);
		PERROR( "Did you remember to load the capfs module?\n");
		return(-1);
	}

	if(!access(dev_name, F_OK))
	{
		/* device file already exists */
		ret = stat(dev_name, &dev_stat);
		if(ret != 0)
		{
			PERROR( "Could not stat %s.\n", dev_name);
			return(-1);
		}
		if(S_ISCHR(dev_stat.st_mode) && (major(dev_stat.st_rdev) == majornum))
		{
			/* the device file already has the correct major number; we're 
			 * done
			 */
			return(0);
		}
		else
		{
			/* the device file is incorrect; unlink it */
			ret = unlink(dev_name);
			if(ret != 0)
			{
				PERROR( "Could not unlink old %s\n", dev_name);
				return(-1);
			}
		}
	}

	/* if we hit this point, then we need to create a new device file */
	ret = mknod(dev_name, (S_IFCHR | S_IRUSR | S_IWUSR) , makedev(majornum, 0));
	if(ret != 0)
	{
		PERROR( "Could not create new %s device entry.\n", dev_name);
		return(-1);
	}

	return(0);
}

/* parse_devices()
 *
 * parses a file in the /proc/devices format looking for an entry for the
 * given "devname".  If found, "majornum" is filled in with the major number of
 * the device.  Else "majornum" is set to -1.
 *
 * returns 0 on successs, -1 on failure
 */
static int parse_devices(const char* targetfile, const char* devname, 
	int* majornum)
{
	int max_str_len = 256;
	char line_buf[max_str_len];
	char dev_buf[max_str_len];
	int major_buf = -1;
	FILE* devfile = NULL;
	int ret = -1;

	/* initialize for safety */
	*majornum = -1;

	/* open up the file to parse */
	devfile = fopen(targetfile, "r");
	if(!devfile){
		PERROR( "Could not open %s.\n", targetfile);
		return(-1);
	}

	/* scan every line until we get a match or end of file */
	while(fgets(line_buf, sizeof(line_buf), devfile)){
		/* sscanf is safe here as long as the target string is at least as
		 * large as the source */
		ret = sscanf(line_buf, " %d %s ", &major_buf, dev_buf);
		if(ret == 2){
			/* this line is the correct format; see if it matches the
			 * devname */
			if(strncmp(devname, dev_buf, max_str_len) == 0)
			{
				*majornum = major_buf;
				/* don't break out; it doesn't cost much to scan the whole
				 * thing, and we want the last entry if somehow(?) there are two
				 */
			}
		}
	}

	fclose(devfile);
	return(0);
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
