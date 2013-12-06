/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/* This file contains CAPFS library call for open.   		*/
/* Should work to create and open files 						*/
#include <capfs-header.h>
#include <lib.h>
#include <signal.h>
#include <sockio.h>
#include <sockset.h>
#include <stdarg.h>
#include <errno.h>
#include <log.h>
#include "mgr.h"

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif

extern sockset socks;
extern jlist_p active_p;
extern int capfs_mode;

#ifndef CAPFS_NR_OPEN
#define CAPFS_NR_OPEN (NR_OPEN)
#endif

fdesc_p pfds[CAPFS_NR_OPEN] = {NULL};
int capfs_checks_disabled=0;
/* No need to check for registration. needed only for the capfs-kernel component */
int check_for_registration=0;

#define DEFAULT_ALLOC 8

static int capfs_open_called = 0;

static int send_open_req(const char* pathname, char *serverfilename,
	struct sockaddr *saddr, mreq_p);
int unix_open(const char *, int, mode_t, fpart_p);
int capfs_open(const char*, int, ...);
static int capfs_open_generic(const char* pathname, int flag, va_list ap);

/*
 * For a given open CAPFS file descriptor,
 * return the number of IOD nodes.
 */
int get_iod_count(int fd)
{
	if (fd < 0 || !pfds[fd]) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIB,  "get_iod_count: invalid fd (fd = %d)\n", fd);
		return(-1);
	}
#ifdef STRICT_DESC_CHECK
	if (pfds[fd]->checkval != FDESC_CHECKVAL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIB,  "get_iod_count: fdesc struct (fd = %d) trashed?\n", fd);
		return(-1);
	}
#endif
	if (pfds[fd]->fs != FS_CAPFS) {
		return(-1);
	}
	return pfds[fd]->fd.meta.p_stat.pcount; 
}

int capfs_open64(const char* pathname, int flags, ...)
{
	va_list ap;
	int ret = -1;

	va_start(ap, flags);

	ret = capfs_open_generic(pathname, flags | O_LARGEFILE, ap);

	va_end(ap);

	return(ret);
}

/* capfs_open() - CAPFS open command.
 * pathname - file to be opened
 * flag - flags (O_RDONLY, etc.) to be used in open
 * possible arguments that get handled by va_arg:
 * ------------------------------------------------
 * mode - if creating file, mode to be used.  Must also be specified
 *	   when providing meta or partation information, though it will be
 *	   ignored unless O_CREAT is set.
 * meta_p - metadata information.  O_META must be set to use this
 * part_p - partition information.  O_PART must be set to use this
 */
int capfs_open(const char* pathname, int flags, ...)
{
	va_list ap;
	int ret = -1;

	va_start(ap, flags);

	ret = capfs_open_generic(pathname, flags, ap);

	va_end(ap);

	return(ret);
}


/* capfs_open_generic()
 *
 * does the real work in open, after the va list and any 64 bit flags
 * have been setup by capfs_open() or capfs_open64().
 *
 * returns file descriptor on success, -1 on failure
 */

