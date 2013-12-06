/*
 * Heavily modified by Murali Vilayannur to support RPCification
 * and Multi-threaded behaviour for CAPFS.
 * 	- vilayann@cse.psu.edu
 *
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 *
 * Credits:
 *   23 Aug 01: Pete Wyckoff <pw@osc.edu>: Architecture-indepdent getdents
 *     and stat changes.
 */
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>
#include <syscall.h>
#include <sys/syscall.h>
#include <utime.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <syslog.h>
#include <grp.h>
#include <pwd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <rpc/rpc.h>
#include <limits.h>

/* CAPFS INCLUDES */
#include "capfs-header.h"
#include "mgr.h"
#include "metaio.h"
#include "dfd_set.h"
#include "capfs_config.h"
#include "log.h"
#include "cas.h"
#include "sha.h"
#include "mgr_prot.h"
#include "bit.h"

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif

#ifndef LLONG_MAX
#define LLONG_MAX 9223372036854775807LL
#endif

#if 0
static fsinfo_p iclose_fsp; /* used with implicit closes only */
static int iclose_sock;
#endif
int random_base = 0;

static int do_noop(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_chmod(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_chown(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_access(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_truncate(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_ctime(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_utime(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_fstat(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_stat(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_lookup(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_statfs(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_unlink(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_close(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_mount(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_open(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_umount(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) __attribute__((unused));
static int do_shutdown(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_rename(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_link(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_readlink(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_iod_info(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_mkdir(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_fchown(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_fchmod(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_rmdir(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_getdents(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_gethashes(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);
static int do_wcommit(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p);

static int send_open_ack(mreq_p req_p, mack_p ack_p, fsinfo_p fs_p, int cap, struct ackdata *ackdata_p);
static fsinfo_p quick_mount(char *fname, int uid, int gid, mack_p ack_p, struct ackdata *ackdata_p);
#if 0
static int do_all_implicit_closes(int sock);
static int do_implicit_closes(void *v_p);
static int check_for_implicit_close(void *v_p);
#endif

/* GLOBALS */
static int (*reqfn[])(mreq_p, void *, mack_p, struct ackdata *) = {
	do_chmod,
	do_chown,
	do_close,
	do_stat, /*really this is do_lstat*/
	do_noop,
	do_open,
	do_unlink,
	do_shutdown,
	do_noop,
	do_fstat,
	do_rename,
	do_iod_info,
	do_mkdir,
	do_fchown,
	do_fchmod,
	do_rmdir,
	do_access,
	do_truncate,
	do_utime,
	do_getdents,
	do_statfs,
	do_noop,
	do_lookup,
	do_ctime,
	do_link,
	do_readlink,
	do_stat,
	do_gethashes,
	do_wcommit,
};

/* reqtest structure only used to print meaningful debug messages */
static char *reqtext[] = {
	"chmod",
	"chown",
	"close",
	"lstat",
	"mount",
	"open",
	"unlink",
	"shutdown",
	"umount",
	"fstat",
	"rename",
	"iod_info",
	"mkdir",
	"fchown",
	"fchmod",
	"rmdir",
	"access",
	"truncate",
	"utime",
	"getdents",
	"statfs",
	"noop",
	"lookup",
	"ctime",
	"link",
	"readlink",
	"stat",
	"gethashes",
	"wcommit",
/*** ADD NEW CALLS ABOVE THIS LINE ***/
	"error",
	"error",
	"error",
	"error",
	"error"
};

extern fslist_p active_p;
extern int default_ssize;
extern int capfs_mode;

/* PROTOTYPES */
extern int resv_name(char *);
extern int check_capfs(const char *pathname);
extern int get_root_dir(char *fname, char *rootbuf);
extern void *filter_dirents(void *buf, size_t buflen);
extern int invalidate_conn(iod_info *info_p);
extern int send_req(iod_info iod[], int iods, int base, int pcount, ireq_p req_p, 
    void *data_p, iack_p ack_p);
extern int stop_iods(void *v_p);
/* METADATA FN PROTOTYPES */
extern int md_chmod(mreq_p request, char *fname);
extern int md_chown(mreq_p request, char *fname);
#ifdef MGR_USE_CACHED_FILE_SIZE
extern int md_close(mreq_p req, char *fname, fsinfo_p fs_p);
#else
extern int md_close(mreq_p req, char *fname);
#endif
extern int md_open(char *name, mreq_p request, fmeta_p metar_p);
extern int md_rmdir(mreq_p req_p, char *fname);
extern int md_mkdir(char *dirpath, dmeta_p dir);
extern int md_islink(char *link_name, struct stat *statbuf);
extern char* md_readlink(char *link_name);
extern int md_link(char *link_name, char *target_name, mreq_p request);
extern int md_symlink(char *link_name, char *target_name, mreq_p request);
extern int md_stat(char *name, mreq_p request, fmeta_p metar_p, int follow_link);
extern int md_unlink(mreq_p request, fmeta_p metar_p, char *fname, int *is_link);
extern int put_dmeta(char *fname, dmeta_p dir);
extern int get_dmeta(char * fname, dmeta_p dir);


/* FUNCTIONS */
#define MAX_REASONABLE_TRAILER 16384
int process_compat_req(mreq *req, mack *ack, char *buf_p, struct ackdata *ackdata_p)
{
	int err = 0;
   struct passwd *user  = NULL;
   struct group  *group = NULL;

	memset(ack, 0, sizeof(*ack));

	user  = getpwuid(req->uid);
	group = getgrgid(req->gid);
	LOG(stderr, INFO_MSG, SUBSYS_META, "Request: type=[%s] user=[%s] group=[%s]\n",
             (req->type < 0 || req->type > MAX_MGR_REQ) ? "Invalid Request Type" : reqtext[req->type],
             (user==NULL)?"NULL":user->pw_name,
             (group==NULL)?"NULL":group->gr_name);

	if (req->majik_nr != MGR_MAJIK_NR) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META, "process_compat_req: invalid magic number; ignoring request\n");
		ack->majik_nr   = MGR_MAJIK_NR;
		ack->release_nr = CAPFS_RELEASE_NR;
		ack->type       = req->type;
		ack->status     = -1;
		ack->eno        = EINVAL;
		ack->dsize      = 0;
		return -1;
	}
	if (req->release_nr != CAPFS_RELEASE_NR) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META, "process_compat_req: release number from client (%d.%d.%d)"
				"does not match release number of this mgr (%d.%d.%d);"
				"returning error\n",
				(req->release_nr / 10000),
			   (req->release_nr / 100) % 10,
			   (req->release_nr % 10),
			   (CAPFS_RELEASE_NR / 10000),
			 	(CAPFS_RELEASE_NR / 100) % 10,
			 	(CAPFS_RELEASE_NR % 10));
		LOG(stderr, CRITICAL_MSG, SUBSYS_META, "process_compat_req: an administrator needs to verify"
				"that all CAPFS servers and clients are at the same release number.\n");
		ack->majik_nr   = MGR_MAJIK_NR;
		ack->release_nr = CAPFS_RELEASE_NR;
		ack->type       = req->type;
		ack->status     = -1;
		ack->eno        = EINVAL;
		ack->dsize      = 0;
		return -1;
	}
	if ((err=(req->dsize >= MAX_REASONABLE_TRAILER))
			|| req->type < 0 || req->type > MAX_MGR_REQ) { 
		/* this is ridiculous - throw request out */
		if (err) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_META, "process_compat_req: crazy huge dsize = %d\n;"
					"ignoring request\n", (int) req->dsize);
		}
		else {
			LOG(stderr, CRITICAL_MSG, SUBSYS_META, "invalid request (%d)", req->type);
		}
		ack->majik_nr   = MGR_MAJIK_NR;
		ack->release_nr = CAPFS_RELEASE_NR;
		ack->type       = req->type;
		ack->status     = -1;
		ack->eno        = EINVAL;
		ack->dsize      = 0;
		return -1;
	}
	if (req->dsize > 0) { /* make sure that there is trailing data that is NULL terminated */
		if (buf_p == NULL || buf_p[req->dsize] != '\0') {
			LOG(stderr, CRITICAL_MSG, SUBSYS_META, "process_compat_req  - getting NULL trailing data\n"
					"or non NULL terminated string; ignoring request\n");
			ack->majik_nr   = MGR_MAJIK_NR;
			ack->release_nr = CAPFS_RELEASE_NR;
			ack->type       = req->type;
			ack->status     = -1;
			ack->eno        = EINVAL;
			ack->dsize      = 0;
			return -1;
		}
		else {
			LOG(stderr, DEBUG_MSG, SUBSYS_META, "Trailing Data=[%s]\n", buf_p);
		}
	}
	/* initialize request */
	ack->majik_nr   = MGR_MAJIK_NR;
	ack->release_nr = CAPFS_RELEASE_NR;
	ack->type       = req->type; /* everything else is filled in in reqfn() */
	ack->status     = 0;
	ack->dsize      = 0;
	ack->eno        = 0;
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "req: %s\n", reqtext[req->type]);
	err = (reqfn[req->type])(req, buf_p, ack, ackdata_p); /* handle request */
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "Completed: type=[%s]\n", 
		 (req->type < 0 || req->type > MAX_MGR_REQ) ? "Invalid Request Type" : reqtext[req->type]);
	return(0);
}

static int do_noop(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	return 0;
}

static int do_chmod(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	/* check for reserved name first */
	if (resv_name(data_p) == 0) {
		ack_p->status = md_chmod(req_p, (char *)data_p); /* 0 on success */
		ack_p->eno = errno;
	}
	else {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
	}
	return 0;
}

static int do_chown(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	/* check for reserved name first */
	if (resv_name(data_p) == 0) {
		ack_p->status          = md_chown(req_p, data_p); /* 0 on success */
		ack_p->eno             = errno;
	}
	else {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
	}
	return 0;
}

/* do_access() - perform the access() operation
 *
 * This call also performs a stat(), so we go ahead and return that
 * data.  The file size included in there is not valid, so we -1 it out.
 *
 * Note that if data_p refers to a symbolic link, then depending upon whether
 * to_follow is set or not we try to
 * follow the link and check whether access is allowed on the target
 * rather than the symbolic link. As such a symbolic link does not have
 * any associated permissions associated with it. ie. it appears with mode
 * 777 on a ls -al.
 */
static int do_access(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	/* right now we simply check the file not the path */
	int mode_bits = 0;
	int ret;
   dmeta dir;

	/* check for reserved name first */
	if (resv_name(data_p) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}
	/*
	 * do a stat so we can figure out owner and permissions, We follow the link here depending upon 
	 * the value of to_follow in the mreq. This is usually the default for POSIX clients,
	 * library need not do this always (esp. capfs_readlink, capfs_symlink etc).
	 */
	ret = md_stat(data_p, req_p,
					  &(ack_p->ack.access.meta),
					  (req_p->req.access.to_follow == FOLLOW_LINK) ? FOLLOW_LINK : NOFOLLOW_LINK);
   if (ret != 0)
	{
		/* here we need to grab the fs_ino and return that for the "file does not exist" rename
		 * case.
		 */
		if (get_dmeta(data_p, &dir) < 0) {
			ack_p->ack.access.meta.fs_ino = -1;
			ack_p->status = -1;
			ack_p->eno    = ENOENT;

			ret = -1; /* force real error; might not be necessary? */
		}
		else {
			ack_p->ack.access.meta.fs_ino = dir.fs_ino;
		}
		ack_p->eno = errno;
		return 0;
	}

	if (capfs_mode == 0) {
		ack_p->ack.access.meta.u_stat.st_size = -1; /* not valid anyway */
	}
	

	ack_p->eno = EACCES;
	ack_p->status = -1;
	if (req_p->req.access.mode & R_OK)
		mode_bits |= 004;
	if (req_p->req.access.mode & W_OK)
		mode_bits |= 002;
	if (req_p->req.access.mode & X_OK)
		mode_bits |= 001;
	/* check root, o, g, and u in that order */
	if (req_p->uid == 0) /*give root access no matter what */
		ack_p->status = 0;
	else if (((ack_p->ack.access.meta.u_stat.st_mode & 007) &
		 mode_bits) == mode_bits)
		ack_p->status = 0;
	else if ((in_group(req_p->uid, ack_p->ack.access.meta.u_stat.st_gid) == 1) &&
		(((ack_p->ack.access.meta.u_stat.st_mode >> 3) & 007) &
		 mode_bits) == mode_bits)
		ack_p->status = 0;
	else if (ack_p->ack.access.meta.u_stat.st_uid == req_p->uid &&
		(((ack_p->ack.access.meta.u_stat.st_mode >> 6) & 007) &
		 mode_bits) == mode_bits)
		ack_p->status = 0;
	if (ack_p->status == 0) {
		ack_p->eno = 0;
	}
	return 0;
}

static int do_truncate(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	int fd;
	fsinfo_p fs_p;
   finfo_p f_p;
	fmeta meta;
   int64_t newhash_count = 0;
   int64_t oldhash_count = 0;

	/* check for reserved name first */
	if (resv_name(data_p) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}

	if ((fs_p = quick_mount(data_p, req_p->uid, req_p->gid, ack_p, ackdata_p)) == NULL)
	{
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}

	/* get metadata, check permission to write to file */
	if ((fd = meta_open(data_p, O_RDONLY)) < 0
		|| (meta_read(fd, &meta) < 0)
   	|| ((meta_access(fd, data_p, req_p->uid, req_p->gid, W_OK)) < 0)
		|| (meta_close(fd) < 0))
	{
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	/* return the fs_ino and file inode numbers of the truncated file */
   ackdata_p->type = MGR_TRUNCATE;
	ackdata_p->u.truncate.fs_ino = fs_p->fs_ino;
	ackdata_p->u.truncate.f_ino  = meta.u_stat.st_ino;
	ackdata_p->u.truncate.begin_chunk = -1;
	ackdata_p->u.truncate.nchunks = 0;
	ackdata_p->u.truncate.old_length = meta.u_stat.st_size;
	/* this formula should effectively calculate the number of hashes required for a file of given size */
   oldhash_count = (meta.u_stat.st_size + CAPFS_CHUNK_SIZE - 1) / CAPFS_CHUNK_SIZE;
	newhash_count = (req_p->req.truncate.length + CAPFS_CHUNK_SIZE - 1) / CAPFS_CHUNK_SIZE;
	meta.u_stat.st_size = req_p->req.truncate.length;

	/* write metadata */
	if ((fd = meta_open(data_p, O_WRONLY)) < 0
		|| (meta_write(fd, &meta) < 0)
		|| (meta_close(fd) < 0))
	{
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}

	/* if the truncate has changed the file size, we need to truncate the recipe file as well */
	if (newhash_count != oldhash_count)
	{
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "[truncate] on %s changed # of hashes from %Ld to %Ld\n",
				(char *) data_p, oldhash_count, newhash_count);
		f_p = f_search(fs_p->fl_p, meta.u_stat.st_ino);
		/* if file was open, acquire an in-memory lock */
		if (f_p)
		{
			f_wrlock(f_p);
		}
		/* if file was not open, treat it like no races possible. Technically incorrect here though */
		meta_hash_truncate(data_p, newhash_count);
		/* unlock it after the operation is done */
		if (f_p)
		{
			f_unlock(f_p);
		}
		/* if the new file is smaller than the previous one */
		if (newhash_count < oldhash_count)
		{
			ackdata_p->u.truncate.begin_chunk = oldhash_count;
			ackdata_p->u.truncate.nchunks     = (oldhash_count - newhash_count);
		}
	}
	
	return 0;
}

/* DO_CTIME() - update ctime of a file.  see also do_utime() function,
 * which updates atime and mtime.
 * Updates the ctime of the target of a symbolic link if it exists!
 */
static int do_ctime(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	int fd, i, myerr;
	fsinfo_p fs_p;
	fmeta meta;

	/* check for reserved name first */
	if (resv_name(data_p) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}

	/* we do this simply so we can get the fsinfo for use later */
	if ((fs_p = quick_mount(data_p, req_p->uid, req_p->gid, ack_p, ackdata_p)) == NULL)
	{
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}	

	/* open metadata file */
	if ((fd = meta_open(data_p, O_RDWR)) < 0)
	{
		if (errno != EISDIR) {
			myerr = errno;
			PERROR(SUBSYS_META,"do_ctime: meta_open");
			errno = myerr;
			ack_p->status = -1;
			ack_p->eno = errno;
			return 0;
		}
		else fd = 0; /* needed for meta_access() below...Scott was on
						  * crack when he wrote the fn... */
	}

	/* check write access permissions on file */
	if (meta_access(fd, data_p, req_p->uid, req_p->gid, W_OK) < 0)
	{
		/* if no write permissions, then check ownership */
		if(meta_check_ownership(fd, data_p, req_p->uid, req_p->gid) < 0)
		{
			myerr = errno;
			PERROR(SUBSYS_META,"do_ctime: meta_access and meta_check_ownership");
			errno = myerr;
			ack_p->status = -1;
			ack_p->eno = errno;
			return 0;
		}
	}

	/* see if its a directory or file */
	if ((i = check_capfs(data_p)) < 1) {
		myerr = errno;
		PERROR(SUBSYS_META,"do_ctime: check_capfs");
		errno = myerr;
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}

	/* read metadata */
	if (i == 1) {
		if (meta_read(fd, &meta) < 0) {
			myerr = errno;
			PERROR(SUBSYS_META,"do_ctime: meta_read");
			errno = myerr;
			ack_p->status = -1;
			ack_p->eno = errno;
			return 0;
		}

		/* modify metadata */
		meta.u_stat.ctime = req_p->req.ctime.createtime;

		if ((i = meta_write(fd, &meta)) < 0) {
			myerr = errno;
			PERROR(SUBSYS_META,"do_ctime: meta_write");
			errno = myerr;
			ack_p->status = -1;
			ack_p->eno = errno;
			return 0;
		}

		meta_close(fd);
	}
	else { /* it's a directory */
		/* TODO: how can we handle this? */
	}
	/* success */
	return(0);
}

/* DO_UTIME() - perform utime() operation.  see also do_ctime, which
 * handles updating creation time in a seperate request.
 * This updates the atime and mtime for the target of the symbolic link
 * file if it exists.
 */
static int do_utime(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	int fd, i, myerr;
	fsinfo_p fs_p;
	finfo_p f_p;
	fmeta meta;

	/* check for reserved name first */
	if (resv_name(data_p) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}

	/* we do this simply so we can get the fsinfo for use later */
	if ((fs_p = quick_mount(data_p, req_p->uid, req_p->gid, ack_p, ackdata_p)) == NULL)
	{
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}	

	/* open metadata file */
	if ((fd = meta_open(data_p, O_RDWR)) < 0)
	{
		if (errno != EISDIR) {
			myerr = errno;
			PERROR(SUBSYS_META,"do_utime: meta_open");
			errno = myerr;
			ack_p->status = -1;
			ack_p->eno = errno;
			return 0;
		}
		else fd = 0; /* needed for meta_access() below...Scott was on
						  * crack when he wrote the fn... */
	}

	/* check write access permissions on file */
	if (meta_access(fd, data_p, req_p->uid, req_p->gid, W_OK) < 0)
	{
		/* if no write permissions, then check ownership */
		if(meta_check_ownership(fd, data_p, req_p->uid, req_p->gid) < 0)
		{
			myerr = errno;
			PERROR(SUBSYS_META,"do_ctime: meta_access and meta_check_ownership");
			errno = myerr;
			ack_p->status = -1;
			ack_p->eno = errno;
			return 0;
		}
	}

	/* see if its a directory or file */
	if ((i = check_capfs(data_p)) < 1) {
		myerr = errno;
		PERROR(SUBSYS_META,"do_utime: check_capfs");
		errno = myerr;
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}

	/* read metadata */
	if (i == 1) {
		if (meta_read(fd, &meta) < 0) {
			myerr = errno;
			PERROR(SUBSYS_META,"do_utime: meta_read");
			errno = myerr;
			ack_p->status = -1;
			ack_p->eno = errno;
			return 0;
		}

		/* modify metadata */
		meta.u_stat.atime = req_p->req.utime.actime;
		meta.u_stat.mtime = req_p->req.utime.modtime;

		if ((i = meta_write(fd, &meta)) < 0) {
			myerr = errno;
			PERROR(SUBSYS_META,"do_utime: meta_write");
			errno = myerr;
			ack_p->status = -1;
			ack_p->eno = errno;
			return 0;
		}

		meta_close(fd);

		/* if the file is open, record the utime event in the file struct */
		if ((f_p = f_search(fs_p->fl_p, meta.u_stat.st_ino)) != NULL) {
			/* we're interested in WHEN the time was set AND what it was set to */
			f_p->utime_event = time(NULL);
			f_p->utime_modtime = req_p->req.utime.modtime;
		}
	}
	else { /* it's a directory */
		struct utimbuf utb;

		utb.actime = req_p->req.utime.actime;
		utb.modtime = req_p->req.utime.modtime;

		if (utime(data_p, &utb) < 0) {
			myerr = errno;
			PERROR(SUBSYS_META,"do_utime: utime");
			errno = myerr;
			ack_p->status = -1;
			ack_p->eno = errno;
			return 0;
		}
	}
	/* success */
	return 0;
}

/* do_fstat() - perform fstat() operation -- calls do_stat() to do the
 * work
 */
static int do_fstat(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	mreq fakereq;
	fsinfo_p fs_p;
	finfo_p f_p;

	/* find filesystem & open file */
	if (!(fs_p = fs_search(active_p, req_p->req.fstat.meta.fs_ino))
		|| !(f_p = f_search(fs_p->fl_p, req_p->req.fstat.meta.u_stat.st_ino)))
	{
		ack_p->status = -1;
		ack_p->eno  = errno;
		return 0;
	}

	/* build a fake stat request, call do_stat() */
	fakereq.majik_nr   = MGR_MAJIK_NR;
	fakereq.release_nr = CAPFS_RELEASE_NR;
	fakereq.type       = MGR_STAT;
	fakereq.uid        = req_p->uid;
	fakereq.gid        = req_p->gid;
	fakereq.dsize      = strlen(f_p->f_name);

	return (do_stat(&fakereq, f_p->f_name, ack_p, ackdata_p));
} /* end of do_fstat() */

/* do_stat() - also fills in for do_lstat.  this is done by checking
 * the request type; see below.
 */
static int do_stat(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	int ret, i;
	fsinfo_p fs_p;
	ireq iodreq;

	/* variables used for calculating correct file size */
	int64_t real_file_sz, tmp_file_sz, iod_file_sz;
	int64_t strip_sz, stripe_sz, nr_strips, leftovers;

	/* used for calculating distributed mtime */
	int64_t max_modtime = 0;
	finfo_p f_p;

	/* check for reserved name first */
	if (resv_name(data_p) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}

	if ((fs_p = quick_mount(data_p, req_p->uid, req_p->gid, ack_p, ackdata_p)) == NULL)
	{
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}	

	/* We allow a stat to follow a symbolic link, depending on req type */
	ret = md_stat(data_p, req_p, &(ack_p->ack.stat.meta),
					  req_p->type == MGR_LSTAT ? NOFOLLOW_LINK :FOLLOW_LINK);
	if (ret != 0) {
		ack_p->status = ret;
		ack_p->eno = errno;
		return 0;
	}

	/* initialize max_mtime from the metadata on disk */
	max_modtime = ack_p->ack.stat.meta.u_stat.mtime;

	/* in CAPFS, u_stat has the latest sizes of the file */
	if (capfs_mode == 0) 
	{
#ifdef MGR_USE_CACHED_FILE_SIZE
		if ( (f_search(fs_p->fl_p, ack_p->ack.stat.meta.u_stat.st_ino) != NULL) 
			  || ack_p->ack.stat.meta.u_stat.st_size == 0)
			{
				/* Either file is open (and we don't have accurate cached values),
				 * or size is 0 (compatibility with old metadata).
				 */
#endif
				/* if regular file, get size from iods */
				if (S_ISREG(ack_p->ack.stat.meta.u_stat.st_mode)) {
					iodreq.majik_nr       = IOD_MAJIK_NR;
					iodreq.release_nr     = CAPFS_RELEASE_NR;
					iodreq.type 			 = IOD_STAT;
					iodreq.dsize 			 = 0;
					iodreq.req.stat.f_ino = ack_p->ack.stat.meta.u_stat.st_ino;
					if ((ret = send_req(fs_p->iod, fs_p->nr_iods, 
											  ack_p->ack.stat.meta.p_stat.base,
											  ack_p->ack.stat.meta.p_stat.pcount, 
											  &iodreq, data_p, NULL)) < 0)
					{
						ack_p->status = ret;
						ack_p->eno = errno;
						return 0;
					}	
					
					strip_sz = ack_p->ack.stat.meta.p_stat.ssize;
					stripe_sz = ack_p->ack.stat.meta.p_stat.ssize * 
						ack_p->ack.stat.meta.p_stat.pcount;
					real_file_sz = 0;
					
					for (i = 0; i < ack_p->ack.stat.meta.p_stat.pcount; i++) {
						/* i gives us the index into the iods, the ith iod in order */

						/* finding largest mtime -- mtime at a particular iod
						 * might have changed while the file is open, and we wouldn't
						 * know until the file was closed.
						 */
						if (max_modtime < fs_p->iod[i].ack.ack.stat.mtime) {
							max_modtime = fs_p->iod[i].ack.ack.stat.mtime;
						}

						iod_file_sz = fs_p->iod[(i + ack_p->ack.stat.meta.p_stat.base) %
														fs_p->nr_iods].ack.ack.stat.fsize;
						if (iod_file_sz == 0) continue;
						
						/* the plan is to calculate the file size based on what
						 * this particular iod has.  the largest of these sizes
						 * is the actual file size, taking into account sparse
						 * issues.
						 *
						 * there are four components to the calculation:
						 *
						 * soff = should be 0 since it isn't implemented :)
						 *
						 * nr_strips * stripe_sz = this is a calculation of how many
						 *   complete stripes are present.  note that any partial strip
						 *   or the last complete strip (if there are no partials)
						 *   may not be part of a whole stripe (thus the if() below).
						 *
						 * i * strip_sz = this accounts for full strips that should be
						 *   present on iods that come before us in the iod ordering
						 *
						 * leftovers = the last remaining bytes
						 */
						nr_strips = iod_file_sz / strip_sz;
						leftovers = iod_file_sz % strip_sz;
						
						if (leftovers == 0) {
							nr_strips--;
							leftovers += strip_sz;
						}
						tmp_file_sz = nr_strips * stripe_sz + i * strip_sz +
							leftovers;
						if (tmp_file_sz > real_file_sz) real_file_sz = tmp_file_sz;
					}
					
					ack_p->ack.stat.meta.u_stat.st_size = real_file_sz;

					/* look for open file information */
					f_p = f_search(fs_p->fl_p, ack_p->ack.stat.meta.u_stat.st_ino);

					/* if we found the open file, then there is the possibility of
					 * both a utime event having been recorded in memory here and
					 * of a change on an iod causing the mtime to be changed.
					 */
					if (f_p) {
						if (f_p->utime_event > max_modtime) {
							max_modtime = f_p->utime_modtime;
						}
						ack_p->ack.stat.meta.u_stat.mtime = max_modtime;
					}
					/* otherwise the file is closed, and we can safely ignore the
					 * results from the iods.  in that case we leave the value
					 * that we read into the ack in place and continue.
					 */

#ifdef MGR_USE_CACHED_FILE_SIZE
					/* store to metadata if closed */
					if (f_p == NULL) {
						int fd;

						/* get fd for meta file */
						if ((fd = meta_open(data_p, O_RDWR)) < 0) {
							PERROR(SUBSYS_META,"do_stat: meta_open");
							return(-1);
						}

						/* Write metadata back */
						if (meta_write(fd, &(ack_p->ack.stat.meta)) < 0) {
							PERROR(SUBSYS_META,"do_stat: meta_write.");
							return(-1);
						}

						/* Close metadata file */
						if (meta_close(fd) < 0) {
							PERROR (SUBSYS_META, "do_stat: File close");
							return(-1);
						}
					}
#endif
				}
#ifdef MGR_USE_CACHED_FILE_SIZE
			} /* if open or zero... */
			/* otherwise do nothing because it is already saved */
#endif
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "atime: %llu mtime: %llu ctime: %llu\n",
			ack_p->ack.stat.meta.u_stat.atime, ack_p->ack.stat.meta.u_stat.mtime, ack_p->ack.stat.meta.u_stat.ctime);
	ack_p->status = 0;
	ack_p->eno    = 0;
	return 0;
}


static int do_lookup(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	int ret;
	fsinfo_p fs_p;
	fmeta meta;

	/* check for reserved name first */
	if (resv_name(data_p) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}

	if ((fs_p = quick_mount(data_p, req_p->uid, req_p->gid, ack_p, ackdata_p)) == NULL)
	{
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}	

	/* We don't allow a lookup to follow a symbolic link also */
	if ((ret = md_stat(data_p, req_p, &meta, NOFOLLOW_LINK)) != 0) {
		ack_p->status = ret;
		ack_p->eno = errno;
		return 0;
	}
	
	/* even though we copy an entire stat structure, we haven't 
	 * calculated the true size */
	memcpy(&(ack_p->ack.lookup.meta.u_stat), &meta.u_stat, sizeof(meta.u_stat));
	return 0;
} 


/* do_statfs()
 */
static int do_statfs(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	ireq iodreq;
	fsinfo_p fs_p;
	struct statfs sfs;
	int64_t min_free_bytes = 0;
	int64_t total_bytes = 0;
	int i, err;

   /* clear the statfs buffer */
   memset(&sfs, 0, sizeof(sfs));

	/* check for reserved name first */
	if (resv_name(data_p) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}

	/* make sure we can talk to the iods */
	if ((fs_p = quick_mount(data_p, req_p->uid, req_p->gid, ack_p, ackdata_p)) == NULL)
	{
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}

	/* statfs or statvfs on the file locally to get file-related values */
	err = statfs(data_p, &sfs);
	if (err == -1) {
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}

	ack_p->ack.statfs.tot_files = sfs.f_files;
	ack_p->ack.statfs.free_files = sfs.f_ffree;
	ack_p->ack.statfs.namelen = sfs.f_namelen;
	
	/* if we didn't get an error by now, then our filename is ok */
	if (capfs_mode == 0) {
		/* send request to all iods for this file system */
		memset(&iodreq, 0, sizeof(iodreq));
		iodreq.majik_nr   = IOD_MAJIK_NR;
		iodreq.release_nr = CAPFS_RELEASE_NR;
		iodreq.type       = IOD_STATFS;
		iodreq.dsize      = 0;
		if ((err = send_req(fs_p->iod, fs_p->nr_iods, 0, 
				fs_p->nr_iods, &iodreq, data_p, NULL)) < 0)
		{
			ack_p->status = err;
			ack_p->eno = errno;
			return 0;
		}	

		/* pull data together
		 *
		 * Note: we total up the bytes on all iods for the total value, but
		 * for the free value we're going to calculate using:
		 * 
		 * (free space) = (# of iods) * (minimum amount free on any one)
		 *
		 * Although we're lying a bit, it's the best value for giving an
		 * expectation of what could be written to the system.
		 */
		min_free_bytes = fs_p->iod[0].ack.ack.statfs.free_bytes;
		for (i=0; i < fs_p->nr_iods; i++) {
			if (min_free_bytes > fs_p->iod[i].ack.ack.statfs.free_bytes)
				min_free_bytes = fs_p->iod[i].ack.ack.statfs.free_bytes;
			total_bytes += fs_p->iod[i].ack.ack.statfs.tot_bytes;
		}
	}
	/* talk to the IOD servers one after the other and pull in that data */
	else {
		min_free_bytes = LLONG_MAX;
		for (i = 0; i < fs_p->nr_iods; i++) {
			int64_t tmp_free_bytes = 0, tmp_tot_bytes = 0;
			struct sockaddr* iodaddr;

			iodaddr = (struct sockaddr *)&fs_p->iod[i].addr;
			/* use tcp for this */
			if (clnt_statfs_req(1, iodaddr, &sfs) < 0) {
				ack_p->status = -1;
				ack_p->eno = errno;
				return 0;
			}
			tmp_tot_bytes = ((int64_t)sfs.f_blocks)*((int64_t)sfs.f_bsize);
			tmp_free_bytes = ((int64_t)sfs.f_bavail)*((int64_t)sfs.f_bsize);
			if (min_free_bytes > tmp_free_bytes)
				min_free_bytes = tmp_free_bytes;
			total_bytes += tmp_tot_bytes;
		}
	}
	ack_p->ack.statfs.tot_bytes = total_bytes;
	ack_p->ack.statfs.free_bytes = min_free_bytes * ((int64_t) fs_p->nr_iods);

	return(0);
}

static int do_unlink(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	int ret, fd, is_link = 0;
	fsinfo_p fs_p;
	finfo_p f_p;
	ireq iodreq;
	fmeta meta;

	/* check for reserved name first */
	if (resv_name(data_p) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}

	if ((fs_p = quick_mount(data_p, req_p->uid, req_p->gid, ack_p, ackdata_p)) == NULL)
	{
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	/* md_unlink returns -1 on failure, or an open fd to metadata file on
	 * success.  This is so we can hold on to the inode if we need to.
	 */
	if ((fd = md_unlink(req_p, &meta, (char *)data_p, &is_link)) < 0) {
		ack_p->status = fd;
		ack_p->eno = errno;
		return 0;
	}
	/* only if it is a normal file should this be done */
	if (is_link != 1) 
	{
		int64_t fs_ino, f_ino;

		fs_ino = fs_p->fs_ino;
		assert(fs_ino == meta.fs_ino);
		f_ino  = meta.u_stat.st_ino;
		ackdata_p->u.unlink.fs_ino = fs_ino;
		ackdata_p->u.unlink.f_ino  = f_ino;
		if ((f_p = f_search(fs_p->fl_p, meta.u_stat.st_ino)) != 0) 
		{
			/* don't delete this one yet */
			LOG(stderr, DEBUG_MSG, SUBSYS_META, " do_unlink: file open, delaying unlink\n");
			f_p->unlinked = fd;
		}
		else /* need to close FD and do IOD call */ 
		{
			meta_close(fd);
			if (capfs_mode == 0) 
			{
				memset(&iodreq, 0, sizeof(iodreq));
				iodreq.majik_nr   = IOD_MAJIK_NR;
				iodreq.release_nr = CAPFS_RELEASE_NR;
				iodreq.type       = IOD_UNLINK;
				iodreq.dsize      = 0;
				iodreq.req.unlink.f_ino = meta.u_stat.st_ino;
				if ((ret = send_req(fs_p->iod, fs_p->nr_iods, meta.p_stat.base, 
					meta.p_stat.pcount, &iodreq, NULL, NULL)) < 0)
				{
					ack_p->status = ret;
					ack_p->eno = errno;
					return 0;
				}
			}
		}
	}
	return(0);
}

static int do_close(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	int i, cnt, myerr = 0;
	fsinfo_p fs_p;
	finfo_p f_p;
	ireq iodreq;
	int64_t max_modtime = 0;

	memset(&iodreq, 0, sizeof(iodreq));

	/* find filesystem & open file */
	if (!(fs_p = fs_search(active_p, req_p->req.close.meta.fs_ino))
		|| !(f_p = f_search(fs_p->fl_p, req_p->req.close.meta.u_stat.st_ino)))
	{
		myerr = errno;
		LOG(stderr, CRITICAL_MSG, SUBSYS_META, " do_close: error finding fs (%Ld) or ino (%Ld)\n", req_p->req.close.meta.fs_ino,
			req_p->req.close.meta.u_stat.st_ino);
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	if (--f_p->cnt == 0) /* last open instance; tell IODs to close */ {
		LOG(stderr, INFO_MSG, SUBSYS_META, "last reference; closing file\n");
		/* send request to close to IODs */
		if (capfs_mode == 0) {
			memset(&iodreq, 0, sizeof(iodreq));
			iodreq.majik_nr        = IOD_MAJIK_NR;
			iodreq.release_nr      = CAPFS_RELEASE_NR;
			iodreq.type            = IOD_CLOSE;
			iodreq.dsize           = 0;
			iodreq.req.close.f_ino = f_p->f_ino;
			/* Tells iod to close all instances of open file */
			iodreq.req.close.cap = -1; 
			
			if (send_req(fs_p->iod, fs_p->nr_iods, f_p->p_stat.base, 
					f_p->p_stat.pcount, &iodreq, NULL, NULL) < 0)
			{
				myerr = errno;
			}
		}

		/* md_close() will pull the atime and mtime from the request and
		 * set them in the metadata file.
		 */
		req_p->req.close.meta.u_stat.atime = time(NULL);
		/* remember that mtime is also set at the time of wcommit for capfs */
		req_p->req.close.meta.u_stat.mtime = time(NULL);
		if (capfs_mode == 0) 
		{
#ifdef USE_OLD_TIME_SYSTEM
			req_p->req.close.meta.u_stat.mtime = time(NULL);
#else

			/* find the latest modification time as returned from the iods */
			for (i = 0, cnt = f_p->p_stat.base;
				  i < f_p->p_stat.pcount; 
				  i++, cnt = (cnt + 1) % fs_p->nr_iods) 
			{
				if (fs_p->iod[cnt].ack.status == 0 && 
					 max_modtime < fs_p->iod[cnt].ack.ack.close.modtime)
				{
						max_modtime = fs_p->iod[cnt].ack.ack.close.modtime;
				}
			}

			/* handle case where utime event occurred after last modification */
			if (f_p->utime_event > max_modtime) {
				req_p->req.close.meta.u_stat.mtime = f_p->utime_modtime;
			}
			else {
				req_p->req.close.meta.u_stat.mtime = max_modtime;
			}
#endif
		}

		if (f_p->unlinked >= 0) /* tell IODs to remove the file too */ 
		{
			if (capfs_mode == 0) 
			{
				memset(&iodreq, 0, sizeof(iodreq));
				iodreq.majik_nr         = IOD_MAJIK_NR;
				iodreq.release_nr       = CAPFS_RELEASE_NR;
				iodreq.type             = IOD_UNLINK;
				iodreq.dsize            = 0;
				iodreq.req.unlink.f_ino = f_p->f_ino;
				if (send_req(fs_p->iod, fs_p->nr_iods, f_p->p_stat.base, 
						f_p->p_stat.pcount, &iodreq, NULL, NULL) < 0)
				{
					myerr = errno;
				}
			}
			meta_close(f_p->unlinked); /* close the FD that was kept around */
		}
		else {
			/* call md_close() to update times and such */
#ifdef MGR_USE_CACHED_FILE_SIZE
			md_close(req_p, f_p->f_name, fs_p);
#else
			md_close(req_p, f_p->f_name);
#endif
		}
		f_rem(fs_p->fl_p,f_p->f_ino); /* wax file info */
	}
	if (myerr) {
		ack_p->status = -1;
		ack_p->eno = myerr;
	}
	return 0;
}

static int do_mount(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	fsinfo_p fs_p;
	int niods;
	char tmpbuf[256];
	iodtabinfo_p tab_p;
	dmeta dir;

	/* check for reserved name first */
	if (resv_name(data_p) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}

	if (get_dmeta(data_p, &dir) < 0) {
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	/* if mounted, OK */
	if (fs_search(active_p, dir.fs_ino)) { 
		return 0;
	}

	/* set up iodtab file name */
	strncpy(tmpbuf, data_p, req_p->dsize);
	strcpy(&tmpbuf[req_p->dsize], TABFNAME);

	/* read iodtab file */
	if ((tab_p = parse_iodtab(tmpbuf)) == NULL) {
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}

	/* allocate and fill in fsinfo structure */
	if ((fs_p = (fsinfo_p) malloc(sizeof(fsinfo)
		+ sizeof(iod_info) * ((tab_p->nodecount)-1))) == NULL)
	{
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	memset(fs_p, 0, sizeof(fsinfo)+sizeof(iod_info)*((tab_p->nodecount)-1));
	fs_p->nr_iods = tab_p->nodecount;
	fs_p->fs_ino  = dir.fs_ino;
	fs_p->fl_p    = NULL;
	for (niods = 0; niods < tab_p->nodecount; niods++) {
		fs_p->iod[niods].addr = tab_p->iod[niods];
		fs_p->iod[niods].sock = -1;
	}

	if (fs_add(active_p, fs_p) < 0) {
		PERROR(SUBSYS_META,"fs_add in do_mount");
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	return 0;
}

#define RQ_PSTAT req_p->req.open.meta.p_stat
#define AK_PSTAT ack_p->ack.open.meta.p_stat

static int do_open(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	int ret, new_entry=0, new_file=0, myerr;
	fsinfo_p fs_p;
	finfo_p f_p;
	ireq iodreq;

	memset(&iodreq, 0, sizeof(ireq));

	/* NOTE:
	 * new_entry is used to indicate that a file needs to be added into
	 * our list of open files (isn't already in there)
	 *
	 * new_file is used to indicate that we're creating this new file.
	 * -- Rob
	 */

	/* check for reserved name first */
	if (resv_name(data_p) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}

	if ((fs_p = quick_mount(data_p, req_p->uid, req_p->gid, ack_p, ackdata_p)) == NULL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META, "do_open: quick_mount: NULL returned for %s\n", (char *) data_p);
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}

	/* check for pcount > nr_iods and base >= nr_iods */
	if (RQ_PSTAT.pcount > fs_p->nr_iods || RQ_PSTAT.base >= fs_p->nr_iods) {
		ack_p->status = -1;
		ack_p->eno = EINVAL;
		return 0;
	}

	/* if default values are requested, fill them in */
	if (RQ_PSTAT.pcount == -1) RQ_PSTAT.pcount = fs_p->nr_iods; 
	if (RQ_PSTAT.ssize == -1)  RQ_PSTAT.ssize  = default_ssize;

	if (RQ_PSTAT.base == -1) {
		if (random_base) {
			/* pick a random base node number */
			int randbase;
			randbase = rand() % fs_p->nr_iods;
			RQ_PSTAT.base = randbase;
		}
		else {
			RQ_PSTAT.base = 0;
		}
	}
	/* check to see if the file exists already */
	if ((ret = meta_open(data_p, O_RDONLY)) < 0) {
		new_file = 1;
	}
	else {
		meta_close(ret);
	}

	/* clients don't always know the fs_ino, and we want to make sure that
	 * this is correct in the metadata file, so we fill it in before
	 * calling md_open().
	 */
	req_p->req.open.meta.fs_ino = fs_p->fs_ino;

	/* md_open() fills in the st_ino structure used below and 
	 * performs sanity checking on the values passed in
	 *
	 * It also creates the metadata file in the specified directory.
	 */
	if ((ret = md_open(data_p, req_p, &(ack_p->ack.open.meta))) < 0) {
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "do_open: md_open failed: %s, flags = 0x%x\n",
			strerror(errno), req_p->req.open.flag);
		if (new_file && !(req_p->req.open.flag & O_CREAT)) {
		    LOG(stderr, CRITICAL_MSG, SUBSYS_META, "do_open: tried to open new file without O_CREAT\n");
		}
		ack_p->status = ret;
		ack_p->eno = errno;
		return 0;
	}
	/*
	AK_PSTAT.base = RQ_PSTAT.base;
	AK_PSTAT.pcount = RQ_PSTAT.pcount;
	AK_PSTAT.ssize = RQ_PSTAT.ssize;
	*/
	LOG(stderr, WARNING_MSG, SUBSYS_META, "create [%s] base [%d (OR) %d] nriods [%d (OR) %d] ssize [%d (OR) %d]\n", 
			(char *) data_p, RQ_PSTAT.base, AK_PSTAT.base, RQ_PSTAT.pcount, AK_PSTAT.pcount, RQ_PSTAT.ssize, AK_PSTAT.ssize); 

	f_p = f_search(fs_p->fl_p, ack_p->ack.open.meta.u_stat.st_ino);

	if (!f_p) /* not open */ {
		new_entry = 1;
		if (!(f_p = f_new())) {
			ack_p->status = -1;
			ack_p->eno = errno;
			return 0;
		}
		f_p->f_ino   = ack_p->ack.open.meta.u_stat.st_ino; 
		f_p->p_stat  = AK_PSTAT;
		f_p->f_name  = (char *)malloc(strlen(data_p)+1);

		/* keep utimes from going backwards */
		f_p->utime_event   = time(NULL);
		f_p->utime_modtime = ack_p->ack.open.meta.u_stat.mtime;

		strcpy(f_p->f_name, data_p);
		//dfd_set(sock, &f_p->socks);
	}
	else /* already open */ {
		f_p->cap++;
		f_p->cnt++;
		//dfd_set(sock, &f_p->socks);
	}

	f_dump(f_p);
	if (capfs_mode == 0) 
	{
		iodreq.majik_nr        = IOD_MAJIK_NR;
		iodreq.release_nr      = CAPFS_RELEASE_NR;
		iodreq.type            = IOD_OPEN;
		iodreq.dsize           = 0;
		iodreq.req.open.f_ino  = f_p->f_ino;
		iodreq.req.open.cap    = f_p->cap;
		iodreq.req.open.flag   = req_p->req.open.flag;
		iodreq.req.open.mode   = S_IRUSR | S_IWUSR; /* permissions on file */
		iodreq.req.open.p_stat = AK_PSTAT;
		iodreq.req.open.pnum   = 0;
		
		if ((ret = send_req(fs_p->iod, fs_p->nr_iods, f_p->p_stat.base, 
				f_p->p_stat.pcount, &iodreq, NULL, NULL)) < 0)
		{
			mreq fakereq;
			mack fakeack;
			
			myerr = errno;

			/* if we fail to open the file we need to send a close to
			 * free resources on the iods that *did* get the open request.
			 *
			 * note that if there are other open instances, we probably won't
			 * really get the file closed.  we aren't trying to handle that case
			 * here.
			 */
			
			LOG(stderr, DEBUG_MSG, SUBSYS_META, "do_open(%s): cleaning up after failed open\n", (char *) data_p);
			fakereq.majik_nr   = MGR_MAJIK_NR;
			fakereq.release_nr = CAPFS_RELEASE_NR;
			fakereq.type       = MGR_CLOSE;
			fakereq.uid        = req_p->uid;
			fakereq.gid        = req_p->gid;
			fakereq.dsize      = 0;
			fakereq.req.close.meta.fs_ino        = fs_p->fs_ino;
			fakereq.req.close.meta.u_stat.st_ino = f_p->f_ino;
			
			if (new_file) {
				/* here is a tricky case...we need to remove all the files we
				 * created on the nodes that did work and we need to remove the
				 * metadata file that md_open() created.
				 *
				 * we do this by marking the file as unlinked before sending
				 * the close request out to the iods.
				 */
				f_p->unlinked = meta_open(data_p, O_RDONLY); 
			}


			/* add the file in so that do_close() can find it */
			if (new_entry) f_add(fs_p->fl_p, f_p);

			do_close(&fakereq, NULL, &fakeack, ackdata_p);
			/* do_close() gets rid of our copy of the file info at f_p */

			if (new_file) {
				/* now get rid of the metadata file */
				meta_unlink(data_p);
			}

			errno = myerr;

			ack_p->status = ret;
			ack_p->eno = errno;
			return 0;
		}
	}
	ackdata_p->type = MGR_OPEN;
	/* now RD lock the hashes file and obtain ALL the hashes only if requested and only if needs to be locked */
	f_rdlock(f_p);
	ret = meta_hash_read(data_p, -1, &ackdata_p->u.open.nhashes, &ackdata_p->u.open.hashes);
	/* now unlock the hashes file */
	f_unlock(f_p);
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "open on %s yielded %Ld hashes\n", (char *)data_p,
			ackdata_p->u.open.nhashes);

#ifdef VERBOSE_DEBUG
	{
		int i;
		for (i = 0; i < ackdata_p->u.open.nhashes; i++) {
			char str[256];

			hash2str(ackdata_p->u.open.hashes + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH, str);
			LOG(stderr, DEBUG_MSG, SUBSYS_META, "%d: %s\n", i, str);
		}
	}
#endif

	if (send_open_ack(req_p, ack_p, fs_p, f_p->cap, ackdata_p) < 0) {
		/* handle cleaning up bad send */
		LOG(stderr, CRITICAL_MSG, SUBSYS_META, "do_open: send_open_ack failed\n");
		if (new_entry) f_free(f_p);
		return 0;
	}
	if (new_entry) f_add(fs_p->fl_p, f_p); /* add file info to list if new */
	return 0;
}

/* SEND_OPEN_ACK() - sends ACK and iod addresses through socket 
 * back to an application that has opened a file
 *
 * note: this call always returns the iod information in an order such
 * that the base iod is listed first on the client side.
 */
static int send_open_ack(mreq_p req_p, mack_p ack_p, fsinfo_p fs_p, int cap, struct ackdata *ackdata_p)
{
	struct sockaddr saddr;
	int pcnt_remain, nr_infos;
	iod_info **vec = NULL;
	int vec_count = 0, j = 0;

	ack_p->eno = 0;
	ack_p->dsize = AK_PSTAT.pcount * sizeof(iod_info);
	ack_p->ack.open.cap = cap;

	get_myaddress((struct sockaddr_in *)&saddr);
	ack_p->ack.open.meta.mgr = saddr;

	vec = (iod_info **)calloc(AK_PSTAT.pcount, sizeof(iod_info *));
	if(!vec) {
		PERROR(SUBSYS_META,"malloc");
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}

	vec_count = 0;
	if (AK_PSTAT.base + AK_PSTAT.pcount <= fs_p->nr_iods) {
		for (j = 0; j < AK_PSTAT.pcount; j++) {
			vec[j] = &fs_p->iod[AK_PSTAT.base + j];
			//sockio_dump_sockaddr(&vec[j]->addr, stderr);
		}
		vec_count += j;
	}
	else {
		/* otherwise it's going to take multiple xfers */
		pcnt_remain = AK_PSTAT.pcount;
		if (AK_PSTAT.base) /* send partial list */ {
			nr_infos = fs_p->nr_iods - AK_PSTAT.base;
			for (j = 0; j < nr_infos; j++) {
				vec[j + vec_count] = &fs_p->iod[AK_PSTAT.base + j];
				//sockio_dump_sockaddr(&vec[j + vec_count]->addr, stderr);
			}
			vec_count += nr_infos;
			pcnt_remain -= nr_infos;
		}
		while (pcnt_remain >= fs_p->nr_iods) /* send the whole list */ {
			for (j = 0; j < fs_p->nr_iods; j++) {
				vec[j + vec_count] = &fs_p->iod[j];
				//sockio_dump_sockaddr(&vec[j + vec_count]->addr, stderr);
			}
			vec_count += fs_p->nr_iods;
			pcnt_remain -= fs_p->nr_iods;
		}
		if (pcnt_remain > 0) /* send list part, starting w/iod 0 */ {
			for (j = 0; j < pcnt_remain; j++) {
				vec[j + vec_count] = &fs_p->iod[j];
				//sockio_dump_sockaddr(&vec[j + vec_count]->addr, stderr);
			}
			vec_count += pcnt_remain;
		}
	}
	
	if (vec_count != AK_PSTAT.pcount) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "BADNESS: do_open_ack vec_count = %d, AK_PSTAT.pcount = %d\n",
				vec_count, AK_PSTAT.pcount);
	}
	ackdata_p->type = MGR_OPEN;
	ackdata_p->u.open.niods = AK_PSTAT.pcount;
	ackdata_p->u.open.iod = vec;
	return(0);
}

static int do_gethashes(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	fsinfo_p fs_p;
	finfo_p f_p;
	int fd, ret;
	fmeta meta;

	/* check for reserved name first */
	if (resv_name(data_p) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}
	/* mount the file system */
	if ((fs_p = quick_mount(data_p, req_p->uid, req_p->gid, ack_p, ackdata_p)) == NULL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META, "do_open: quick_mount: NULL returned for %s\n", (char *) data_p);
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	/* obtain the file's meta data */
	fd = meta_open(data_p, O_RDONLY);
	/* huh? the file does not exist */
	if (fd < 0) {
		PERROR(SUBSYS_META,"do_gethashes: meta_open");
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	/* checking permission is pretty cheesy here, since credentials are faked */
	if (meta_access(fd, data_p, req_p->uid, req_p->gid, R_OK) < 0) {
		PERROR(SUBSYS_META,"do_gethashes: meta_access");
		ack_p->status = -1;
		ack_p->eno = errno;
		meta_close(fd);
		return 0;
	}
	/* Something would be fishy if all these tests were to fail */
	if (meta_read(fd, &meta) < 0) {
		PERROR(SUBSYS_META,"do_gethashes: meta_read");
		ack_p->status = -1;
		ack_p->eno = errno;
		meta_close(fd);
		return 0;
	}
	memcpy(&ack_p->ack.gethashes.meta, &meta, sizeof(meta));
	meta_close(fd);
	/* search for the file. it must be open!! */
	f_p = f_search(fs_p->fl_p, meta.u_stat.st_ino);
	if (f_p == NULL) {
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	ackdata_p->type = MGR_GETHASHES;
	ackdata_p->u.gethashes.nhashes = req_p->req.gethashes.nchunks;
	/* now RD lock the hashes file and obtain the requested hashes if the consistency policy requires so*/
	f_rdlock(f_p);
	ret = meta_hash_read(data_p, req_p->req.gethashes.begin_chunk,
			&ackdata_p->u.gethashes.nhashes, &ackdata_p->u.gethashes.hashes);
	/* now unlock the hashes file and obtain the requested hashes if the consistency policy requires so*/
	f_unlock(f_p);

	if (ackdata_p->u.gethashes.nhashes < 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "BADNESS in gethashes.nhashes = %Ld for %s\n",
				ackdata_p->u.gethashes.nhashes, (char *)data_p);
	}

	LOG(stderr, DEBUG_MSG, SUBSYS_META, "gethash on %s from %Ld yielded %Ld hashes\n", (char *)data_p,
			req_p->req.gethashes.begin_chunk, ackdata_p->u.gethashes.nhashes);
#ifdef VERBOSE_DEBUG
	{
		int i;
		for (i = 0; i < ackdata_p->u.gethashes.nhashes; i++) {
			char str[256];

			hash2str(ackdata_p->u.gethashes.hashes + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH, str);
			LOG(stderr, DEBUG_MSG, SUBSYS_META, "%d: %s\n", i, str);
		}
	}
#endif
	ack_p->status = ret;
	if (ack_p->status) {
		ack_p->eno = errno;
	}
	else {
		ack_p->eno = 0;
		ack_p->ack.gethashes.nhashes = ackdata_p->u.gethashes.nhashes;
	}
	return 0;
}

/*
 * returns 1 if the presented hash matches with what we have on disk
 * and 0 otherwise.
 */
static int compare_presented_hash(int64_t presented_hash_len, unsigned char **presented_hashes,
		int64_t current_hash_len, unsigned char *current_hashes)
{
	int64_t i;
	
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "Presented old hash length = %Ld, current hash length = %Ld\n",
			presented_hash_len, current_hash_len);
	/* lengths did not match. we must have raced. return that it did not match */
	if (presented_hash_len != current_hash_len) {
		return 0;
	}
	/* cool, now check individual values */
	for (i = 0; i < presented_hash_len; i++) {
		/* did not match */
		if (memcmp(presented_hashes[i], current_hashes + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH)) 
		{
			return 0;
		}
	}
	/* yippee! we did not race */
	return 1;
}

static int do_wcommit(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p)
{
	fsinfo_p fs_p;
	finfo_p f_p;
	int fd, ret;
	fmeta meta;

	/* check for reserved name first */
	if (resv_name(data_p) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}
	/* mount the file system */
	if ((fs_p = quick_mount(data_p, req_p->uid, req_p->gid, ack_p, ackdata_p)) == NULL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META, "do_open: quick_mount: NULL returned for %s\n", (char *) data_p);
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	/* obtain the file's meta data */
	fd = meta_open(data_p, O_RDONLY);
	/* huh? the file does not exist */
	if (fd < 0) {
		PERROR(SUBSYS_META,"do_gethashes: meta_open");
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	/* checking permission is pretty cheesy here, since credentials are faked */
	if (meta_access(fd, data_p, req_p->uid, req_p->gid, R_OK) < 0) {
		PERROR(SUBSYS_META,"do_gethashes: meta_access");
		ack_p->status = -1;
		ack_p->eno = errno;
		meta_close(fd);
		return 0;
	}
	/* Something would be fishy if all these tests were to fail */
	if (meta_read(fd, &meta) < 0) {
		PERROR(SUBSYS_META,"do_gethashes: meta_read");
		ack_p->status = -1;
		ack_p->eno = errno;
		meta_close(fd);
		return 0;
	}
	memcpy(&ack_p->ack.wcommit.meta, &meta, sizeof(meta));
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "meta_read %s: [atime: %llu] [mtime: %llu] [ctime: %llu]\n", 
			(char *) data_p, meta.u_stat.atime, meta.u_stat.mtime, meta.u_stat.ctime);
	meta_close(fd);
	/* search for the file. it must be open!! */
	f_p = f_search(fs_p->fl_p, meta.u_stat.st_ino);
	if (f_p == NULL) {
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "Thread %lu about to wcommit for file %Ld\n", pthread_self(), meta.u_stat.st_ino);
	/* obtain a write lock on f_p */
	f_wrlock(f_p);
	do {
		int hcache_optimization = 0;
		/* 
		 * Try to read the specified number of hashes from disk.
		 * Note that this cannot be old_hash_len since the client's
		 * knowledge of the file size may have changed since the time
		 * it was issued.
		 */
		ackdata_p->u.wcommit.current_hash_len = ackdata_p->u.wcommit.new_hash_len;
		ret = meta_hash_read(data_p, req_p->req.wcommit.begin_chunk,
				&ackdata_p->u.wcommit.current_hash_len, &ackdata_p->u.wcommit.current_hashes);
		if (ret < 0) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "wcommit: Could not read current set of hashes: %s\n",
					strerror(errno));
			ack_p->status = -1;
			ack_p->eno = errno;
			break;
		}
		/*
		 * if the meta-data recipe indicates that there are no hashes to be served for a particular
		 * range of file, it is a safe bet that no client nodes are going to have that cached.
		 * So we can save on sending a whole bunch of hcache updates and/or invalidates
		 * when a file/files are just being created for instance by a whole bunch of writers.
		 */
		hcache_optimization = 0;
		if (ackdata_p->u.wcommit.current_hash_len == 0) {
			hcache_optimization = 1;
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "WCOMMIT [CB %d] desire_hcache_coherence = %d force_wcommit = %d File %s from chunk %Ld"
				" hcache_optimization = %d\n", ackdata_p->u.wcommit.owner_cbid, ackdata_p->u.wcommit.desire_hcache_coherence,
				ackdata_p->u.wcommit.force_commit, (char *) data_p, req_p->req.wcommit.begin_chunk, 
				hcache_optimization);

		LOG(stderr, DEBUG_MSG, SUBSYS_META, "WCOMMIT [CB %d] yielded %Ld CURRENT hashes\n",
			ackdata_p->u.wcommit.owner_cbid, ackdata_p->u.wcommit.current_hash_len);
#ifdef VERBOSE_DEBUG
		{
			int i;
			for (i = 0; i < ackdata_p->u.wcommit.current_hash_len; i++) 
			{
				char str[256];

				hash2str(ackdata_p->u.wcommit.current_hashes + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH, str);
				LOG(stderr, DEBUG_MSG, SUBSYS_META, "%d: %s\n", i, str);
			}
		}
#endif
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "WCOMMIT [CB %d] presented %Ld OLD hashes\n",
			ackdata_p->u.wcommit.owner_cbid, ackdata_p->u.wcommit.old_hash_len);
#ifdef VERBOSE_DEBUG
		{
			int i;
			for (i = 0; i < ackdata_p->u.wcommit.old_hash_len; i++) 
			{
				char str[256];

				hash2str(ackdata_p->u.wcommit.old_hashes[i], CAPFS_MAXHASHLENGTH, str);
				LOG(stderr, DEBUG_MSG, SUBSYS_META, "%d: %s\n", i, str);
			}
		}
#endif
		/* Check if clients are requesting the commits to be forced!! */
		if (ackdata_p->u.wcommit.force_commit == 1)
		{
			ret = 1;
		}
		/* Else we check if the presented hashes match in length & values with what we have on disk! */
		else {
			ret = compare_presented_hash(ackdata_p->u.wcommit.old_hash_len,
					ackdata_p->u.wcommit.old_hashes, ackdata_p->u.wcommit.current_hash_len, 
					ackdata_p->u.wcommit.current_hashes);
		}
		/* If they don't we ask clients to retry with the current values of the hashes */
		if (ret == 0) 
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "WCOMMIT [CB %d]: presented hashes don't match with what we have on disk!\n",
					ackdata_p->u.wcommit.owner_cbid);
			ack_p->status = -1;
			ack_p->eno = EAGAIN; /* special error number asking clients to retry */
			ack_p->ack.wcommit.nhashes = ackdata_p->u.wcommit.current_hash_len;
			break;
		}
		/* old hashes matched. so we succeed */
		else 
		{
			LOG(stderr, DEBUG_MSG, SUBSYS_META, "WCOMMIT [CB %d] presented %Ld NEW hashes\n",
				ackdata_p->u.wcommit.owner_cbid, ackdata_p->u.wcommit.new_hash_len);
#ifdef VERBOSE_DEBUG
			{
				int i;
				/* debug */
				for (i = 0; i < ackdata_p->u.wcommit.new_hash_len; i++) {
					char str[256];

					hash2str(ackdata_p->u.wcommit.new_hashes[i], CAPFS_MAXHASHLENGTH, str);
					LOG(stderr, DEBUG_MSG, SUBSYS_META, "%d: %s\n", i, str);
				}
			}
#endif
			/*
			 * If they do, we write the new hashes to the file 
			 */
			ret = meta_hash_write(data_p, req_p->req.wcommit.begin_chunk,
					ackdata_p->u.wcommit.new_hash_len, ackdata_p->u.wcommit.new_hashes);
			if (ret < 0) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "wcommit: could not write new set of hashes to disk!: %s\n",
						strerror(errno));
				ack_p->status = -1;
				ack_p->eno = errno;
				break;
			}
			/* if the write was successful, we free up the current hashes thingie and re-read the current hashes */
			free(ackdata_p->u.wcommit.current_hashes);
			ackdata_p->u.wcommit.current_hashes = NULL;
			/* Re-read the current set of hashes */
			ackdata_p->u.wcommit.current_hash_len = ackdata_p->u.wcommit.new_hash_len;
			ret = meta_hash_read(data_p, req_p->req.wcommit.begin_chunk,
					&ackdata_p->u.wcommit.current_hash_len, &ackdata_p->u.wcommit.current_hashes);
			if (ret < 0) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "wcommit: could not re-read the old set of hashes from disk!: %s\n",
						strerror(errno));
				ack_p->status = -1;
				ack_p->eno = errno;
				break;
			}
			LOG(stderr, DEBUG_MSG, SUBSYS_META, "WCOMMIT [CB %d]: reading back %Ld CURRENT hashes\n",
					ackdata_p->u.wcommit.owner_cbid, ackdata_p->u.wcommit.current_hash_len);
#ifdef VERBOSE_DEBUG
			{
				int i;
				for (i = 0; i < ackdata_p->u.wcommit.current_hash_len; i++) {
					char str[256];

					hash2str(ackdata_p->u.wcommit.current_hashes + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH, str);
					LOG(stderr, DEBUG_MSG, SUBSYS_META, "%d: %s\n", i, str);
				}
			}
#endif
			ack_p->status = 0;
			ack_p->eno = 0;
			ack_p->ack.wcommit.nhashes = ackdata_p->u.wcommit.current_hash_len;
			/* go ahead and set the access and modification time */
			meta.u_stat.mtime = time(NULL);
			meta.u_stat.atime = time(NULL);
			/* write the meta data of the file with the updated file's size */
			if (meta.u_stat.st_size < req_p->req.wcommit.write_size) 
			{
				meta.u_stat.st_size = req_p->req.wcommit.write_size;
				LOG(stderr, INFO_MSG, SUBSYS_META, "%s: Updating new file size on disk to %Ld [mtime: %llu, atime: %llu]\n",
						(char *) data_p, req_p->req.wcommit.write_size, meta.u_stat.mtime, meta.u_stat.atime);
			}
			else {
				LOG(stderr, INFO_MSG, SUBSYS_META, "%s: Not updating file size on disk [mtime: %llu, atime: %llu]\n",
						(char *) data_p, meta.u_stat.mtime, meta.u_stat.atime);
			}
			fd = meta_open(data_p, O_RDWR);
			/* huh? the file does not exist */
			if (fd < 0) {
				PERROR(SUBSYS_META,"do_wcommit: meta_open");
				ack_p->status = -1;
				ack_p->eno = errno;
				break;
			}
			/* checking permission is pretty cheesy here, since credentials are faked */
			if (meta_access(fd, data_p, req_p->uid, req_p->gid, R_OK | W_OK) < 0) {
				PERROR(SUBSYS_META,"do_wcommit: meta_access");
				ack_p->status = -1;
				ack_p->eno = errno;
				meta_close(fd);
				break;
			}
			/* Something would be fishy if all these tests were to fail */
			if (meta_write(fd, &meta) < 0) {
				PERROR(SUBSYS_META,"do_wcommit: meta_write");
				ack_p->status = -1;
				ack_p->eno = errno;
				meta_close(fd);
				break;
			}
			memcpy(&ack_p->ack.wcommit.meta, &meta, sizeof(meta));
			meta_close(fd);
			/* If we are asked to send hcache updates/invalidates and if we think it is necessary, then we do so now */
			if (ackdata_p->u.wcommit.desire_hcache_coherence == 1 && hcache_optimization == 0)
			{
				unsigned long bitmap;
				int position;
				char *fname = NULL;

				bitmap = get_callbacks(meta.fs_ino, meta.u_stat.st_ino, &fname);
				LOG(stderr, DEBUG_MSG, SUBSYS_META, "WCOMMIT [CB %d] Obtained callbacks for <%Ld,%Ld> -> 0x%lx\n", 
					ackdata_p->u.wcommit.owner_cbid, meta.fs_ino, meta.u_stat.st_ino, bitmap);
				/* 
				 * wcommit commits a set of hashes for <fname> from <arg1.begin_chunk> for <arg1.new_hashes.sha1_hashes_len> 
				 * If there is only 1 other sharer, then we choose to send hcache updates
				 * rather than hcache invalidates.
				 * We choose to ignore errors here...
				 * can fname  == NULL here?
				 */
				if (fname)
				{
					if ((position = OnlyOtherBitSet((unsigned char *)&bitmap, 4, ackdata_p->u.wcommit.owner_cbid)) < 0) 
					{
						LOG(stderr, DEBUG_MSG, SUBSYS_META, "WCOMMIT [CB %d] Starting to send invalidates\n", ackdata_p->u.wcommit.owner_cbid);
						cb_invalidate_hashes(fname, bitmap, ackdata_p->u.wcommit.owner_cbid,
								req_p->req.wcommit.begin_chunk, ackdata_p->u.wcommit.new_hash_len);
						LOG(stderr, DEBUG_MSG, SUBSYS_META, "Finished sending invalidates\n");
					}
					/* if there is only 1 bit set in the bitmap in addition to us, we can do a hash update */
					else {
						LOG(stderr, DEBUG_MSG, SUBSYS_META, "WCOMMIT [CB %d] Starting to send updates to %d\n", ackdata_p->u.wcommit.owner_cbid,
								position);
						cb_update_hashes(fname, position, req_p->req.wcommit.begin_chunk, 
								ackdata_p->u.wcommit.current_hash_len, ackdata_p->u.wcommit.current_hashes);
						LOG(stderr, DEBUG_MSG, SUBSYS_META, "Finished sending updates\n");
					}
				}
				else 
				{
					LOG(stderr, WARNING_MSG, SUBSYS_META, "WCOMMIT [CB %d] found NULL fname (%s?)!!!\n", ackdata_p->u.wcommit.owner_cbid, (char *) data_p);
				}
			}
			else if (hcache_optimization == 1)
			{
				LOG(stderr, DEBUG_MSG, SUBSYS_META, "WCOMMIT [CB %d] on file %s from %Ld for %Ld chunks hcache optimization\n",
					ackdata_p->u.wcommit.owner_cbid, (char *)data_p, req_p->req.wcommit.begin_chunk, ackdata_p->u.wcommit.new_hash_len);
			}
			else
			{
				LOG(stderr, DEBUG_MSG, SUBSYS_META, "WCOMMIT [CB %d] on file %s from %Ld for %Ld chunks skipping hcache coherence\n",
					ackdata_p->u.wcommit.owner_cbid, (char *)data_p, req_p->req.wcommit.begin_chunk, ackdata_p->u.wcommit.new_hash_len);
			}
		}
	} while (0);
	/* unlock the file */
	f_unlock(f_p);
	return 0;
}

#undef RQ_PSTAT
#undef AK_PSTAT

static int do_umount(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	int ret;
	fsinfo_p fs_p;
	dmeta dir;

	/* check for reserved name first */
	if (resv_name(data_p) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}

	/* find filesystem & check for mounted */
	if (get_dmeta(data_p, &dir) < 0) {
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}

	if ((fs_p = fs_search(active_p, dir.fs_ino)) == NULL) {
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}

	/* shut down all IODs */
	if ((ret = stop_iods(fs_p)) < 0) {
		ack_p->status = ret;
		ack_p->eno = errno;
		return 0;
	}
	/* close any open files (?) */

	/* remove entry for filesystem */
	fs_rem(active_p, dir.fs_ino);
	return 0;
}

static int do_shutdown(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p)
{
	forall_fs(active_p, stop_iods);
	LOG(stderr, DEBUG_MSG, SUBSYS_META, " Shut down all iods\n");
	return(0);
}

static int do_rename(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p)
{
	char *c_p = data_p, temp[MAXPATHLEN], *old_name, *new_name;
	int fd, length;

	/* set pointers to old and new filenames */
	old_name = c_p;
	for (;*c_p;c_p++); /* theres got to be a better way */
	new_name = ++c_p;

	/* check for reserved names first */
	if (resv_name(old_name) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}

	if (resv_name(new_name) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}

	/* check metadat for permissions of old file */
	if ((fd = meta_open(old_name, O_RDONLY)) < 0) {
		PERROR(SUBSYS_META,"do_rename: meta_open");
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	
	/* check permissions */
	if (meta_access(fd, old_name, req_p->uid, req_p->gid, R_OK | W_OK) < 0) {
		meta_close(fd);
		PERROR(SUBSYS_META,"do_rename: meta_access (old name)");
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	if (meta_close(fd) < 0) {
		PERROR(SUBSYS_META,"do_rename: meta_close");
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	/* check for write/execute permissions on parent directory */
	strncpy(temp, new_name, MAXPATHLEN);
	length = get_parent(temp);
	/* if length < 0, CWD used, directory permissions not checked */
	if (length >= 0) {
		if (meta_access(0, temp, req_p->uid, req_p->gid, X_OK|W_OK) < 0) { 
			PERROR(SUBSYS_META,"do_rename: meta_access (new name)");
			ack_p->status = -1;
			ack_p->eno = errno;
			return 0;
		}
	}

	/* just try to remove the new file and see if it works */
	/* FIXME: Shouldnt we also delete this file on the IOD's? */
   if (unlink(new_name) < 0) {
		if (errno != ENOENT) {
			PERROR(SUBSYS_META,"do_rename: unlink");
			ack_p->status = -1;
			ack_p->eno = errno;
			return 0;
		}
	}
	/* also unlink the hash file */
	else {
		char hashpath[MAXPATHLEN];

		sprintf(hashpath, "%s.hashes", new_name);
		unlink(hashpath);
	}
	/* move to new name */
   if (rename(old_name, new_name) < 0) {
      PERROR(SUBSYS_META,"do_rename: rename");
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
   }
	else {
		char oldhashpath[MAXPATHLEN], hashpath[MAXPATHLEN];

		sprintf(oldhashpath, "%s.hashes", old_name);
		sprintf(hashpath, "%s.hashes", new_name);
		rename(oldhashpath, hashpath);
	}
	return(0); 
}

/* Create a hard or soft link */
static int do_link(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p)
{
	char *c_p = data_p, *target_name, *link_name;
	int soft;

#ifndef ENABLE_HARDLINKS
	if (req_p->req.link.soft == 0)
	{
		ack_p->status = -1;
		ack_p->eno = ENOSYS;
		return 0;
	}
#endif

	/* set pointers to target and link filenames */
	link_name = c_p;
	for (;*c_p;c_p++); /* theres got to be a better way */
	target_name = ++c_p;
	soft = req_p->req.link.soft;
	if (soft) {
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "Try to create soft link from %s->%s\n", link_name, target_name);
	}
	else {
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "Try to create hard link from %s->%s\n", link_name, target_name);
	}

	/*
	 * Check for reserved names!
	 * FIXME: Should a soft link allow for  non-existent reserved target_name?
	 */
	if (resv_name(link_name) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}
	if (resv_name(target_name) != 0) {
 		ack_p->status = -1;
 		ack_p->eno = ENOENT;
		return 0;
	}
	/* Now we try to create the hard/soft link */
	if (soft == 1) {
		/* create a symbolic link */
		if (md_symlink(link_name, target_name, req_p) < 0) {
			PERROR(SUBSYS_META,"do_link: md_symlink");
			ack_p->status = -1;
			ack_p->eno = errno;
			return 0;
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "Created soft link from %s->%s\n", link_name, target_name);
	}
	else {
		if (md_link(link_name, target_name, req_p) < 0) {
			PERROR(SUBSYS_META,"do_link: md_link");
			ack_p->status = -1;
			ack_p->eno = errno;
			return 0;
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "Created hard link from %s->%s\n", link_name, target_name);
	}
	/* Change the ownership */
	if (lchown(link_name, req_p->req.link.meta.u_stat.st_uid,
				  req_p->req.link.meta.u_stat.st_gid) < 0)
	{
		PERROR(SUBSYS_META,"do_link: lchown");
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	return(0); /* no errors, no ack to send */
} /* end of do_link() */

/* try to read the link */
static int do_readlink(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p)
{
	char *link_name = NULL, *readlink_name = NULL;
	/* set pointers to target and link filenames */
	link_name = (char *)data_p;

	if(!link_name) {
		ack_p->status = -1;
		ack_p->eno = EINVAL;
		return 0;
	}
	if (resv_name(link_name) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}
	readlink_name = md_readlink(link_name);
	if(!readlink_name) {
		PERROR(SUBSYS_META,"do_readlink: md_readlink");
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "readlink of %s yielded %s\n", link_name, readlink_name);
	/* ok great. Now send it back */
	ack_p->dsize = strlen(readlink_name);
	ackdata_p->type = MGR_READLINK;
	ackdata_p->u.readlink.link_name = readlink_name;
	return (0);
}


/* do_iod_info() - returns information on iods
 */
static int do_iod_info(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p)
{
	fsinfo_p fs_p;

	/* make sure we can talk to the iods */
	if ((fs_p = quick_mount(data_p, req_p->uid, req_p->gid, ack_p, ackdata_p)) == NULL)
	{
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}

	ack_p->ack.iod_info.nr_iods = fs_p->nr_iods;
	ack_p->dsize  = fs_p->nr_iods*sizeof(iod_info);
	ackdata_p->type = MGR_IOD_INFO;
	ackdata_p->u.iodinfo.niods = fs_p->nr_iods;
	ackdata_p->u.iodinfo.iod = fs_p->iod;

	return(0);
}

/* do_mkdir() - makes a new directory in the file system
 */
static int do_mkdir(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	char temp[MAXPATHLEN];
	int length;
	dmeta dir;

	/* check for reserved name first */
	if (resv_name(data_p) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}

	/* check permissions to make this directory */
	strncpy(temp, data_p, MAXPATHLEN);
	length = get_parent(temp);
	if (length >= 0) {
		/* check for permissions to write to directory */
		if (meta_access(0, temp, req_p->uid, req_p->gid, X_OK | W_OK) < 0) {
			ack_p->status = -1;
			ack_p->eno = errno;
			PERROR(SUBSYS_META,"do_mkdir: meta_access");
			return 0;
		}
	}

	/* get metadata of the dir this is going into */
	if (get_dmeta(data_p, &dir) < 0) {
		errno = ENOENT;
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	
	/* make the new directory */
	if ((ack_p->status = mkdir(data_p, 0775)) < 0) {
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	/* set up the dotfile in the new directory */
	dir.dr_uid = req_p->uid;
	if (!(S_ISGID & dir.dr_mode))
		dir.dr_gid = req_p->gid;
	dir.dr_mode = req_p->req.mkdir.mode | (S_ISGID & dir.dr_mode) | S_IFDIR;

	/* NOTE:
	 * It's important to use md_mkdir() here instead of put_dmeta;
	 * md_mkdir() understands to use the fname portion of the dmeta
	 * structure as part of the name of the directory, so it will get the
	 * sd_path field correct.  put_dmeta() will not. -- NOT ANY MORE
	 */
	if (md_mkdir(data_p, &dir) < 0) {
		errno = ENOENT;
		ack_p->status = -1;
		ack_p->eno = errno;
		return 0;
	}
	return(0);
}

/* do_fchown() - chowns based on file descriptor
 */
static int do_fchown(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	mreq fakereq;
	fsinfo_p fs_p;
	finfo_p f_p;

	/* find filesystem & open file */
	if (!(fs_p = fs_search(active_p, req_p->req.fchown.fs_ino))
		|| !(f_p = f_search(fs_p->fl_p, req_p->req.fchown.file_ino)))
	{
		ack_p->status = -1;
		ack_p->eno  = errno;
		return 0;
	}

	/* build a fake chown request, call do_chown() */
	fakereq.majik_nr   = MGR_MAJIK_NR;
	fakereq.release_nr = CAPFS_RELEASE_NR;
	fakereq.type       = MGR_CHOWN;
	fakereq.uid        = req_p->uid;
	fakereq.gid        = req_p->gid;
	fakereq.dsize      = strlen(f_p->f_name);
	fakereq.req.chown.owner = req_p->req.fchown.owner;
	fakereq.req.chown.group = req_p->req.fchown.group;

	return(do_chown(&fakereq, f_p->f_name, ack_p, ackdata_p));
}

/* do_fchmod() - chmods based on file descriptor
 */
static int do_fchmod(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	mreq fakereq;
	fsinfo_p fs_p;
	finfo_p f_p;

	/* find filesystem & open file */
	if (!(fs_p = fs_search(active_p, req_p->req.fchmod.fs_ino))
	|| !(f_p = f_search(fs_p->fl_p, req_p->req.fchmod.file_ino)))
	{
		ack_p->status = -1;
		ack_p->eno  = errno;
		return 0;
	}
	/* build a fake chmod request, call do_chmod() */
	fakereq.majik_nr   = MGR_MAJIK_NR;
	fakereq.release_nr = CAPFS_RELEASE_NR;
	fakereq.type       = MGR_CHMOD;
	fakereq.uid        = req_p->uid;
	fakereq.gid        = req_p->gid;
	fakereq.dsize      = strlen(f_p->f_name);
	fakereq.req.chmod.mode = req_p->req.fchmod.mode;

	return(do_chmod(&fakereq, f_p->f_name, ack_p, ackdata_p));
}

/* do_rmdir() - removes a directory in the file system
 */
static int do_rmdir(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	/* check for reserved name first */
	if (resv_name(data_p) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}

	ack_p->status = md_rmdir(req_p, (char *) data_p);
	ack_p->eno  = errno;
	return(0);
}

/* DO_GETDENTS() - opens a directory, seeks to the appropriate location,
 * reads directory data out, and pushes it back through the socket.
 *
 * The common case for this with the kernel module is a request for a
 * single entry.  The common case for this for the library is somewhat
 * larger.  We need to strip out the reserved names from the output
 * here.
 *
 * Client expects a certain number of pvsf_dirent structures, not
 * the actual system dirents.  Offset is an opaque pointer on the
 * client, we use it to find where we were in the directory listing.
 */
static int do_getdents(mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata *ackdata_p) 
{
	int i = -1, myerr = 0, ret;
	struct dirent dir;
	struct capfs_dirent *pdir;
	int num_pdir, opos;
	off_t off;

	/* check for reserved name first */
	if (resv_name(data_p) != 0) {
		ack_p->status = -1;
		ack_p->eno = ENOENT;
		return 0;
	}
	/* do permission checking */
	if (meta_access(0, data_p, req_p->uid, req_p->gid, R_OK) < 0) {
		ack_p->status = -1;                                             
		ack_p->eno = errno;                                             
		return 0;
	}

	/* debugging check */
	if (req_p->req.getdents.length % sizeof(struct capfs_dirent)) {
		ack_p->status = -1;
		ack_p->eno = EINVAL;
		return 0;
	}
	/* return buffer to client */
	num_pdir = req_p->req.getdents.length / sizeof(struct capfs_dirent);

	if (!(pdir = malloc(req_p->req.getdents.length))) {
		goto do_getdents_error;
	}

	memset(pdir, 0, req_p->req.getdents.length);

	if ((i = open(data_p, O_RDONLY)) < 0) {
		goto do_getdents_error;
	}

	off = req_p->req.getdents.offset;

	/*
	 * Read into the buffer until we get at least one unfiltered entry,
	 * hit EOF, or get an error of some kind.  Just go one at a time
	 * and copy into capfs_dirent until it is full.
	 */
	for (opos=0; opos < num_pdir; ) {
		/* seek to our starting position */
		if (lseek(i, off, SEEK_SET) < 0) {
			goto do_getdents_error;
		}

		if ((ret = syscall(SYS_getdents, i, &dir, sizeof(dir))) < 0) {
			goto do_getdents_error;
		}
		if (ret == 0) {
			break;
		}

		if (!resv_name(dir.d_name)) {
			pdir[opos].handle = dir.d_ino;
			pdir[opos].off = off;
			strcpy(pdir[opos].name, dir.d_name);
			++opos;
		}
		off = dir.d_off;
	}

	/* send back the final position */
	ack_p->status = 0;
	ack_p->eno = 0;
	ack_p->ack.getdents.offset = off;
	ack_p->dsize = opos * sizeof(struct capfs_dirent);
	if (opos) {
		ackdata_p->type = MGR_GETDENTS;
		ackdata_p->u.getdents.nentries = opos;
		ackdata_p->u.getdents.pdir = pdir;
	}
	close(i);
	return(0);

do_getdents_error:
	myerr = errno;
	free(pdir);
	if (i >= 0) close(i);
	ack_p->dsize = 0;
	errno = myerr;
	ack_p->status = -1;
	ack_p->eno = errno;
	return 0;
}

/* DO_ALL_IMPLICIT_CLOSES(),
 * DO_IMPLICIT_CLOSES(),
 * CHECK_FOR_IMPLICIT_CLOSE()
 *
 * These functions are used to clean up after applications when they
 * exit.  All files that are open are checked to see if they were opened
 * by the application holding open the socket, and if so the files are
 * "closed" (reference count is decremented and files are closed if
 * count reaches zero).
 */
#if 0
static int do_all_implicit_closes(int sock) 
{
	iclose_sock = sock;
	forall_fs(active_p, do_implicit_closes);
	return(0);
}

static int do_implicit_closes(void *v_p) 
{
	fsinfo_p fs_p = (fsinfo_p) v_p;
	iclose_fsp = fs_p;
	forall_finfo(fs_p->fl_p, check_for_implicit_close);
	return(0);
}

static int check_for_implicit_close(void *v_p) 
{
	finfo_p f_p = (finfo_p) v_p;
	mreq req;
	ireq iodreq;
	int ret, i;
	int64_t max_modtime = 0;
	
	memset(&iodreq, 0, sizeof(iodreq));

	if (dfd_isset(iclose_sock, &f_p->socks)) {
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "socket %d found in mask for file %Ld\n", iclose_sock, f_p->f_ino);
		if (--f_p->cnt > 0) /* not the last reference */ {
			dfd_clr(iclose_sock, &f_p->socks);
		}
		else /* need to close file on IODs */ {
			LOG(stderr, DEBUG_MSG, SUBSYS_META, "file %Ld implicitly closed\n", f_p->f_ino);
			/* send request to close to IODs */
			iodreq.majik_nr        = IOD_MAJIK_NR;
			iodreq.release_nr      = CAPFS_RELEASE_NR;
			iodreq.type            = IOD_CLOSE;
			iodreq.dsize           = 0;
			iodreq.req.close.f_ino = f_p->f_ino;
			/* Tells iod to close all instances of open file */
			iodreq.req.close.cap = -1; 
			
			if ((ret = (send_req(iclose_fsp->iod, iclose_fsp->nr_iods,
				f_p->p_stat.base, f_p->p_stat.pcount, &iodreq, NULL, NULL)) < 0))
			{
				PERROR(SUBSYS_META,"send_req failed during implicit close\n");
				return(-1);
			}

			/* find the latest modification time as returned from the iods */
			for (i = 0; i < f_p->p_stat.pcount; i++) {
				if (iclose_fsp->iod[i].ack.status == 0
				|| iclose_fsp->iod[i].ack.eno != ENOENT) {
					if (max_modtime < iclose_fsp->iod[i].ack.ack.close.modtime)
						max_modtime = iclose_fsp->iod[i].ack.ack.close.modtime;
				}
			}
			/* handle case where utime event occurred after last modification */
			if (f_p->utime_event > max_modtime) {
				req.req.close.meta.u_stat.mtime = f_p->utime_modtime;
			}
			else {
				req.req.close.meta.u_stat.mtime = max_modtime;
			}
			req.req.close.meta.u_stat.atime = time(NULL);
			/* store the time values in the metadata file */
#ifdef MGR_USE_CACHED_FILE_SIZE
			md_close(&req, f_p->f_name, iclose_fsp);
#else
			md_close(&req, f_p->f_name);
#endif

			if (f_p->unlinked >= 0) /* tell IODs to remove the file too */ {
				iodreq.majik_nr         = IOD_MAJIK_NR;
				iodreq.release_nr       = CAPFS_RELEASE_NR;
				iodreq.type             = IOD_UNLINK;
				iodreq.dsize            = 0;
				iodreq.req.unlink.f_ino = f_p->f_ino;
				if ((ret = (send_req(iclose_fsp->iod, iclose_fsp->nr_iods,
					f_p->p_stat.base, f_p->p_stat.pcount, &iodreq, NULL,
					NULL)) < 0))
				{
					PERROR(SUBSYS_META,"send_req failed during implicit close (unlink)\n");
					return(-1);
				}
			}

			f_rem(iclose_fsp->fl_p,f_p->f_ino); /* wax file info */
		}
	}
	return(0);
}
#endif

/*
 * quick_mount() - checks if file system holding a given file is mounted;
 * if not, it mounts it
 */
static fsinfo_p quick_mount(char *fname, int uid, int gid, mack_p ack_p, struct ackdata *ackdata_p) 
{
	int ret;
	mreq fakereq;
	fsinfo_p fs_p;
	dmeta dir;
	char rd_path[MAXPATHLEN];

	memset(&dir, 0, sizeof(dir));
	memset(&fakereq, 0, sizeof(fakereq));
	memset(rd_path, 0, MAXPATHLEN);

	/* fill in fake request for do_mount() */
	if (get_dmeta(fname, &dir) < 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META, "quick_mount: get_dmeta (%s): ", fname);
		PERROR(SUBSYS_META,"quick_mount failed?");
		return(NULL);
	}

	if ((fs_p = fs_search(active_p, dir.fs_ino)) != 0) 
		return(fs_p);

	LOG(stderr, DEBUG_MSG, SUBSYS_META, "quick_mount: mounting %s\n", dir.rd_path);
	/* Need a / on the end of the root path -- use static buffer to ensure
	 * that we have enough space.  Because dmeta is only good from call to
	 * call, this should be ok (thanks DP).
	 */
	strncpy(rd_path, dir.rd_path, MAXPATHLEN - 2);
	rd_path[strlen(rd_path)] = '/';

	fakereq.type  = MGR_MOUNT;
	fakereq.uid   = uid;
	fakereq.gid   = gid;
	fakereq.dsize = strlen(dir.rd_path);
	/* mount fs */
	if ((ret = do_mount(&fakereq, dir.rd_path, ack_p, ackdata_p)) < 0) {
		PERROR(SUBSYS_META,"quick_mount: do_mount");
		return(NULL);
	}
	/* find filesystem & open file */
	return (fs_search(active_p, dir.fs_ino));
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