static int capfs_open_generic(const char* pathname, int flag, va_list ap)
{
	int 		i, fd, myerr, niodservers = 0;
	mreq 		req;
	struct sockaddr *saddr;
	char *fn;
	int64_t fs_ino;
	mode_t mode = 0;
	capfs_filestat* meta_p = NULL;
	fpart* part_p = NULL;

	if (capfs_open_called == 0) {
		atexit((void *)capfs_exit);
		capfs_open_called = 1;
	}

	memset(&req, 0, sizeof(req));

	/* we should look for mode in any of these cases */
	if(flag & O_CREAT || flag & O_META || flag & O_PART)
	{
		mode = va_arg(ap, mode_t);
	}

	/* the meta will be next if it is required */
	if(flag & O_META)
	{
		meta_p = va_arg(ap, capfs_filestat*);
	}

	/* and finally the partition */
	if(flag & O_PART)
	{
		part_p = va_arg(ap, fpart_p);
	}

#ifdef LARGE_FILE_SUPPORT
	flag |= O_LARGEFILE;
#endif

	if (!pathname) {
		errno = EFAULT;
		return(-1);
	}
	if (!capfs_checks_disabled) {
		/* Check to see if file is a CAPFS file */
		i = capfs_detect(pathname, &fn, &saddr, &fs_ino, NULL, FOLLOW_LINK);
		if (i < 0 || i > 2) /* error */ {
			PERROR(SUBSYS_LIB,"Error finding file");
			return(-1);
		}
	}
	else i = 0;

	if (i == 0) /* not a CAPFS file or directory */ {
		return(unix_open(pathname, flag, mode, part_p));
	}
	if (i == 2) /* CAPFS directory */ {
		if (flag & (O_WRONLY | O_RDWR)) {
			errno = EISDIR;
			return(-1);
		}

		if ((fd = open("/dev/null", O_RDONLY, 0)) < 0) {
			myerr = errno;
			PERROR(SUBSYS_LIB,"capfs_open: error opening /dev/null instead of metafile");
			errno = myerr;
			return(-1);
		}

		if (!(pfds[fd] = (fdesc_p)malloc(sizeof(fdesc)))) {
			myerr = errno;
			close(fd);
			errno = myerr;
			return(-1);
		}
		memset(pfds[fd], 0, sizeof(fdesc));

		/* save the file name in terms manager would understand */
		if (!(pfds[fd]->fn_p = (char *)malloc(strlen(fn)+1))) {
			myerr = errno;
			free(pfds[fd]);
			pfds[fd] = NULL;
			close(fd);
			errno = myerr;
			return(-1);
		}
		memset(pfds[fd]->fn_p, 0, strlen(fn)+1);
		strcpy(pfds[fd]->fn_p, fn);

#ifdef STRICT_DESC_CHECK
		pfds[fd]->checkval = FDESC_CHECKVAL;
#endif
		pfds[fd]->fs = FS_PDIR;
		pfds[fd]->part_p    = NULL;
		pfds[fd]->fd.off    = 0;
		pfds[fd]->fd.flag   = flag;
		pfds[fd]->fd.cap    = 0;
		pfds[fd]->fd.ref    = 1;

		/* store the manager connection info */
		memcpy(&pfds[fd]->fd.meta.mgr, saddr, sizeof(struct sockaddr));
		return(fd);
	}

	/* otherwise we have a CAPFS file... */
	/* Prepare request for file system */
	if (flag & O_META) {
		req.req.open.meta.p_stat.base = meta_p->base;
		niodservers = req.req.open.meta.p_stat.pcount = meta_p->pcount;
		if (niodservers < 0) niodservers = 0; /* pcount < 0 input is valid */
		req.req.open.meta.p_stat.ssize = meta_p->ssize;
	}
	else {
		req.req.open.meta.p_stat.base = -1;
		req.req.open.meta.p_stat.pcount = -1;
		req.req.open.meta.p_stat.ssize = -1;
		/*
		 * we on first call we'll use zero, then we'll store the # of
		 * iods from that call to use as the default # later.
		 */
		niodservers = 0;
	}

	req.uid = getuid();
	req.gid = getgid();
	req.type = MGR_OPEN;

	req.dsize = strlen(fn);
	req.req.open.flag = flag;
	req.req.open.mode  = mode;
	req.req.open.meta.fs_ino = fs_ino;
	req.req.open.meta.u_stat.st_uid = req.uid;
	req.req.open.meta.u_stat.st_gid = req.gid;
	req.req.open.meta.u_stat.st_mode = mode;
	req.req.open.ackdsize = 0;

	if (flag & O_APPEND) 
		req.req.open.flag = flag & ~(O_APPEND);

	if ((fd = send_open_req(pathname, fn, saddr, &req)) < 0) {
		return(-1);
	}
	/* set partition (if provided) */
	if (flag & O_PART) {
		if (!(pfds[fd]->part_p = malloc(sizeof(fpart)))) {
			PERROR(SUBSYS_LIB,"capfs_open: malloc");
			return(-1);
		}
		*(pfds[fd]->part_p) = *part_p;
	}
	if (capfs_mode == 0)
	{
		/* open all IOD connections (if requested) */
#ifndef __ALWAYS_CONN__
		if (flag & O_CONN) 
#else
		if (1) 
#endif
		{
			ireq iodreq;

			iodreq.majik_nr   = IOD_MAJIK_NR;
			iodreq.release_nr = CAPFS_RELEASE_NR;
			iodreq.type       = IOD_NOOP;
			iodreq.dsize      = 0;

			/* build job to send reqs and recv acks to/from iods */
			if (!active_p) active_p = jlist_new();
			initset(&socks); /* clear out the socket set */

			if (build_simple_jobs(pfds[fd], &iodreq) < 0) {
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  "capfs_open: build_simple_jobs failed...continuing\n");
				return(fd);
			}

			/* call do_job */
			while (!jlist_empty(active_p)) {
				if (do_jobs(active_p, &socks, -1) < 0) {
					LOG(stderr, WARNING_MSG, SUBSYS_LIB,  "capfs_open: do_jobs failed...continuing\n");
					return(fd); /* we'll let it slide for now */
				}
			}
			/* don't bother looking for errors */
		}
	}

	/* move to end of file (if necessary) */
	if (flag & O_APPEND) {
		capfs_lseek(fd, 0, SEEK_END);
	}

	/* tell the sockset library to start with random sockets if enabled */
	randomnextsock(1);

	return(fd);
}

int capfs_creat(const char* pathname, mode_t mode)
{
	int flags;
	
	/* call capfs_open with correct flags            */
	flags  =  O_CREAT|O_WRONLY|O_TRUNC;
	return(capfs_open(pathname, flags, mode, NULL, NULL)); 
}

/* send_open_req() - sends request to the manager, sets up FD structure,
 * returns file descriptor number
 *
 * servername is actually the filename in terms the server can
 * understand, not the name of the server.
 *
 * Returns -1 on error.
 */
static int send_open_req(const char* pathname, char *servername,
	struct sockaddr *saddr, mreq_p req_p)
{
	int fd, i;
	mack ack;
	iod_info *ptr = NULL;
	struct ackdata_c ackdata;
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;

	/* always just open /dev/null as a placeholder */
	if ((fd = open("/dev/null", O_RDONLY, 0)) < 0) {
		int myerr = errno;
		PERROR(SUBSYS_LIB,"capfs_open: error opening /dev/null instead of metafile");
		errno = myerr;
		return(-1);
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "Opening: %s\n", pathname);

	/* ensure we didn't over-run our CAPFS open file table */
	if (fd >= CAPFS_NR_OPEN) {
	    LOG(stderr, WARNING_MSG, SUBSYS_LIB,  "capfs_open: exceeded max CAPFS open files!\n");
	    close(fd);
	    return -1;
	}
	ptr = (iod_info *) calloc(sizeof(iod_info), CAPFS_MAXIODS);
	if (ptr == NULL) {
		LOG(stderr, WARNING_MSG, SUBSYS_LIB,  "capfs_open: could not allocate memory\n");
		close(fd);
		return -1;
	}
	/* library does not want hashes */
	ackdata.type = MGR_OPEN;
	ackdata.u.open.niods = CAPFS_MAXIODS;
	ackdata.u.open.iod = ptr;
	ackdata.u.open.nhashes = 0;
	ackdata.u.open.hashes = NULL;
	/* send request and receive ack; this will receive the iod info as
	 * well if we pass in a non-NULL ptr.
	 */
	memset(&ack, 0, sizeof(ack));
	if (send_mreq_saddr(&opt, saddr, req_p, servername, &ack, &ackdata) < 0) { 
		int my_err = errno;
		/* problems... */
		close(fd);
		free(ptr);
		errno = my_err;
		return -1;
	}
	if (ack.status != 0) {
		/* problems... */
		close(fd);
		free(ptr);
		errno = ack.eno;
		return -1;
	}
	pfds[fd] = (fdesc_p) malloc(sizeof(fdesc) + sizeof(iod_info) * (ack.ack.open.meta.p_stat.pcount - 1));
	if(!pfds[fd]) {
		close(fd);
		free(ptr);
		errno = ENOMEM;
		return -1;
	}
#ifdef STRICT_DESC_CHECK
	pfds[fd]->checkval  = FDESC_CHECKVAL;
#endif
	pfds[fd]->fs        = FS_CAPFS; /* CAPFS file type */
	pfds[fd]->fd.off    = 0;
	pfds[fd]->fd.flag   = req_p->req.open.flag;
	pfds[fd]->fd.meta   = ack.ack.open.meta;
	pfds[fd]->fd.cap    = ack.ack.open.cap;
	pfds[fd]->fd.ref    = 1;
	pfds[fd]->part_p    = NULL;
	pfds[fd]->fn_p      = NULL;
	memcpy(pfds[fd]->fd.iod, ptr, ack.dsize);
	free(ptr);

	for (i=0; i < pfds[fd]->fd.meta.p_stat.pcount; i++) {
#ifdef STRICT_DESC_CHECK
		pfds[fd]->fd.iod[i].checkval = IOD_INFO_CHECKVAL;
#endif
		/* dont connect to the iod's now. just add them to the iodinfo table */
		if((pfds[fd]->fd.iod[i].sock = instantiate_iod_entry((struct sockaddr *)&pfds[fd]->fd.iod[i].addr)) < 0) {
			int my_err = errno;
			PERROR(SUBSYS_LIB,"Could not instantiate iod entry!\n");
			close(fd);
			errno = my_err;
			return -1;
		}
		inc_ref_count(pfds[fd]->fd.iod[i].sock);
	}
	return(fd);
}

int unix_open(const char *pathname, int flag, mode_t mode, fpart_p part_p)
{
	int fd, myerr;
	capfs_filestat p_stat={0,1,8192,8192}; /* metadata */

	if ((fd = open(pathname, flag & ~CAPFSMASK, mode)) < 0) {
		return(fd);
	}
	if (!(pfds[fd] = (fdesc_p)malloc(sizeof(fdesc)))) {
		myerr = errno;
		close(fd);
		errno = myerr;
		return(-1);
	}
#ifdef STRICT_DESC_CHECK
	pfds[fd]->checkval = FDESC_CHECKVAL;
#endif
	pfds[fd]->fs = FS_UNIX;
	pfds[fd]->part_p    = NULL;
	pfds[fd]->fn_p      = NULL;
	pfds[fd]->fd.off    = 0;
	pfds[fd]->fd.flag   = flag;
	pfds[fd]->fd.cap    = 0;
	pfds[fd]->fd.ref    = 1;
	pfds[fd]->fd.meta.p_stat = p_stat; /* fill in some dummy values */
	/* set partition (if provided) */
	if (flag & O_PART) {
		if (!(pfds[fd]->part_p = malloc(sizeof(fpart)))) {
			myerr = errno;
			close(fd);
			errno = myerr;
			return(-1);
		}
		*(pfds[fd]->part_p) = *part_p;
	}
	return(fd);
}

#ifdef STRICT_DESC_CHECK
int do_fdesc_check(int fd)
{
	int i, err=0;
	if (fd < 0 || !pfds[fd]) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIB,  "do_fdesc_check: invalid fd (fd = %d)\n", fd);
		return(-1);
	}
	if (pfds[fd]->checkval != FDESC_CHECKVAL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIB,  "do_fdesc_check: fdesc struct (fd = %d) trashed?\n", fd);
		return(-1);
	}
	if (pfds[fd]->fs != FS_CAPFS) return(0);

	for (i=0; i < pfds[fd]->fd.meta.p_stat.pcount; i++) {
		if (pfds[fd]->fd.iod[i].checkval != IOD_INFO_CHECKVAL) {
			err = -1;
			LOG(stderr, CRITICAL_MSG, SUBSYS_LIB,  "do_fdesc_check: iod_info struct"
				" (fd = %d, iod = %d) trashed?\n", fd, i);
		}
	}
	return(err);
}

#endif

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
