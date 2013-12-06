/*
 * capfs_v1_xfer.c 
 *
 * Heavily modified by Murali Vilayannur (c) 2005 for capfs
 * 
 * original copyright (c) 1999 Rob Ross and Phil Carns,
 * all rights reserved.
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
 *
 * Credits:
 *   23 Aug 01: Pete Wyckoff <pw@osc.edu>: Architecture-independent
 *              getdents changes.
 */

/*
 * This file implements a library of calls which allow VFS-like access
 * to CAPFS file systems.  This is done by mapping the VFS-like
 * operations (as defined in ll_capfs.h) into v1.xx operations, which is
 * not always a 1-to-1 mapping.
 *
 * We also build a list of open files in order to avoid using the
 * wrapper functions in the CAPFS library.  This list is searched via
 * handle and pointer to manager information (sockaddr really).
 *
 * EXTERNALLY AVAILABLE OPERATIONS IMPLEMENTED IN THIS FILE:
 *
 * int capfs_comm_init(void) - called before communication is started
 *    (before do_capfs_op() is called)
 * int do_capfs_op(mgr, up, down) - called to perform any valid CAPFS
 *    VFS operation
 * void capfs_comm_idle(void) - called occasionally to close idle files
 * void capfs_comm_shutdown(void) - called to cleanly shut down
 *    communication before exiting
 *
 * A NOTE ON FILENAMES PASSED TO DO_CAPFS_OP:
 * We needed to have a simple way to get the manager address and port
 * into the functions here, so we have embedded this information into
 * the file names passed in.  The format is <addr>:<port>/<filename>.
 * Examples:
 * foo:3000/capfs
 * localhost:4000/capfs/foo/bar
 * Names such as these are used throughout the do_XXX_op calls and in
 * the code that handles searches for open files.
 *
 * YOU SHOULD ENSURE THE NAME HAS A VALID FORMAT BEFORE PASSING IT IN!!!
 *
 * Three functions are used for working with these names, so if we want
 * to change the format later we can:
 *   static char *skip_to_filename(char *name);
 *   static int name_to_port(char *name);
 *   static void hostcpy(char *host, char *name);
 * The first one returns a pointer to the filename portion of the name.
 * The second returns an integer port number in host byte order.
 * The last one copies the hostname into a buffer of size CAPFSHOSTLEN.
 * 
 */

/* TODO
 *
 * Make capfs_mgr_init() take a pointer to a buffer and store its result
 * in there instead of calloc()ing the space.
 */

/* UNIX INCLUDES */
#include "capfs-header.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <malloc.h>
#include <unistd.h>
#include <assert.h>

#include <linux/types.h>
#include <linux/dirent.h>
#include "capfs_config.h"
#include "ll_capfs.h"
#include "capfs_mount.h"
#include "capfs_v1_xfer.h"
#include "capfs_kernel_config.h"
#include "sockio.h"
#include "sockset.h"
#include "jlist.h"
#include "req.h"
#include "meta.h"
#include "desc.h"
#include "minmax.h"

/* pull in the signatures for the hash server */
#include "mgr.h"
#include "mgr_prot.h"
/* and the local RPC servers */
#include "capfsd_prot.h"
/* and the hash cache prototypes */
#include "hashes.h"
#include "cas.h"
#include "sha.h"
#include "log.h"
/* and the mapping code from blocks to hashes to iods */
#include "map_chunk.h"
/* and the plugin structure's */
#include "plugin.h"

enum {
	/* number of times we'll retry if too many files are open */
	CAPFS_XFER_MAX_RETRY   = 5,
	CAPFS_XFER_RETRY_DELAY = 5
};

/* GLOBALS (for v1 lib. calls) */
extern jlist_p active_p;
extern sockset socks;
extern int capfs_debug, capfs_mode, check_for_registration, my_cb_id;

extern void hcache_get_cb_stats(int64_t *hcache_inv, int64_t *hcache_inv_range, int64_t *hcache_upd);
extern int send_mreq_saddr(struct capfs_options *, struct sockaddr *saddr_p, mreq_p req_p,
	void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
extern int build_rw_jobs(fdesc_p f_p, char *buf_p, int64_t size, int type);
extern int build_simple_jobs(fdesc_p f_p, ireq *req_p);
extern int do_jobs(jlist_p jl_p, sockset_p ss_p, int msec);
extern int jlist_empty(jlist_p jl_p);
extern void cleanup_iodtable(void);
extern int instantiate_iod_entry(struct sockaddr *saddr_p);
extern int add_iodtable(struct sockaddr *saddr_p);
extern int badiodfd(int fd);
extern int find_iodtable(struct sockaddr *saddr_p);
extern int getfd_iodtable(int slot);
extern int getslot_iodtable(int sockfd);
extern int shutdown_alliods(void);
extern int inc_ref_count(int slot);
extern int dec_ref_count(int slot);
extern struct plugin_info* capfsd_match_policy_name(char *policy);
extern struct plugin_info* capfsd_match_policy_id(int id);


/* OPEN FILE MISC. -- all this stuff is internal to this file too */
typedef llist_p pfl_t;

struct pf {
	capfs_handle_t handle;
	char *name;
	time_t ltime; /* last time we used this open file */
	fdesc *fp;
};

struct pf_cmp {
	capfs_handle_t handle;
	char *name;
};

static pfl_t file_list = NULL;

static pfl_t pfl_new(void);
static struct pf *pfl_head(pfl_t pfl);
static int pf_add(pfl_t pfl, struct pf *p);
static void pfl_cleanup(pfl_t pfl);
static struct pf *pf_search(pfl_t pfl, capfs_handle_t handle, char *name);
static void pfl_cleanup(pfl_t pfl);
static int pf_handle_cmp(void *cmp, void *pfp);
static int pf_ltime_cmp(void *time, void *pfp);
static int pf_ltime_olderthan(void *time, void *pfp);
static void pf_free(void *p);
static int pf_rem(pfl_t pfl, capfs_handle_t handle, char *name);
static struct pf *pf_new(fdesc *fp, capfs_handle_t handle, char *name);
static int pf_zerotime(void *pfp);

/* miscellaneous capfs specific mount time options */
struct capfs_specific_options {
	int32_t use_tcp;
	int32_t use_hcache;
	int32_t use_dcache;
	int32_t cons; /* cons will determine a few other things */
};

/* PROTOTYPES FOR INTERNAL FUNCTIONS */
static void init_mgr_req(mreq *rp, struct capfs_upcall *op);
static void init_res(struct capfs_downcall *resp, struct capfs_upcall *op);
static void v1_fmeta_to_v2_meta(struct fmeta *fmeta, struct capfs_meta *meta);
static void v1_fmeta_to_v2_phys(struct fmeta *fmeta, struct capfs_phys *phys);
static int open_capfs_file(struct capfs_specific_options *, struct sockaddr *mgr, capfs_char_t name[]);
static void cas_close_capfs_file(struct capfs_specific_options *, struct sockaddr *mgr, struct pf *pfp);
static int do_generic_op(struct capfs_specific_options *, struct sockaddr *mgr, struct capfs_upcall *op,
	struct capfs_downcall *resp);
static int do_create_op(struct capfs_specific_options *, struct sockaddr *mgr, struct capfs_upcall *op,
	struct capfs_downcall *resp);
static int do_hint_op(struct capfs_specific_options *, struct sockaddr *mgr, struct capfs_upcall *op,
	struct capfs_downcall *resp);
static int do_cas_fsync_op(struct capfs_specific_options *, struct sockaddr *mgr, struct capfs_upcall *op,
struct capfs_downcall *resp);
static int do_setmeta_op(struct capfs_specific_options *, struct sockaddr *mgr, struct capfs_upcall *op,
	struct capfs_downcall *resp, int force_grp_change);
static int do_getdents_op(struct capfs_specific_options *, struct sockaddr *mgr, struct capfs_upcall *op,
	struct capfs_downcall *resp);
static int do_readlink_op(struct capfs_specific_options *, struct sockaddr *mgr, struct capfs_upcall *op,
			  struct capfs_downcall *resp);
static int do_rw_op(struct capfs_specific_options *, struct sockaddr *mgr, struct capfs_upcall *op,
	struct capfs_downcall *resp);
static int do_mkdir_post(struct capfs_specific_options *, struct sockaddr *mgr, struct capfs_upcall *op,
	struct capfs_downcall *resp);
static fdesc *do_open_req(struct capfs_specific_options *, struct sockaddr *mgr, mreq *reqp, capfs_char_t name[],
	int *errp);
static int create_capfs_file(struct capfs_specific_options *options, struct sockaddr *mgr, capfs_char_t name[],
	struct capfs_meta *meta, struct capfs_phys *phys);
static void v2_meta_to_v1_fmeta(struct capfs_meta *meta,
	struct fmeta *fmeta);
static void v2_phys_to_v1_fmeta(struct capfs_phys *phys,
	struct fmeta *fmeta);
static void v1_ack_to_v2_statfs(struct mack *ack, struct capfs_statfs *sfs);
static char *skip_to_filename(char *name);
static int name_to_port(char *name);
static void hostcpy(char *host, char *name);
static struct sockaddr *capfs_mgr_init(capfs_char_t host[], uint16_t port);
static int close_some_files(void);

int64_t n_retries, sha1_time, rpc_get_time, rpc_put_time, rpc_commit_time, get_hashes_time, compute_hashes_time;
extern int64_t server_get_time[CAPFS_STATS_MAX], server_put_time[CAPFS_STATS_MAX];

/* EXPORTED FUNCTIONS */

/* capfs_comm_init()
 *
 * Handle any initialization necessary...
 */
int capfs_comm_init(void)
{
	return 0;
}

/* capfs_mgr_init()
 *
 * Returns a pointer to a dynamically allocated region holding
 * connection information for a given manager.
 */
static struct sockaddr *capfs_mgr_init(capfs_char_t host[], uint16_t port)
{
	struct sockaddr *sp;

	sp = (struct sockaddr *)calloc(1, sizeof(struct sockaddr));
	if (sp == NULL) return NULL;

	if (init_sock(sp, host, port) < 0) 
		goto init_mgr_conn_error;
	return sp;

init_mgr_conn_error:
	free(sp);
	return NULL;
}

/* do_capfs_op(mgr, op, resp)
 *
 * Exported function for performing CAPFS operations.
 *
 * We leave the error in the downcall as is, and pass back any errors
 * returned from the underlying functions instead.  The upper layer can
 * do what they want with that error value.
 *
 * Returns 0 on success, -errno on failure.
 */
int do_capfs_op(struct capfs_upcall *op, struct capfs_downcall *resp)
{
	int error = 0, port, retry = 0;
	struct sockaddr *mgr = NULL;
	char host[CAPFSHOSTLEN];
	struct capfs_specific_options sp_options;

	sp_options.use_tcp    = op->options.tcp;
	sp_options.use_hcache = op->flags & CAPFS_MOUNT_HCACHE ? 1 : 0;
	sp_options.use_dcache = op->flags & CAPFS_MOUNT_DCACHE ? 1 : 0;
	/* beware that cons is not going to be valid, the first time around */
	sp_options.cons       = op->options.u.i_cons; 
	LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "use_tcp: %d, use_hcache: %d, use_dcache: %d, cons: %d\n", 
			sp_options.use_tcp, sp_options.use_hcache, sp_options.use_dcache, sp_options.cons);

	/*
	 * op->v1.fhname will not be set if op->type == HINT_OP and op->u.hint.hint == HINT_STATS 
	 * In other words if op is not a HINT_OP (OR) op is a HINT_OP but not a HINT_STATS ops
	 */
	if (op->type != HINT_OP ||  (op->u.hint.hint != HINT_STATS))
	{
		if (strlen(op->v1.fhname) == 0) return -EINVAL;

		port = name_to_port(op->v1.fhname);
		hostcpy(host, op->v1.fhname);
		if ((mgr = capfs_mgr_init(host, port)) == NULL) 
			return -ENXIO;
	}
do_capfs_op_retry:
	switch(op->type) {
	case SETMETA_OP:
		/* Force a group change if necessary */
		error = do_setmeta_op(&sp_options, mgr, op, resp, 1);
		break;
	case READ_OP:
	case WRITE_OP:
	{
		error = do_rw_op(&sp_options, mgr, op, resp);
		break;
	}
	case GETDENTS_OP:
		error = do_getdents_op(&sp_options, mgr, op, resp);
		break;
	case CREATE_OP:
	{
		error = do_create_op(&sp_options, mgr, op, resp);
		break;
	}
	case HINT_OP:
	{
		int (*plugin_close)(const char *) = NULL;
		struct plugin_info *pinfo = NULL;

		if (op->u.hint.hint != HINT_STATS)
		{
			pinfo = capfsd_match_policy_id(sp_options.cons);
			if (pinfo && (plugin_close = pinfo->policy_ops->close)) {
				plugin_close(op->v1.fhname);
			}
		}
		error = do_hint_op(&sp_options, mgr, op, resp);
		if (op->u.hint.hint != HINT_STATS)
		{
		}
		break;
	}
	case FSYNC_OP:
	{
		/* talk to the cas servers */
		int (*plugin_sync)(const char *);
		struct plugin_info *pinfo;

		pinfo = capfsd_match_policy_id(sp_options.cons);
		if (pinfo && (plugin_sync = pinfo->policy_ops->sync)) {
			plugin_sync(op->v1.fhname);
		}
		error = do_cas_fsync_op(&sp_options, mgr, op, resp);
		break;
	}
	case READLINK_OP:
		error = do_readlink_op(&sp_options, mgr, op, resp);
		break;
	default:
		error = do_generic_op(&sp_options, mgr, op, resp);
	}
	if (error == -ENFILE || error == -EMFILE) {
		/* need to close some files */
		PDEBUG(D_LLEV, "do_capfs_op: closing files\n");
		close_some_files();
	}

	if (error == -ENFILE ||
		 error == -EMFILE ||
		 error == -ECONNRESET ||
		 error == -ECONNREFUSED ||
		 error == -EPIPE)
	{
		/* retry the operation if we haven't tried too many times already. */
		if (retry++ < CAPFS_XFER_MAX_RETRY) {
			PERROR("do_capfs_op: retrying operation (type %d)\n",
					 op->type);
			sleep(CAPFS_XFER_RETRY_DELAY);
			goto do_capfs_op_retry;
		}
	}
	else if (error != 0) {
		PDEBUG(D_LLEV, "do_capfs_op: not retrying operation, return value %d\n",
				 error);
	}
	free(mgr);
	return error;
}

/* capfs_comm_idle()
 *
 * This incarnation of this function uses a standard "two strikes and
 * you're out" sort of deal.  First we look for any files with the ltime
 * set to 0.  These get removed.  Then we set all the remaining files
 * ltimes to 0.
 *
 * It might be best to add some sort of check so that we don't remove
 * files from the list "too often".  Perhaps this should be a parameter
 * to the function?
 *
 * We're going to ignore errors in here.
 */
void capfs_comm_idle(void)
{
	int port;
	struct sockaddr *mgr = NULL;
	struct pf *old;
	time_t time = 0;
	char host[CAPFSHOSTLEN];
	struct capfs_specific_options sp_options;

	memset(&sp_options, 0, sizeof(struct capfs_specific_options));
	/* only important field is use_tcp */
	sp_options.use_tcp = 0;

	/* search out and remove all the old (zero'd) entries */
	while ((old = (struct pf *) llist_search(file_list,
	(void *) &time, pf_ltime_cmp)) != NULL)
	{
		pf_rem(file_list, old->handle, old->name); /* takes out of list */

		/* close the file */
		port = name_to_port(old->name);
		hostcpy(host, old->name);
		if ((mgr = capfs_mgr_init(host, port)) == NULL) 
			return;
		/* talk to cas servers */
		cas_close_capfs_file(&sp_options, mgr, old);
		free(mgr);
		
		/* free the file structure */
		pf_free(old);
	}

	/* zero everyone else */
	llist_doall(file_list, pf_zerotime);
}

/* close_some_files()
 *
 * Returns -errno on error, number of files closed on success (can be 0).
 */
static int close_some_files(void)
{
	int i = 0, j, port;
	struct sockaddr *mgr = NULL;
	struct pf *old;
	time_t t = 0;
	char host[CAPFSHOSTLEN];
	struct capfs_specific_options sp_options;

	memset(&sp_options, 0, sizeof(struct capfs_specific_options));
	/* only important field is use_tcp */
	sp_options.use_tcp = 0;

	/* first we'll look for files that are already marked for removal */
	while ((old = (struct pf *) llist_search(file_list,
	(void *) &t, pf_ltime_cmp)) != NULL)
	{
		i++;

		pf_rem(file_list, old->handle, old->name); /* takes out of list */

		/* close the file */
		port = name_to_port(old->name);
		hostcpy(host, old->name);
		if ((mgr = capfs_mgr_init(host, port)) == NULL) 
			return -errno;
		/* talk to cas servers */
		cas_close_capfs_file(&sp_options, mgr, old);
		free(mgr);
		
		/* free the file structure */
		pf_free(old);
	}

	/* if we got anything, let's go ahead and return */
	if (i > 0) return i;

	/* next we'll look for files of decreasing age */
	for (j = 20; j > 0; j-= 5) {
		t = time(NULL) - j;
		while ((old = (struct pf *) llist_search(file_list,
		(void *) &t, pf_ltime_olderthan)) != NULL)
		{
			i++;

			pf_rem(file_list, old->handle, old->name); /* takes out of list */
			PDEBUG(D_FILE, "closing %s\n", old->name);

			/* close the file */
			port = name_to_port(old->name);
			hostcpy(host, old->name);
			if ((mgr = capfs_mgr_init(host, port)) == NULL) 
				return -1;
			/* talk to cas servers */
			cas_close_capfs_file(&sp_options, mgr, old);
			free(mgr);
			
			/* free the file structure */
			pf_free(old);
		}
		if (i > 0) return i;
	}

	/* finally we give up */
	return 0;
}

/* pf_zerotime()
 *
 * Sets the time value in a file structure (pf) to zero/null.
 *
 * This needs to be a function so we can pass it to llist_doall().
 */
static int pf_zerotime(void *pfp)
{
	((struct pf *) pfp)->ltime = 0;
	return 0;
}

static inline void init_capfs_options(struct capfs_options* opt, struct capfs_specific_options *sp_options)
{
	struct plugin_info *pinfo = NULL;
	void (*semantics)(int *, int *, int *) = NULL;
	opt->tcp = sp_options->use_tcp;
	opt->use_hcache = sp_options->use_hcache;
	/* consistency plugins specify whether or not hcache coherence protocols need to be observed */
	pinfo = capfsd_match_policy_id(sp_options->cons);
	if (pinfo && (semantics = pinfo->policy_ops->semantics)) 
	{
		semantics(&opt->force_commit, &opt->desire_hcache_coherence, &opt->delay_commit);
	}
	else /* by default we offer POSIX semantics */
	{
		opt->force_commit = 0;
		opt->desire_hcache_coherence = 1;
		opt->delay_commit = 0;
	}
	return;
}

/* cas_close_capfs_file()
 *
 * Handles sending a close request to a manager.  All local data
 * management must be handled at a higher level.
 */
static void cas_close_capfs_file(struct capfs_specific_options *sp_options,
		struct sockaddr *mgr, struct pf *pfp)
{
    mreq req;
    mack ack;
	 struct capfs_options opt;

	/* initialize request to manager */
    memset(&req, 0, sizeof(req));
    req.majik_nr = MGR_MAJIK_NR;
	 req.release_nr = CAPFS_RELEASE_NR;
    req.type = MGR_CLOSE;
    req.uid = 0;		/* root */
    req.gid = 0;		/* root */
    req.req.close.meta.fs_ino = pfp->fp->fd.meta.fs_ino;
    req.req.close.meta.u_stat.st_ino = pfp->fp->fd.meta.u_stat.st_ino;
    req.dsize = 0;
    /*
	  * send_mreq_saddr() handles receiving ack and checking error value,
     * but we really don't care so much in this case.  We're going to let
     * any errors just slide.
     */
	 init_capfs_options(&opt, sp_options);
	 send_mreq_saddr(&opt, mgr, &req, NULL, &ack, NULL);
	 /* 
	  * consistency semantic policy might want to 
	  * decide whether to clear the hcache or not on close...?
	  */
	 LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[close] calling clear_hashes on %s\n", pfp->name);
	 clear_hashes(pfp->name);
	 return;
}

/* capfs_comm_shutdown()
 *
 * Clean up after ourselves.
 */
void capfs_comm_shutdown(void)
{
	struct pf *head;

	/* remove all entries, handling each one individually */
	while ((head = pfl_head(file_list)) != NULL) {
		pf_rem(file_list, head->handle, head->name); /* takes out of list */
		pf_free(head); /* frees memory */
	}

	pfl_cleanup(file_list);
	file_list = NULL;
	return;
}

/* INTERNAL FUNCTIONS FOR CAPFS OPERATIONS */

static char *print_all_names(char *str, int length)
{
	static char print_name[256];
	int i;

	if (length > 256) {
		return "";
	}
	for (i = 0; i < length; i++) {
		print_name[i] = str[i] == '\0' ? ' ' : str[i];
	}
	return print_name;
}

/* do_generic_op(sp_options, mgr, op, resp)
 *
 * Handles the "easy" cases:
 *   GETMETA_OP, LOOKUP_OP, REMOVE_OP, RENAME_OP, MKDIR_OP, RMDIR_OP
 *   LINK_OP and SYMLINK_OP
 */
static int do_generic_op(struct capfs_specific_options *sp_options, 
		struct sockaddr *mgr, struct capfs_upcall *op, struct capfs_downcall *resp)
{
	int error = 0;
	mreq req;
	mack ack;
	char *fn = NULL;
	int to_free = 0;
	struct capfs_options opt;

	init_capfs_options(&opt, sp_options);
	init_mgr_req(&req, op);
	init_res(resp, op);

	switch(op->type) {
		case GETMETA_OP:
			req.type = MGR_LSTAT;
			fn = skip_to_filename(op->v1.fhname);
			req.dsize = strlen(fn); /* don't count terminator */
			break;
		case LOOKUP_OP:
			fn = skip_to_filename(op->u.lookup.name);
			req.dsize = strlen(fn); /* don't count terminator */
#ifdef HAVE_MGR_LOOKUP
			req.type = MGR_LOOKUP;
#else
			req.type = MGR_LSTAT;
#endif
			break;
		case REMOVE_OP:
			fn = skip_to_filename(op->v1.fhname);
			req.dsize = strlen(fn); /* don't count terminator */
			req.type = MGR_UNLINK;
			break;
		case RENAME_OP:
			req.type = MGR_RENAME;
			/* here we have to actually send BOTH names out to the manager.
			 * This is done with a single buffer using a null terminator
			 * between the two strings to indicate where one ends and the
			 * next begins.
			 */
			req.dsize = strlen(skip_to_filename(op->v1.fhname)) +
			strlen(skip_to_filename(op->u.rename.new_name)) + 2;
			if ((fn = (char *)calloc(1, req.dsize)) == NULL) return -ENOMEM;
			strcpy(fn, skip_to_filename(op->v1.fhname));
			strcpy(fn + strlen(skip_to_filename(op->v1.fhname)) + 1,
			skip_to_filename(op->u.rename.new_name));
			to_free = 1;
			break;
		case SYMLINK_OP:
		case LINK_OP:
 		{
			int soft = (op->type == SYMLINK_OP) ? 1: 0;
			req.type = MGR_LINK;
			req.req.link.soft = soft;
	 		/* here we have to actually send BOTH names out to the manager.
	  		 * This is done with a single buffer using a null terminator
	 		 * between the two strings to indicate where one ends and the
	  		 *	next begins. Just like a rename!
	  		 */
			if(soft == 1) {
				req.dsize = strlen(skip_to_filename(op->v1.fhname)) +
				strlen(op->u.symlink.target_name) + 2;
				if ((fn = (char *)calloc(1, req.dsize + 1)) == NULL) return -ENOMEM;
				strcpy(fn, skip_to_filename(op->v1.fhname));
				strcpy(fn + strlen(skip_to_filename(op->v1.fhname)) + 1,
				op->u.symlink.target_name);
				v2_meta_to_v1_fmeta(&op->u.symlink.meta, &req.req.link.meta);
			}
			else {
				req.dsize = strlen(skip_to_filename(op->v1.fhname)) +
				strlen(skip_to_filename(op->u.link.target_name)) + 2;
				if ((fn = (char *)calloc(1, req.dsize + 1)) == NULL) return -ENOMEM;
				strcpy(fn, skip_to_filename(op->v1.fhname));
				strcpy(fn + strlen(skip_to_filename(op->v1.fhname)) + 1,
				skip_to_filename(op->u.link.target_name));
				v2_meta_to_v1_fmeta(&op->u.link.meta, &req.req.link.meta);
			}
			to_free = 1;
			PDEBUG(D_LIB, "symlink - fn = %s, dsize = %Ld, target = %s, symlink = %s\n",
					print_all_names(fn, req.dsize), req.dsize, op->u.link.target_name, op->v1.fhname);
			break;
		}
		case MKDIR_OP:
			fn = skip_to_filename(op->u.mkdir.name);
			req.dsize = strlen(fn); /* don't count terminator */
			req.type = MGR_MKDIR;
			req.req.mkdir.mode = op->u.mkdir.meta.mode;
			break;
		case RMDIR_OP:
			fn = skip_to_filename(op->u.rmdir.name);
			req.dsize = strlen(fn); /* don't count terminator */
			req.type = MGR_RMDIR;
			break;
		case STATFS_OP:
			fn = skip_to_filename(op->v1.fhname);
			req.dsize = strlen(fn); /* don't count terminator */
			req.type = MGR_STATFS;
			break;
		default:
			error = -ENOSYS;
			goto do_generic_op_error;
	}
	/*
	 * If it is a special case mount operation, we need to also
	 * register our callback program number etc to
	 * the metadata server. Also do that only if we are operating
	 * as CAPFS.
	 */
	if (capfs_mode == 1) 
	{
		if (op->type == LOOKUP_OP &&
				op->u.lookup.register_cb == 1) 
		{
			struct plugin_info *pinfo;

			/* First check if we recognize the mount time consistency option */
			if ((pinfo = capfsd_match_policy_name(op->options.u.s_cons)) == NULL) {
				PERROR("capfsd: Could not find matching policy for %s\n", op->options.u.s_cons);
				error = -EINVAL;
				goto do_generic_op_error;
			}
			resp->u.lookup.cons = pinfo->policy_id;
			/* We will use tcp for callbacks */
			if (capfs_cbreg(&opt, mgr, CAPFS_CAPFSD, clientv1,
						IPPROTO_TCP) < 0) {
				error = -errno;
				goto do_generic_op_error;
			}
			check_for_registration = 0;
			/* Reset internal counters */
			n_retries = 0;
			sha1_time = 0;
			rpc_get_time = rpc_put_time = get_hashes_time = compute_hashes_time = 0;
			rpc_commit_time = 0;
			memset(server_get_time, 0, sizeof(int64_t) * CAPFS_STATS_MAX);
			memset(server_put_time, 0, sizeof(int64_t) * CAPFS_STATS_MAX);
		}
	}
	/* note: send_mreq_saddr() is a mgrcomm.c call.  It handles opening
	 * connections when necessary and so on.  All we need to do is make
	 * sure that the address we pass to it is ready to go, which is
	 * handled at a higher layer.
	 */
	if (send_mreq_saddr(&opt, mgr, &req, fn, &ack, NULL) < 0) {
		error = -errno;
		if(to_free == 1) {
			free(fn);
		}
		goto do_generic_op_error;
	}
	if (ack.status) {
		error = -ack.eno;
		if(to_free == 1) {
			free(fn);
		}
		goto do_generic_op_error;
	}

	switch(op->type) {
		case GETMETA_OP:
			v1_fmeta_to_v2_meta(&ack.ack.stat.meta, &(resp->u.getmeta.meta));
			v1_fmeta_to_v2_phys(&ack.ack.stat.meta, &(resp->u.getmeta.phys));
			break;
		case LOOKUP_OP:
#ifdef HAVE_MGR_LOOKUP
			v1_fmeta_to_v2_meta(&ack.ack.stat.meta, &(resp->u.lookup.meta));
			/* fast lookup returns everything but the size*/
			resp->u.lookup.meta.valid &= ~V_SIZE;
#else
			v1_fmeta_to_v2_meta(&ack.ack.stat.meta, &(resp->u.lookup.meta));
#endif
			break;
		case RENAME_OP:
			free(fn);
			break;
		case SYMLINK_OP:
		case LINK_OP:
			free(fn);
			break;
		case MKDIR_OP:
			/* call do_mkdir_post() to handle setting the owner of the dir */
			if ((error = do_mkdir_post(sp_options, mgr, op, resp)) < 0)
				goto do_generic_op_error;
			break;
		case REMOVE_OP:
			LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[remove] calling clear_hashes on %s\n", op->v1.fhname);
			clear_hashes(op->v1.fhname);
			/* Fall thru */
		case RMDIR_OP:
			/* all that is needed is the return value */
			break;
		case STATFS_OP:
			v1_ack_to_v2_statfs(&ack,&(resp->u.statfs.statfs));
			break;
		default:
			/* can't get here */
			error = -ENOSYS;
			goto do_generic_op_error;
	}
	resp->error = 0;
	return 0;

do_generic_op_error:
	resp->error = error;
	return error;
}

/* do_mkdir_post()
 *
 * Handles the ancillary operations necessary for correct mkdir
 * operation.  Currently this means that we call do_setmeta_op()
 * to set the owner and group appropriately.
 *
 * The mode is set already by the do_generic_op() call.
 */
static int do_mkdir_post(struct capfs_specific_options *sp_options, 
		struct sockaddr *mgr, struct capfs_upcall *op, struct capfs_downcall *resp)
{
	int error;
	struct capfs_upcall smup;
	struct capfs_downcall smdown;

	/* set up the upcall */
	memset(&smup, 0, sizeof(smup));
	smup.magic = op->magic;
	smup.seq = op->seq;
	smup.type = SETMETA_OP;
	smup.proc = op->proc;
	smup.u.setmeta.meta.handle = op->u.mkdir.meta.handle;
	smup.u.setmeta.meta.valid = V_UID | V_GID;
	smup.u.setmeta.meta.uid = op->u.mkdir.meta.uid;
	smup.u.setmeta.meta.gid = op->u.mkdir.meta.gid;
	strcpy(smup.v1.fhname, op->v1.fhname);
	
	/* We dont force a group change here */
	error = do_setmeta_op(sp_options, mgr, &smup, &smdown, 0);
	return error;
}

/* do_hint_op(sp_options, mgr, op, resp)
 *
 */
static int do_hint_op(struct capfs_specific_options *sp_options,
		struct sockaddr *mgr, struct capfs_upcall *op, struct capfs_downcall *resp)
{
	struct pf *pfp;

	init_res(resp, op);

	switch (op->u.hint.hint) 
	{
		case HINT_STATS:
		{
			/* We need to shove in the SHA-1 stats, nretries etc */
			resp->u.hint.stats.retries = n_retries;
			resp->u.hint.stats.sha1_time = sha1_time;
			resp->u.hint.stats.rpc_get = rpc_get_time;
			resp->u.hint.stats.rpc_put = rpc_put_time;
			resp->u.hint.stats.rpc_commit = rpc_commit_time;
			resp->u.hint.stats.rpc_gethashes = get_hashes_time;
			resp->u.hint.stats.rpc_compute = compute_hashes_time;
			memcpy(resp->u.hint.stats.server_get_time, server_get_time, sizeof(int64_t) * CAPFS_STATS_MAX);
			memcpy(resp->u.hint.stats.server_put_time, server_put_time, sizeof(int64_t) * CAPFS_STATS_MAX);
			n_retries = sha1_time = 0;
			rpc_get_time = rpc_put_time = rpc_commit_time = get_hashes_time = compute_hashes_time = 0;
			memset(server_get_time, 0, sizeof(int64_t) * CAPFS_STATS_MAX);
			memset(server_put_time, 0, sizeof(int64_t) * CAPFS_STATS_MAX);
			/* hcache stats */
			hashes_stats(&resp->u.hint.stats.hcache_hits, &resp->u.hint.stats.hcache_misses,
					&resp->u.hint.stats.hcache_fetches, &resp->u.hint.stats.hcache_invalidates,
					&resp->u.hint.stats.hcache_evicts);
			/* hcache callback stats */
			hcache_get_cb_stats(&resp->u.hint.stats.hcache_inv, &resp->u.hint.stats.hcache_inv_range,
					&resp->u.hint.stats.hcache_upd);
			break;
		}
		case HINT_CLOSE:
		{
			/* find the file in our list, close it, remove from list */
			pfp = pf_search(file_list, op->u.hint.handle, op->v1.fhname);
			if (pfp == NULL) return 0;
			pf_rem(file_list, pfp->handle, pfp->name);
			/* call the cas servers alone */
			cas_close_capfs_file(sp_options, mgr, pfp);
			pf_free(pfp);
			/* Purge the hcache of any hashes that may belong to this file */
			LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[hint_close] calling clear_hashes on %s\n", op->v1.fhname);
			clear_hashes(op->v1.fhname);
			break;
		}
		case HINT_OPEN:
			/* don't do anything; we'll open the file when I/O is started */
		default:
		break;
	}
	return 0;
}

static int do_cas_fsync_op(struct capfs_specific_options *sp_options,
		struct sockaddr *mgr, struct capfs_upcall *op, struct capfs_downcall *resp)
{
	/*
	 * The CAPFS data servers do not yet
	 * export any primitives for fsync/fdatasync
	 */
	return 0;
}

/* do_create_op(sp_options, mgr, op, resp)
 */
static int do_create_op(struct capfs_specific_options *sp_options,
		struct sockaddr *mgr, struct capfs_upcall *op, struct capfs_downcall *resp)
{
	int error = 0;
	mreq req;

	init_mgr_req(&req, op);
	init_res(resp, op);

	error = create_capfs_file(sp_options, mgr, op->u.create.name,
					&(op->u.create.meta), &(op->u.create.phys));
	resp->error = error;
	return error;
}

/* do_setmeta_op(sp_options, mgr, op, resp, force_grp_change)
 *
 * Notes:
 * This is more complicated than it should be.  The problem here is that
 * we don't have a "setmeta" operation in CAPFS v1.  We instead have
 * chown, chmod, and utime.
 *
 * First we perform a GETMETA request to obtain the current metadata and
 * also to grab the capfs_phys which we must return (and the v1 calls
 * don't return this for ANY of the calls we would perform in here).
 * This has the added bonus of giving us the opportunity to avoid calls
 * which wouldn't result in any modification to the metadata (ie. we can
 * compare the existing values to the new ones).
 *
 * If we do a MGR_CHOWN request, then force_grp_change dictates whether
 * or not the group id is going to be obeyed or not 
 */
static int do_setmeta_op(struct capfs_specific_options *sp_options, 
		struct sockaddr *mgr, struct capfs_upcall *op, struct capfs_downcall *resp, int force_grp_change)
{
	int error = 0;
	char *fn;
	mreq req;
	mack ack;
	int owner_or_mode_updated = 0;
	struct capfs_options opt;

	struct capfs_upcall gmup;
	struct capfs_downcall gmres;

	init_capfs_options(&opt, sp_options);
	/* perform a GETMETA first */
	memset(&gmup, 0, sizeof(gmup));
	gmup.magic = op->magic;
	gmup.seq = op->seq;
	gmup.type = GETMETA_OP;
	gmup.proc = op->proc;
	gmup.u.getmeta.handle = op->u.setmeta.meta.handle;
	strcpy(gmup.v1.fhname, op->v1.fhname);

	error = do_generic_op(sp_options, mgr, &gmup, &gmres);
	if (error < 0) {
		goto do_setmeta_op_error;
	}

	/* we initialize the result and copy in the final values. */
	init_res(resp, op);
	resp->u.setmeta.meta = gmres.u.getmeta.meta;
	resp->u.setmeta.phys = gmres.u.getmeta.phys;

	fn = skip_to_filename(op->v1.fhname);

	if (op->u.setmeta.meta.valid & V_MODE
			&& op->u.setmeta.meta.mode != gmres.u.getmeta.meta.mode)
	{
		owner_or_mode_updated = 1;
		init_mgr_req(&req, op);
		req.uid = op->u.setmeta.caller_uid;
		req.gid = op->u.setmeta.caller_gid;
		req.type = MGR_CHMOD;
		req.dsize = strlen(fn); /* don't count terminator */
		req.req.chmod.mode = op->u.setmeta.meta.mode;

		if (send_mreq_saddr(&opt, mgr, &req, fn, &ack, NULL) < 0) {
			error = -errno;
			goto do_setmeta_op_error;
		}
		if (ack.status) {
			error = -ack.eno;
			goto do_setmeta_op_error;
		}

		/* save new value in the response */
		resp->u.setmeta.meta.mode = op->u.setmeta.meta.mode;
	}

	/* this is a little convoluted, but it keeps us from doing separate
	 * requests for owner and group changes.
	 */
	if (op->u.setmeta.meta.valid & (V_UID | V_GID)) {
		int changing_something = 0;
		owner_or_mode_updated = 1;

		init_mgr_req(&req, op);
		req.uid = op->u.setmeta.caller_uid;
		req.gid = op->u.setmeta.caller_gid;
		req.type = MGR_CHOWN;
		req.dsize = strlen(fn);
		req.req.chown.force_group_change = force_grp_change;
		if (op->u.setmeta.meta.valid & V_UID 
				&& op->u.setmeta.meta.uid != gmres.u.getmeta.meta.uid) {
			changing_something = 1;
			req.req.chown.owner = op->u.setmeta.meta.uid;
			resp->u.getmeta.meta.uid = op->u.setmeta.meta.uid;
		}
		else
			req.req.chown.owner = -1;

		if (op->u.setmeta.meta.valid & V_GID
				&& op->u.setmeta.meta.gid != gmres.u.getmeta.meta.gid) {
			changing_something = 1;
			req.req.chown.group = op->u.setmeta.meta.gid;
			resp->u.getmeta.meta.gid = op->u.setmeta.meta.gid;
		}
		else
			req.req.chown.group = -1;

		if (changing_something) {
			if (send_mreq_saddr(&opt, mgr, &req, fn, &ack, NULL) < 0) {
				error = -errno;
				goto do_setmeta_op_error;
			}
			if (ack.status) {
				error = -ack.eno;
				goto do_setmeta_op_error;
			}
		}

		/* save new values in the response */
		if (op->u.setmeta.meta.valid & V_UID)
			resp->u.setmeta.meta.uid = op->u.setmeta.meta.uid;
		if (op->u.setmeta.meta.valid & V_GID)
			resp->u.setmeta.meta.gid = op->u.setmeta.meta.gid;
	}

	/* this is for access and modification time */
	if (op->u.setmeta.meta.valid & V_TIMES) {
		init_mgr_req(&req, op);
		/* if the permissions were changed successfully, then update the
		 * time as root in case the permissions now prevent us from doing
		 * it as a normal user
		 */
		if(owner_or_mode_updated)
		{
			req.uid = 0;
			req.gid = 0;
		}
		else
		{
			req.uid = op->u.setmeta.caller_uid;
			req.gid = op->u.setmeta.caller_gid;
		}
		req.type = MGR_UTIME;
		req.dsize = strlen(fn);
		req.req.utime.actime = op->u.setmeta.meta.atime;
		req.req.utime.modtime = op->u.setmeta.meta.mtime;

		if (send_mreq_saddr(&opt, mgr, &req, fn, &ack, NULL) < 0) {
			error = -errno;
			goto do_setmeta_op_error;
		}
		if (ack.status) {
			error = -ack.eno;
			goto do_setmeta_op_error;
		}

		resp->u.setmeta.meta.atime = op->u.setmeta.meta.atime;
		resp->u.setmeta.meta.mtime = op->u.setmeta.meta.mtime;
	}

#ifdef HAVE_MGR_CTIME
	/* see if we need to modify the ctime */
	if (op->u.setmeta.meta.valid & V_CTIME) {
		init_mgr_req(&req, op);
		/* if the permissions were changed successfully, then update the
		 * time as root in case the permissions now prevent us from doing
		 * it as a normal user
		 */
		if(owner_or_mode_updated)
		{
			req.uid = 0;
			req.gid = 0;
		}
		else
		{
			req.uid = op->u.setmeta.caller_uid;
			req.gid = op->u.setmeta.caller_gid;
		}
		req.type = MGR_CTIME;
		req.dsize = strlen(fn);
		req.req.ctime.createtime = op->u.setmeta.meta.ctime;

		if (send_mreq_saddr(&opt, mgr, &req, fn, &ack, NULL) < 0) {
			error = -errno;
			goto do_setmeta_op_error;
		}
		if (ack.status) {
			error = -ack.eno;
			goto do_setmeta_op_error;
		}

		resp->u.setmeta.meta.ctime = op->u.setmeta.meta.ctime;
	}
#endif /* HAVE_MGR_CTIME */

	if (op->u.setmeta.meta.valid & V_SIZE
	&& op->u.setmeta.meta.size != gmres.u.getmeta.meta.size)
	{
		/* Hmm...is this a truncate? */
		init_mgr_req(&req, op);
		/* if the permissions were changed successfully, then truncate
		 * as root in case the permissions now prevent us from doing
		 * it as a normal user
		 */
		if(owner_or_mode_updated)
		{
			req.uid = 0;
			req.gid = 0;
		}
		else
		{
			req.uid = op->u.setmeta.caller_uid;
			req.gid = op->u.setmeta.caller_gid;
		}

		req.type = MGR_TRUNCATE;
		req.dsize = strlen(fn);
		req.req.truncate.length = op->u.setmeta.meta.size;

		if (send_mreq_saddr(&opt, mgr, &req, fn, &ack, NULL) < 0) {
			error = -errno;
			goto do_setmeta_op_error;
		}
		if (ack.status) {
			error = -ack.eno;
			goto do_setmeta_op_error;
		}

		resp->u.setmeta.meta.size = op->u.setmeta.meta.size;
	}

	if (gmres.u.getmeta.phys.blksize > 0) {
		resp->u.setmeta.meta.blksize = gmres.u.getmeta.phys.blksize;
	}
	else {
		resp->u.setmeta.meta.blksize = 512;
		resp->u.setmeta.phys.blksize = 512;
	}
	resp->u.setmeta.meta.blocks =
		(resp->u.setmeta.meta.size / resp->u.setmeta.meta.blksize) +
		((resp->u.setmeta.meta.size % resp->u.setmeta.meta.blksize) ? 1 : 0);
	resp->u.setmeta.meta.valid =
		V_MODE|V_UID|V_GID|V_SIZE|V_TIMES|V_BLKSIZE|V_BLOCKS;
	resp->error = 0;
	return 0;

do_setmeta_op_error:
	resp->error = error;
	return error;
}

/* do_getdents_op(sp_options, mgr, op, resp)
 *
 * Send a CAPFS getdents message, return the results in a buffer defined
 * by the caller.
 *
 * NOTES:
 * CAPFS getdents operations now include a count instead of a size,
 * indicating the number of records instead of the size of the region to
 * receive into.  Since our new dirent records are of a fixed size, this
 * will be fine in that we know how big a buffer we will need.
 *
 * However, we kinda have to guess what size buffer we will need to
 * receive the information from the manager; the v1 implmentation of
 * getdents() is rather primitive.
 *
 * This call relies on the op->xfer.ptr and op->xfer.size values being
 * set to describe a buffer region in which to put the resulting data.
 * It does not check the validity of the op->xfer.ptr value.
 *
 * Returns 0 on success, -errno on failure.  resp->xfer.ptr and
 * resp->xfer.size are set to describe the resulting buffer on success.
 */
static int do_getdents_op(struct capfs_specific_options *sp_options,
		struct sockaddr *mgr, struct capfs_upcall *op, struct capfs_downcall *resp)
{
	int error = 0;
	capfs_size_t res_dsize;
	mreq req;
	mack ack;
	char *fn;
	struct ackdata_c ackdata;
	struct capfs_options opt;

	init_capfs_options(&opt, sp_options);
	res_dsize = op->u.getdents.count * sizeof(struct capfs_dirent);
	fn = skip_to_filename(op->v1.fhname);

	/* ensure data buffer as big as what they asked for */
	if (op->xfer.size < res_dsize) {
		error = -EINVAL;
		goto out;
	}

	init_mgr_req(&req, op);

	req.type = MGR_GETDENTS;
	req.dsize = strlen(fn); /* don't count terminator */
	req.req.getdents.offset = op->u.getdents.off;
	req.req.getdents.length = res_dsize;

	init_res(resp, op);
	resp->xfer.orig_size = op->xfer.size;
	resp->xfer.from_kmem = op->xfer.to_kmem;
	resp->xfer.to_free_ptr = op->xfer.to_free_ptr;

	ackdata.type = req.type;
	ackdata.u.getdents.nentries = op->u.getdents.count;
	ackdata.u.getdents.pdir = op->xfer.ptr;

	if (send_mreq_saddr(&opt, mgr, &req, fn, &ack, &ackdata) < 0) {
		error = -errno;
		goto out;
	}
	if (ack.status) {
		error = -ack.eno;
		goto out;
	}
	if (ack.dsize == 0) /* EOF */ {
		resp->u.getdents.eof = 1;
	}
	else if (ack.dsize > res_dsize) {
		error = -EINVAL;
		goto out;
	} else /* copy the trailing dirent data */ {

		/* transfer-specific elements */
		resp->u.getdents.off = ack.ack.getdents.offset;
		resp->u.getdents.count = ack.dsize / sizeof(struct capfs_dirent);

		resp->xfer.ptr = op->xfer.ptr;
		resp->xfer.size = ack.dsize;
	}

out:
	resp->error = error;
	return error;
}

/*
 * do_readlink_op(sp_options, mgr, op, resp)
 *
 * We send the name of the link to the manager, that
 * attempts to then follow the link and returns the 
 * location to which it points to.
 * Once we get that do we need to piece the name back to 
 * however the client understands it??
 */
static int do_readlink_op(struct capfs_specific_options *sp_options,
		struct sockaddr *mgr, struct capfs_upcall *op, struct capfs_downcall *resp)
{
    int error = 0;
    capfs_size_t res_dsize;
    mreq req;
    mack ack;
    char *fn;
	 struct ackdata_c ackdata;
	 struct capfs_options opt;


	 init_capfs_options(&opt, sp_options);
    res_dsize = op->xfer.size;
    fn = skip_to_filename(op->v1.fhname);

	 PDEBUG(D_LIB, "trying to readlink %s\n", fn);

    init_mgr_req(&req, op);
    req.type = MGR_READLINK;
    req.dsize = strlen(fn);	/* don't count terminator */
    init_res(resp, op);
	 resp->xfer.from_kmem = op->xfer.to_kmem;
	 resp->xfer.orig_size = op->xfer.size;
	 ackdata.type = MGR_READLINK;

	 ackdata.u.readlink.link_len = res_dsize;
	 ackdata.u.readlink.link_name = op->xfer.ptr;
    if (send_mreq_saddr(&opt, mgr, &req, fn, &ack, &ackdata) < 0) {
		 	error = -errno;
			goto out;
    }
	 if (ack.status) {
		 	error = -ack.eno;
			goto out;
	 }
    if (ack.dsize <= 0 || ack.dsize > res_dsize) {
			error = -EINVAL;
			goto out;
    } 
	 else {			
			/* NUL terminate it */
			*((char *)op->xfer.ptr+ack.dsize) = '\0';
			PDEBUG(D_LIB, "readlink got back %s %ld\n", (char *)op->xfer.ptr, (long)ack.dsize);
			/* transfer-specific elements */
			resp->xfer.ptr = op->xfer.ptr;
			resp->xfer.size = ack.dsize;
    }
out:
    resp->error = error;
    return error;
}

struct op_info {
	int  type;
	fdesc *fp;
	char  *fhname;
	void  *user_ptr;
	capfs_off_t user_offset;
	capfs_size_t user_size;
	struct capfs_specific_options *sp_options;
	struct capfs_upcall* op;

	int64_t aligned_size;
	void  *aligned_buffer;
	/* nhashes is what is actually present */
	int64_t  nhashes;
	int64_t  begin_chunk;
	int64_t  end_chunk;
	/* nchunks is end_chunk - begin_chunk + 1 */
	int64_t  nchunks;
	unsigned char  *phashes;
	unsigned char  *pnewhashes;
	capfs_size_t file_size;
};

static int lookup_file_size(struct op_info *info, capfs_size_t *size)
{	
	mreq req;
	mack ack;
	struct sockaddr *saddr;
	char host[256];
	int port;
	struct capfs_options opt;

	init_capfs_options(&opt, info->sp_options);
	init_mgr_req(&req, info->op);
	req.type = MGR_FSTAT;
	req.dsize = 0;
	req.req.fstat.meta = info->fp->fd.meta;
	port = name_to_port(info->op->v1.fhname);
	hostcpy(host, info->op->v1.fhname);
	if ((saddr = capfs_mgr_init(host, port)) == NULL)
		return -ENXIO;

	if (send_mreq_saddr(&opt, saddr, &req, NULL, &ack, NULL) < 0
			|| ack.status) {
		free(saddr);
		return -1;
	}
	free(saddr);
	*size = ack.ack.fstat.meta.u_stat.st_size;
	LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "Looked up file size of %s as %Ld\n",
			info->op->v1.fhname, *size);
	return 0;
}

static void info_dtor(struct op_info *info)
{
	if (info->aligned_buffer) free(info->aligned_buffer);
	if (info->phashes) free(info->phashes);
	if (info->pnewhashes) free(info->pnewhashes);
	/* FREE any other stuff here if need be */
	return;
}

static int64_t time_diff(struct timeval *end, struct timeval *begin)
{
	int64_t difference = 0;
	if (end->tv_usec < begin->tv_usec) {
		end->tv_usec += 1000000;
		end->tv_sec--;
	}
	end->tv_sec -= begin->tv_sec;
	end->tv_usec -= begin->tv_usec;
	difference = (end->tv_sec * 1000000) + end->tv_usec;
	return difference;
}

/*
 * Try to fetch the hashes for this operation (either from the hcache
 * or by sending an RPC to the hash server.
 */
static int do_get_hashes(struct op_info *info)
{
	fmeta meta;
	int64_t req_nchunks = 0;
	struct timeval begin, end;

	gettimeofday(&begin, NULL);
	info->begin_chunk = info->user_offset / CAPFS_CHUNK_SIZE;
	info->end_chunk   = (info->user_offset + info->user_size - 1) / CAPFS_CHUNK_SIZE;
	req_nchunks = info->nchunks = (info->end_chunk - info->begin_chunk + 1);
	/* Allocate only if need be */
	if (info->phashes == NULL) {
		/*
		 * We will allocate a fairly large chunk of memory to retrieve a whole
		 * bunch of hashes instead of retrieving only as many hashes
		 * as requested.. Sort of like prefetching of the hashes...
		 */
		info->phashes 		= (unsigned char *) calloc(CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHES);
		if (info->phashes == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "Could not allocate memory\n");
			return -ENOMEM;
		}
	}
	/*
	 * I really think, this whole sequence can be optimized a lot, but I can't think
	 * of anything at the moment. We dont necessarily have to query the file size,
	 * if we knew that the maximum number of hashes is greater than the number of 
	 * chunks spanned by this read/write
	 */
	/* lookup the file size if hcache is enabled */
	if (info->sp_options->use_hcache == 1) 
	{
		int64_t max_chunks;
		if (lookup_file_size(info, &info->file_size) < 0) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "Could not lookup file size: %d\n", -errno);
			return -errno;
		}
		max_chunks = (info->file_size + CAPFS_CHUNK_SIZE - 1) / CAPFS_CHUNK_SIZE;
		/*
		 * Another hcache optimization is possible.
		 * Instead of requesting only as many hashes as the user wants,
		 * we might as well fetch a big chunk of hashes at this point of time
		 * if (max_chunk - info->begin_chunk > info->nchunks)
		 * then fetch as many chunks as MIN(max_chunk - info->begin_chunk, CAPFS_MAXHASHES)
		 * to put into the hcache.
		 * Originally, we used to do
		 * req_nchunks = MIN(info->nchunks, max_chunks - info->begin_chunk);
		 */
		if (max_chunks - info->begin_chunk > 0 && max_chunks - info->begin_chunk > info->nchunks)
		{
			req_nchunks = MIN(max_chunks - info->begin_chunk, CAPFS_MAXHASHES);
		}
		else
		{
			req_nchunks = MIN(info->nchunks, max_chunks - info->begin_chunk);
		}
		if (req_nchunks < 0) req_nchunks = 0;
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[get_hashes] called on %s from %Ld for MIN(%Ld, %Ld) = %Ld\n", 
				info->fhname, info->begin_chunk, info->nchunks, max_chunks - info->begin_chunk, req_nchunks);
	}
	else {
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[get_hashes] called on %s from %Ld for %Ld hashes\n",
				info->fhname, info->begin_chunk, req_nchunks);
	}
	/* Now issue a fetch of the hashes if necessary */
	if (req_nchunks > 0) 
	{
		int64_t nhashes;
		/*
		 * We have two possibilities
		 * a) req_nchunks > info->nchunks means that we are doing pre-fetching
		 * b) req_nchunks < info->nchunks means that there were fewer hashes than what was 
		 * requested for the file.
		 */
		nhashes = get_hashes(info->sp_options->use_hcache, info->fhname, 
					info->begin_chunk, req_nchunks, info->nchunks, info->phashes, &meta);
		/* 
		 * Even though we received more, it does not make sense to pre-read/pre-fetch
		 * data from the file unless we have the dcache in place as well...
		 * So we need to set info->nhashes to how many ever hashes were requested
		 * in case it was larger.
		 */
		if (nhashes < 0)
		{
			info->nhashes = nhashes;
		}
		else 
		{
			if (nhashes != req_nchunks) {
				LOG(stderr, INFO_MSG, SUBSYS_CLIENT, "Requested %Ld hashes, but obtained %Ld hashes?\n", req_nchunks, nhashes);
			}
			if (nhashes > info->nchunks)
			{
				info->nhashes = info->nchunks;
			}
			else
			{
				info->nhashes = nhashes;
			}
			LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "info->nhashes = %Ld\n", info->nhashes);
		}
	}
	else 
	{
		info->nhashes = 0;
	}
	if (info->nhashes < 0) 
	{
		LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "Could not obtain hashes for file: %Ld\n", info->nhashes);
		return -errno;
	}
	/* if we did have a few hashes!, we also allocate an aligned buffer for xfer */
	else if (info->nhashes > 0) {
		/* Do this only for reads. We do the allocation for writes in do_compute_hashes() */
		if (info->type == IOD_RW_READ) {
			info->aligned_buffer = (void *) calloc(CAPFS_CHUNK_SIZE, info->nhashes);
			if (info->aligned_buffer == NULL) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "Could not allocate memory\n");
				return -ENOMEM;
			}
			info->aligned_size = info->nhashes * CAPFS_CHUNK_SIZE;
			LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[READ] info->aligned_buffer = %p of aligned_size = %Ld\n",
					info->aligned_buffer, info->aligned_size);
		}
	}
	/* if we did not use hcache, then use the returned meta-data value to fill in the file size */
	if (info->sp_options->use_hcache == 0) 
	{
		info->file_size = meta.u_stat.st_size;
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "get_hashes yielded %Ld hashes with file size %Ld\n",
			info->nhashes, info->file_size);
#ifdef VERBOSE_DEBUG
	{
		int i;
		for (i = 0; i < info->nhashes; i++) {
			char str[256];
			hash2str(info->phashes + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH, str);
			LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "%d: %s\n", i, str);
		}
	}
#endif
	gettimeofday(&end, NULL);
	get_hashes_time += time_diff(&end, &begin);
	return 0;
}

/* 
 * Given a chunk, and a file descriptor information, map
 * the chunk back to an iod server number
 * Also return the value of the global_iod_number
 * for the stats updates.
 */
static int map_chunk(int64_t chunk, fdesc_p fp, struct iod_map *my_map)
{
	struct capfs_filestat *pfstat = &fp->fd.meta.p_stat;
	int64_t off = 0;

	if (pfstat->ssize <= 0 || my_map == NULL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "Invalid stripe size (%d) < 0\n", pfstat->ssize);
		return -1;
	}
	if (CAPFS_CHUNK_SIZE > pfstat->ssize) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "Invalid value of chunk size. Cannot be (%d) > stripe size (%d)\n",
				CAPFS_CHUNK_SIZE, pfstat->ssize);
		return -1;
	}
	if (pfstat->ssize % CAPFS_CHUNK_SIZE != 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "Stripe size (%d) needs to be a multiple of chunk size (%d)\n",
				pfstat->ssize, CAPFS_CHUNK_SIZE);
		return -1;
	}
	if (pfstat->pcount <= 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "Invalid value of pcount(%d) < 0\n", pfstat->pcount);
		return -1;
	}
	/* can be -ve */
	if (pfstat->base < 0) {
		pfstat->base = 0;
	}
	off = chunk * CAPFS_CHUNK_SIZE;
	if (pfstat->pcount == 1) {
		my_map->normalized_iod = (pfstat->base % pfstat->pcount);
		my_map->global_iod = pfstat->base;
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "Normalized iod %d, Global iod %d\n", my_map->normalized_iod, my_map->global_iod);
		return (pfstat->base % pfstat->pcount);
	}
	my_map->normalized_iod = (pfstat->base + (off / pfstat->ssize)) % pfstat->pcount;
	my_map->global_iod = (pfstat->base + (off / pfstat->ssize));
	/* LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "Normalized iod %d, Global iod %d\n", my_map->normalized_iod, my_map->global_iod); */
	return (pfstat->base + (off / pfstat->ssize)) % pfstat->pcount;
}

/*
 * If need be fetch atmost 2 corner blocks into the appropriate
 * locations in "overall" and then return.
 * Note that fetch can be avoided completely, if the write begins
 * at a location that is less than the total number of hashes
 * known for this file.
 */
static int fetch_corner_chunks(struct op_info *info, char *overall)
{
	int j, nissues = 0, niods, ret;
	struct iod_map map[2];
	long issue_read[2] = {0, 0};
	unsigned char *corner_hashes[2] = {NULL, NULL}, *phash = NULL;
	struct cas_iod_worker_data *cas = NULL;
	struct dataArray jobs[2];
	int part1 = 0, part2 = 0;

	/* There is still a possibility of not having to fetch anything */
	if (info->nhashes == 0) {
		return 0;
	}
	part1 = (info->user_offset % CAPFS_CHUNK_SIZE);
	part2 = ((info->user_offset + info->user_size) % CAPFS_CHUNK_SIZE);
	memset(jobs, 0, 2 * sizeof(struct dataArray));

	LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[fetch corner] begin_chunk = %Ld, end_chunk = %Ld,"
			" nhashes = %Ld, part1 = %d, part2 = %d\n",
			info->begin_chunk, info->end_chunk, info->nhashes, part1, part2);
	/* We definitely need to fetch the beginning chunk */
	if (part1 != 0) {
		issue_read[nissues] = info->begin_chunk;
		corner_hashes[nissues++] = info->phashes;
	}
	/* if the write ends at a location that is also less than number of hashes */
	if (part2 != 0 && (info->end_chunk - info->begin_chunk + 1) == info->nhashes && (info->end_chunk != info->begin_chunk)) {
		/* We need to fetch the end chunk as well */
		issue_read[nissues] = info->end_chunk;
		corner_hashes[nissues++] = info->phashes + 
			(info->end_chunk - info->begin_chunk) * CAPFS_MAXHASHLENGTH;
	}
	/* Still a possibility of not having to fetch anything? */
	if (nissues == 0)
	{
		LOG(stderr, WARNING_MSG, SUBSYS_CLIENT, "[fetch_corner] No need to issue anything?\n");
		return 0;
	}
	phash = (unsigned char *) calloc(CAPFS_MAXHASHLENGTH * nissues, sizeof(unsigned char));
	if (phash == NULL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "could not allocate memory\n");
		return -ENOMEM;
	}
	/* We need to issue reads here for atmost 2 of the chunks here */
	for (j = 0; j < nissues; j++) 
	{
		memcpy(phash + j * CAPFS_MAXHASHLENGTH, corner_hashes[j], CAPFS_MAXHASHLENGTH);
		map_chunk(issue_read[j], info->fp, &map[j]);
		jobs[j].start = overall +
			(issue_read[j] - info->begin_chunk) * CAPFS_CHUNK_SIZE;
		jobs[j].byteCount = CAPFS_CHUNK_SIZE;
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "Fetch corner chunks hashes for %d\n", nissues);
#ifdef DEBUG
	for (j = 0; j < nissues; j++) {
		char str[256];
		hash2str(phash + j * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH, str);
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "%d: %s\n", j, str);
	}
#endif
	/* build a job for the cas servers */
	cas = convert_to_jobs(jobs, nissues, map, info->fp, phash, &niods);
	if (cas == NULL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "could not allocate memory\n");
		free(phash);
		return -ENOMEM;
	}
	/* feed it to the cas engine */
	clnt_get(info->sp_options->use_tcp, cas, niods);
	for (j = 0; j < niods; j++) {
		ret = *(cas[j].returnValue);
		/* It is possible that we get ENOENT errors
		 * or ENOSUCH file sort of errors.
		 * because of the way we are handling lseek() and
		 * truncate(). Need to just let those errors through
		 */
		if (ret < 0 && ret != -ENOENT) {
			free(phash);
			freeJobs(cas, niods);
			return ret;
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "Fetch corner chunk from iod %d returned %d\n", j, ret);
	}
	free(phash);
	freeJobs(cas, niods);
	return 0;
}

/*
 * Try to compute the hashes for this block write operation.
 * Note that this may require us to reissue a read of the 2 corner
 * chunks. (begin, end). That will be the worst case scenario.
 * However, if people want performance out of this file system,
 * maybe people should use a block size for each write equal
 * to our chunk_size.
 */
static int do_compute_hashes(struct op_info *info)
{
	int64_t i;
	int     nissues = 0, err = 0, part1 = 0, part2 = 0;
	struct timeval begin, end, start, finish;

	gettimeofday(&start, NULL);
	info->begin_chunk = info->user_offset / CAPFS_CHUNK_SIZE;
	info->end_chunk 	= (info->user_offset + info->user_size - 1) / CAPFS_CHUNK_SIZE;
	info->nchunks 		= (info->end_chunk - info->begin_chunk + 1);

	/* Allocate memory if need be */
	if (info->pnewhashes == NULL) {
		info->pnewhashes  = (unsigned char *) calloc(CAPFS_MAXHASHLENGTH, info->nchunks);
		if (info->pnewhashes == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "could not allocate memory\n");
			return -ENOMEM;
		}
	}
	if ((part1 = (info->user_offset % CAPFS_CHUNK_SIZE)) != 0) {
		nissues++;
	}
	if ((part2 = ((info->user_offset + info->user_size) % CAPFS_CHUNK_SIZE)) != 0) {
		if (nissues == 0 || info->begin_chunk != info->end_chunk) {
			nissues++;
		}
	}
	/* let us be optimistic here. totally aligned writes! */
	if (nissues == 0) {
		gettimeofday(&begin, NULL);
		for (i = 0; i < info->nchunks; i++) {
			size_t len;
			unsigned char *ptr = NULL;
	
			ptr = info->pnewhashes + i * CAPFS_MAXHASHLENGTH;
			if ((err = sha1(info->user_ptr + i * CAPFS_CHUNK_SIZE, 
							CAPFS_CHUNK_SIZE, &ptr, &len)) < 0) {
				goto cleanup;
			}
		}
		gettimeofday(&end, NULL);
		sha1_time += time_diff(&end, &begin);
		gettimeofday(&finish, NULL);
		compute_hashes_time += time_diff(&finish, &start);
		return 0;
	}
	else {
		char *overall = NULL;
		int64_t total_length = info->file_size;
		int64_t size_thus_far = 0;

		overall = (char *) calloc(info->nchunks, CAPFS_CHUNK_SIZE);
		if (overall == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "could not allocate memory\n");
			return -ENOMEM;
		}
		if ((err = fetch_corner_chunks(info, overall)) < 0) {
			free(overall);
			return err;
		}
		err = 0;
		if (total_length < (info->user_offset + info->user_size)) {
			total_length = (info->user_offset + info->user_size);
		}
		/* copy the new data to be written */
		memcpy(overall + part1, info->user_ptr, info->user_size);
		size_thus_far = (info->begin_chunk * CAPFS_CHUNK_SIZE);
		gettimeofday(&begin, NULL);
		for (i = 0; i < info->nchunks; i++) {
			size_t len;
			unsigned char *ptr;

			ptr = info->pnewhashes + i * CAPFS_MAXHASHLENGTH;
			if (size_thus_far + CAPFS_CHUNK_SIZE < total_length) {
				if ((err = sha1(overall + i * CAPFS_CHUNK_SIZE, 
								CAPFS_CHUNK_SIZE, &ptr, &len)) < 0) {
					break;
				}
			}
			else {
				assert(total_length - size_thus_far > 0);
				if ((err = sha1(overall + i * CAPFS_CHUNK_SIZE,
								(total_length - size_thus_far), &ptr, &len)) < 0) {
					break;
				}
			}
			size_thus_far += CAPFS_CHUNK_SIZE;
		}
		if (i != info->nchunks) {
			free(overall);
			goto cleanup;
		}
		/* set overall to be the aligned buffer using which I/O is going to be done */
		info->aligned_buffer = overall;
		info->aligned_size   = info->nchunks * CAPFS_CHUNK_SIZE;
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[WRITE] info->aligned_buffer = %p of aligned_size = %Ld\n",
				info->aligned_buffer, info->aligned_size);
		gettimeofday(&end, NULL);
		sha1_time += time_diff(&end, &begin);
		gettimeofday(&finish, NULL);
		compute_hashes_time += time_diff(&finish, &start);
		return 0;
	}
cleanup:
	return -errno;
}

/* new content-addressable data servers */
static int do_cas_data_staging(struct op_info *info)
{
	struct cas_iod_worker_data *cas = NULL;
	struct iod_map *map = NULL;
	int niods = 0, ret = 0, j, part1 = 0;
	struct dataArray *jobs = NULL;
	char *ptr = NULL;
	/* 
	 * If it is a READ operation, then we use <info->nhashes,info->phashes>
	 * to retrieve data from the CAS servers. The data should
	 * be staged to info->aligned_buffer if that  is not NULL,
	 * else it can be staged directly to info->user_ptr.
	 */
	if (info->type == IOD_RW_READ) 
	{
		struct timeval begin, end;

		gettimeofday(&begin, NULL);
		if (info->nhashes <= 0) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "read operation cannot have nhashes set to %Ld\n", info->nhashes);
			return -EINVAL;
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "do_cas_staging get of %Ld hashes\n", info->nhashes);
#ifdef VERBOSE_DEBUG
		for (j = 0; j < info->nhashes; j++) {
			char str[256];

			hash2str(info->phashes + j * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH, str);
			LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "%d: %s\n", j, str);
		}
#endif
		part1 = (info->user_offset % CAPFS_CHUNK_SIZE);
		if (info->aligned_buffer != NULL) {
			ptr = info->aligned_buffer;
		}
		else {
			ptr = info->user_ptr;
		}
		jobs = (struct dataArray *) calloc(info->nhashes, sizeof(struct dataArray));
		if (jobs == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "could not allocate memory\n");
			return -ENOMEM;
		}
		map = (struct iod_map *) calloc(info->nhashes, sizeof(struct iod_map));
		if (map == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "could not allocate memory\n");
			free(jobs);
			return -ENOMEM;
		}
		//sockio_dump_sockaddr(&info->fp->fd.iod[0].addr, stderr);
		/* Need to issue reads to the cas servers */
		for (j = 0; j < info->nhashes; j++) {
			map_chunk(info->begin_chunk + j, info->fp, &map[j]);
			jobs[j].start = ptr +  j * CAPFS_CHUNK_SIZE;
			/*
			 * FIXME: To handle truncates correctly, we probably need to read
			 * minimum (CAPFS_CHUNK_SIZE, info->file_size - (info->begin_chunk + j ) *  CAPFS_CHUNK_SIZE) bytes
			 */
			jobs[j].byteCount = CAPFS_CHUNK_SIZE;
		}
		//sockio_dump_sockaddr(&info->fp->fd.iod[0].addr, stderr);
		/* build a job for the cas servers */
		cas = convert_to_jobs(jobs, info->nhashes, map, info->fp, info->phashes, &niods);
		if (cas == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "could not allocate memory\n");
			free(map);
			free(jobs);
			return -ENOMEM;
		}
		/* feed it to the cas engine */
		clnt_get(info->sp_options->use_tcp, cas, niods);
		for (j = 0; j < niods; j++) {
			ret = *(cas[j].returnValue);
			/*
			 * Due to the way we are handling lseek() and truncate(),
			 * it is possible that we may get ENOENT errors from
			 * the CAS servers, but we can just let them slide,
			 * since it essentially means that the read should see
			 * all zeroes for such data.
			 */
			if (ret < 0 && ret != -ENOENT) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT,"Read operation finished with errors %d\n", ret);
				free(map);
				free(jobs);
				freeJobs(cas, niods);
				return ret;
			}
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "Read operation finished with no errors\n");
		free(map);
		free(jobs);
		freeJobs(cas, niods);
		/*
		 * Now that ptr holds the right data, we need to copy out the requested 
		 * portion of data to the user_ptr address..
		 * i.e we need to copy from ptr + part1 for info->user_bytes bytes.
		 */
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "info = %p, ptr = %p, user_ptr = %p, part1 = %d, user_size = %Ld\n",
				info, ptr, info->user_ptr, part1, info->user_size);
		if (ptr != info->user_ptr) {
			memcpy(info->user_ptr, ptr + part1, MIN(info->user_size, (info->aligned_size - part1)));
		}
		gettimeofday(&end, NULL);
		rpc_get_time += time_diff(&end, &begin);
		return 0;
	}
	/*
	 * If it is a WRITE operation, then we use <info->nchunks,info->pnewhashes>
	 * to shove the data to the CAS servers. The data should be
	 * staged from info->aligned_buffer if that is not NULL,
	 * else it can be staged directly from info->user_ptr.
	 */
	else {
		struct timeval begin, end;

		gettimeofday(&begin, NULL);
		if (info->nchunks <= 0) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "write operation cannot have nchunks set to %Ld\n", info->nchunks);
			return -EINVAL;
		}
		if (info->aligned_buffer != NULL) {
			ptr = info->aligned_buffer;
		}
		else {
			ptr = info->user_ptr;
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "do_cas_staging put of %Ld hashes\n", info->nchunks);
#ifdef VERBOSE_DEBUG
		for (j = 0; j < info->nchunks; j++) {
			char str[256];

			hash2str(info->pnewhashes + j * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH, str);
			LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "%d: %s\n", j, str);
		}
#endif
		jobs = (struct dataArray *) calloc(info->nchunks, sizeof(struct dataArray));
		if (jobs == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "could not allocate memory\n");
			return -ENOMEM;
		}
		map = (struct iod_map *) calloc(info->nchunks, sizeof(struct iod_map));
		if (map == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "could not allocate memory\n");
			free(jobs);
			return -ENOMEM;
		}
		/* Need to issue writes to the cas servers */
		for (j = 0; j < info->nchunks; j++) {
			map_chunk(info->begin_chunk + j, info->fp, &map[j]);
			jobs[j].start = ptr +  j * CAPFS_CHUNK_SIZE;
			jobs[j].byteCount = CAPFS_CHUNK_SIZE;
		}
		/* build a job for the cas servers */
		cas = convert_to_jobs(jobs, info->nchunks, map, info->fp, info->pnewhashes, &niods);
		if (cas == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "could not allocate memory\n");
			free(map);
			free(jobs);
			return -ENOMEM;
		}
		/* feed it to the cas engine */
		clnt_put(info->sp_options->use_tcp, cas, niods);
		for (j = 0; j < niods; j++) {
			ret = *(cas[j].returnValue);
			if (ret < 0) {
				free(map);
				free(jobs);
				freeJobs(cas, niods);
				LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "Write operation finished with errors (server %d error %d)\n", j, ret);
				return ret;
			}
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "Write operation finished with no errors\n");
		free(map);
		free(jobs);
		freeJobs(cas, niods);
		gettimeofday(&end, NULL);
		rpc_put_time += time_diff(&end, &begin);
		return 0;
	}
}

/*
 * This function will attempt to commit the write operation
 * to the meta-data server, failing which it has to *modify*
 * the info structure's current set of hashes to the ones
 * obtained from the meta-data server's wcommit RPC.
 * Therefore this function has *side effects*!!
 *
 * Return value : -error on error
 * 				 :  0 on race conditions
 * 				 :  1 on success.
 */
static int do_cas_commit_write(struct op_info *info)
{
	int i, ret = 1; /* ret must be 1 for success */
	sha1_info old_hashes, new_hashes, current_hashes;
	struct capfs_upcall *op = info->op;
	struct capfs_options opt;
	struct timeval begin, end;

	gettimeofday(&begin, NULL);
	init_capfs_options(&opt, info->sp_options);
	memset(&old_hashes, 0, sizeof(old_hashes));
	memset(&new_hashes, 0, sizeof(new_hashes));
	memset(&current_hashes, 0, sizeof(current_hashes));

	old_hashes.sha1_info_len = info->nhashes;
	if (info->nhashes > 0) {
		old_hashes.sha1_info_ptr = 
			(unsigned char **) calloc(info->nhashes, sizeof(unsigned char *));
		if (old_hashes.sha1_info_ptr == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "could not allocate memory\n");
			ret = -ENOMEM;
			goto cleanup;
		}
		for (i = 0; i < info->nhashes; i++) {
			void *ptr;

			ptr = info->phashes + i * CAPFS_MAXHASHLENGTH;
			old_hashes.sha1_info_ptr[i] = ptr;
		}
	}
	new_hashes.sha1_info_len = info->nchunks;
	if (info->nchunks > 0) {
		new_hashes.sha1_info_ptr = 
			(unsigned char **) calloc(info->nchunks, sizeof(unsigned char *));
		if (new_hashes.sha1_info_ptr == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "could not allocate memory\n");
			ret = -ENOMEM;
			goto cleanup;
		}
		for (i = 0; i < info->nchunks; i++) {
			void *ptr;

			ptr = info->pnewhashes + i * CAPFS_MAXHASHLENGTH;
			new_hashes.sha1_info_ptr[i] = ptr;
		}
	}
	current_hashes.sha1_info_len = info->nchunks;
	if (info->nchunks > 0) 
	{
		int j;

		current_hashes.sha1_info_ptr = 
			(unsigned char **) calloc(info->nchunks, sizeof(unsigned char *));
		if (current_hashes.sha1_info_ptr == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "could not allocate memory\n");
			ret = -ENOMEM;
			goto cleanup;
		}
		for (i = 0; i < info->nchunks; i++) {
			current_hashes.sha1_info_ptr[i] =
				(unsigned char *) calloc(CAPFS_MAXHASHLENGTH, sizeof(unsigned char));
			if (current_hashes.sha1_info_ptr[i] == NULL) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "could not allocate memory\n");
				break;
			}
		}
		if (i != info->nchunks) {
			for (j = 0; j < i; j++) {
				free(current_hashes.sha1_info_ptr[j]);
			}
			ret = -ENOMEM;
			goto cleanup;
		}
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "Committing with %Ld OLD hashes\n", info->nhashes);
#ifdef VERBOSE_DEBUG
	for (i = 0; i < info->nhashes; i++) {
		char str[256];

		hash2str(old_hashes.sha1_info_ptr[i], CAPFS_MAXHASHLENGTH, str);
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "%d: %s\n", i, str);
	}
#endif
	LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "and %Ld NEW hashes\n", info->nchunks);
#ifdef VERBOSE_DEBUG
	for (i = 0; i < info->nchunks; i++) {
		char str[256];

		hash2str(new_hashes.sha1_info_ptr[i], CAPFS_MAXHASHLENGTH, str);
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "%d: %s\n", i, str);
	}
#endif
	/* 
	 * Note that in the case of a race condition, what we do is return 0
	 * and modify info->phashes to have the current set of hashes.
	 * The upper level function will automatically call the do_compute_hashes
	 * function and the whole procedure will be repeated!
	 */
	if (commit_write(&opt, op->v1.fhname, 
						  info->begin_chunk, info->user_size + info->user_offset,
						  &old_hashes, &new_hashes, &current_hashes) < 0) 
	{
		/* only if errno is set to EAGAIN is it a race condition */
		if (errno == EAGAIN) 
		{
			/* indicate that it is a race */
			ret = 0;
			/* update info->phashes and info->nhashes */
			info->nhashes = current_hashes.sha1_info_len;
			if (info->phashes == NULL) {
				LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "WARNING! info->phashes turned out to be NULL. Reallocating\n");
				info->phashes 	= (unsigned char *) calloc(CAPFS_MAXHASHLENGTH, info->nchunks);
			}
			if (info->phashes == NULL) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "Could not allocate memory\n");
				ret = -ENOMEM;
			}
			else {
				LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "WARNING! RACE condition obtained %Ld current hashes\n", info->nhashes);
				for (i = 0; i < info->nhashes; i++) {
#ifdef VERBOSE_DEBUG
					char str[256];
					hash2str(current_hashes.sha1_info_ptr[i], CAPFS_MAXHASHLENGTH, str);
					LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "%d: %s\n", i, str);
#endif
					memcpy(info->phashes + i * CAPFS_MAXHASHLENGTH,
							current_hashes.sha1_info_ptr[i], CAPFS_MAXHASHLENGTH);
				}
				n_retries++;
			}
		}
		else
		{
			ret = -errno;
		}
	}
	else
	{
		ret = 1; /* success */
		/* Update the hcache */
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[wcommit] calling put_hashes on %s from %Ld for %Ld hashes\n",
				op->v1.fhname, info->begin_chunk, info->nchunks);
		put_hashes(op->v1.fhname, info->begin_chunk, info->nchunks, info->pnewhashes);
		/*
		for (i = 0; i < info->nchunks; i++)
		{
			put_hashes(op->v1.fhname, info->begin_chunk + i, 1, current_hashes.sha1_info_ptr[i]);
		}
		*/
	}
	for (i = 0; i < info->nchunks; i++) {
		free(current_hashes.sha1_info_ptr[i]);
	}
	gettimeofday(&end, NULL);
	rpc_commit_time += time_diff(&end, &begin);
cleanup:
	free(old_hashes.sha1_info_ptr);
	free(new_hashes.sha1_info_ptr);
	free(current_hashes.sha1_info_ptr);
	return ret;
}

/* do_rw_op(sp_options, mgr, op, resp)
 *
 * NOTES:
 * I/O daemons in v1 associate every instance of an open file with some
 * socket.  That is, for every file structure they have around, they
 * have a socket associated with that file.  There can, however, be more
 * than one file associated with a given socket -- that isn't a problem.
 *
 * Our goal here will be to use the same sockets over again when a file
 * is opened more than once.  That's not really likely to happen though,
 * unless someone is running multiple application tasks on the same
 * machine (which could eventually be commonplace).  So we're going to
 * end up with lots of connections around.
 *
 * In the long run (ie. v2) we would like to have one or more sets of
 * connections to the I/O daemons that we use for any communication
 * instead of this one set per file nonsense.  For now it is easier to
 * stick with the one per file method.  We'll try to encapsulate things
 * better next time...
 *
 * Returns -errno on failure, 0 on success.
 */
static int do_rw_op(struct capfs_specific_options *sp_options,
		struct sockaddr *mgr, struct capfs_upcall *op, struct capfs_downcall *resp)
{
	int error = 0;
	capfs_size_t size = 0;
	struct pf *pfp;
	fdesc *fp;

	if (op->u.rw.io.type != IO_CONTIG) {
		error = -ENOSYS;
		goto do_rw_op_error;
	}
	
	/* make sure the file is open and ready for access */
	if ((pfp = pf_search(file_list, op->u.rw.handle, op->v1.fhname)) == NULL) {
		/* if it isn't open, open it now */
		if ((error = open_capfs_file(sp_options, mgr, op->v1.fhname)) < 0)
			goto do_rw_op_error;

		/* NOTE: It SHOULD be the case that we never hit this error;
		 * however, we are in fact having this problem.  Until I can
		 * discover why this test is failing when the open succeeds, this
		 * workaround will be here to keep the capfsd from segfaulting.
		 */
		/* UPDATE 2-8-2001: Maybe fixed now?  do_open_req() was not always
		 * returning the correct value or setting correct error values on
		 * failure.  This propogated through open_capfs_file().
		 */
		/* UPDATE 8-22-2001: Nope.  This is still a problem. -- Rob
		 */
		if ((pfp = pf_search(file_list, op->u.rw.handle, op->v1.fhname)) == NULL)
		{
			PERROR( "NULL returned from pf_search after successful open\n");
			error = -EINVAL; /* as good as anything... */
			goto do_rw_op_error;
		}
	}
	fp = pfp->fp;

	fp->fd.off = op->u.rw.io.u.contig.off;

	if (op->u.rw.io.u.contig.size != op->xfer.size)  {
		error = -EINVAL;
		goto do_rw_op_error;
	}

	if(op->xfer.size == 0) {
		error = 0;
		size  = 0;
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "0-byte reads/writes?\n");
		goto do_rw_op_complete;
	}

	init_res(resp, op);
	resp->xfer.orig_size = op->xfer.size;
	resp->xfer.from_kmem = op->xfer.to_kmem;
	resp->xfer.ptr = op->xfer.ptr;
	resp->xfer.to_free_ptr = op->xfer.to_free_ptr;

	/* if operating as capfs, send request to cas servers */
	if (capfs_mode == 1) 
	{
		struct op_info info;

		memset(&info, 0, sizeof(info));
		if (op->type == READ_OP) {
			info.type = IOD_RW_READ;
		}
		else if(op->type == WRITE_OP) {
			info.type = IOD_RW_WRITE;
		}
		else {
			error = -EINVAL;
			goto do_rw_op_error;
		}
		info.fp = fp;
		info.fhname = op->v1.fhname;
		info.user_ptr = op->xfer.ptr;
		info.user_offset = fp->fd.off;
		info.user_size = op->xfer.size;
		info.op = op;
		info.sp_options = sp_options;

		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[%s] user pointer %p of size: %Ld\n",
				info.type == IOD_RW_READ ? "READ ": "WRITE ", info.user_ptr,
				info.user_size);
		/* Initiate fetching of the hashes */
		if ((error = do_get_hashes(&info)) < 0) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "Could not get hashes: %d\n", error);
			info_dtor(&info);
			goto do_rw_op_error;
		}
		/* read operation with no hashes on the meta-data server */
		if (info.nhashes == 0 && op->type == READ_OP) {
			size = 0;
			info_dtor(&info);
			goto do_rw_op_complete;
		}
		if (op->type == WRITE_OP) {
write_retry:
			if ((error = do_compute_hashes(&info)) < 0) {
				info_dtor(&info);
				goto do_rw_op_error;
			}
			LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "Computed %Ld hashes for put\n", info.nchunks);
#ifdef VERBOSE_DEBUG
			{
				int i;
				for (i = 0; i < info.nchunks; i++) {
					char str[256];

					hash2str(info.pnewhashes + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH, str);
					LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "%d: %s\n", i, str);
				}
			}
#endif
			size = op->xfer.size;
			LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "about to stage WRITE operation. should return %Ld bytes\n", size);
		}
		else {
			/* This is what the user would expect as its return value */
			size = MIN((info.file_size - fp->fd.off), info.user_size);
			LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "about to stage READ operation. should return %Ld bytes\n", size);
			if (size == 0) {
				info_dtor(&info);
				goto do_rw_op_error;
			}
		}
		/* cool, so now we can stage the I/O operation to the cas servers */
		if ((error = do_cas_data_staging(&info)) < 0) {
			info_dtor(&info);
			goto do_rw_op_error;
		}
		if (op->type == WRITE_OP) {
			int commit_status;

			/* Writes need to commit */
			if ((commit_status = do_cas_commit_write(&info)) < 0) {
				error = commit_status;
				info_dtor(&info);
				goto do_rw_op_error;
			}
			else if (commit_status == 0) {
				/* we must have raced. So let us retry */
				goto write_retry;
			}
			error = 0;
		}
		/* free up info structure */
		info_dtor(&info);
		error = 0;
	}
do_rw_op_complete:
	if (size > op->xfer.size) {
		PERROR( "v1_xfer write size mismatch!\n");
		PERROR( "returned size reported: %ld\n", (long)size);
		PERROR( "returned size set to: %ld\n", (long)op->xfer.size);
		size = op->xfer.size;
	}
	resp->xfer.size = size;
	resp->u.rw.size = size;
do_rw_op_error:
	resp->error = error;
	return error;
}

/*** INTERNAL, SUPPORT FUNCTIONS BELOW HERE ***/

/* ping_cas_servers() */
static void ping_cas_servers(fdesc *fp, int tcp)
{
	int i;
	for (i = 0; i < fp->fd.meta.p_stat.pcount; i++)
	{
		struct sockaddr_in *iod_addr = &fp->fd.iod[i].addr;
		clnt_ping(tcp, (struct sockaddr *) iod_addr);
	}
	return;
}

/* create_capfs_file(sp_options, mgr, name, meta, phys)
 *
 * Create a new file with the given physical distribution and metadata.
 *
 * NOTE:
 * Actually only certain values of the metadata are used, in particular
 * the mode and so on.  I'm not going to talk too much about that here,
 * because I don't remember off the top of my head exactly what values
 * in there are actually used.  If you really want to know, you should
 * look at the manager code.
 *
 * This function takes the same name format as the do_XXX_op calls, as
 * it needs the manager information for adding to the open file list.
 *
 * Returns 0 on success error value (< 0) on failure.
 */
static int create_capfs_file(struct capfs_specific_options *sp_options, 
		struct sockaddr *mgr, capfs_char_t name[], struct capfs_meta *meta, struct capfs_phys *phys)
{
	int error = 0;
	mreq req;
	fdesc *fp = NULL;

	/* initialize request to manager */
	memset(&req, 0, sizeof(req));
	req.majik_nr   = MGR_MAJIK_NR;
	req.release_nr = CAPFS_RELEASE_NR;
	req.type       = MGR_OPEN;
	req.uid        = 0; /* root */
	req.gid        = 0; /* root */
	req.dsize      = 0; /* set in do_open_req() */
	req.req.open.flag        = O_RDWR | O_CREAT | O_EXCL;
	req.req.open.meta.fs_ino = 0; /* probably needs to be set... */
	v2_meta_to_v1_fmeta(meta, &(req.req.open.meta));
	v2_phys_to_v1_fmeta(phys, &(req.req.open.meta));

	/* send the request, save the open file information */
	if ((fp = do_open_req(sp_options, mgr, &req, name, &error)) == NULL)
		return error;

	/* Let us open all connections to the IOD servers for predictable I/O performance */
	ping_cas_servers(fp, sp_options->use_tcp);
	return 0;
}

/* open_capfs_file(sp_options, mgr, name)
 *
 * Opens the CAPFS file named "name" held by the manager referenced by
 * "mgr".  Only operates on regular files, not directories.
 *
 * Finally we connect to the IODs.  This is the point where the eepro
 * driver craps out <smile>.
 *
 * Returns error value (negative) on failure, 0 on success.
 */
static int open_capfs_file(struct capfs_specific_options *sp_options,
		struct sockaddr *mgr, capfs_char_t name[])
{
	int error = 0;
	mreq req;
	fdesc *fp = NULL;

	/* initialize request to manager */
	memset(&req, 0, sizeof(req));
	req.majik_nr   = MGR_MAJIK_NR;
	req.release_nr = CAPFS_RELEASE_NR;
	req.type       = MGR_OPEN;
	req.uid        = 0; /* root */
	req.gid        = 0; /* root */
	req.dsize      = 0; /* set in do_open_req() */
	req.req.open.flag        = O_RDWR;
	req.req.open.meta.fs_ino = 0; /* probably needs to be set... */

	/* send the request, save the open file information */
	if ((fp = do_open_req(sp_options, mgr, &req, name, &error)) == NULL)
		return error;
	/* Let us open all connections to the IOD servers for predictable I/O performance */
	ping_cas_servers(fp, sp_options->use_tcp);
	return 0;
}

/* do_open_req()
 *
 * Used by create_capfs_file() and open_capfs_file() to communicate with
 * the manager and allocate any necessary memory.
 *
 * This function also takes the long name format, as it is used when
 * storing the open file information.
 *
 * First we send an open request to the manager.  This results in the
 * IOD addresses being returned to us (on success).
 *
 * Next we store up all this information so we can connect to these
 * guys again later.  We have to store it in the fdesc format because
 * that's what we'll be passing into build_rw_jobs() inside of
 * do_rw_op().  This is a little sad, but it's the easiest thing to do.
 *
 * Returns a pointer to the fdesc structure on success, NULL on failure.
 * In the event of failure, the error value is returned in the region
 * pointed to by errp.
 *
 */
static fdesc *do_open_req(struct capfs_specific_options *sp_options,
		struct sockaddr *mgr, mreq *reqp, capfs_char_t name[], int *errp)
{
	int error = 0, ct, i;
	int64_t nhashes = 0;
	fdesc *fp = NULL;
	mack ack;
	capfs_handle_t handle;
	struct pf *p = NULL;
	char *fn, *phashes = NULL;
	int hashes_count = 0, free_hashes = 0;
	iod_info *ptr = NULL;
	struct ackdata_c ackdata;
	struct capfs_options opt;
	struct plugin_info *pinfo = NULL;
	int (*pre_open)(const char *, char **pphashes, int *hcount) = NULL;
	int (*post_open)(const char*, char *phashes, int hcount) = NULL;

	init_capfs_options(&opt, sp_options);
	/* preallocate so that we can receive the iod information directly */
	ptr = (iod_info *) calloc(CAPFS_MAXIODS, sizeof(iod_info));
	if (!ptr) {
		*errp = -errno;
		return NULL;
	}
	/*
	 * if plugins define an open routine, then it is their job to determine how many
	 * hashes to receive as well as to allocate a piece of memory location holding
	 * as many hashes.
	 */
	if (sp_options->use_hcache == 1)
	{
		pinfo = capfsd_match_policy_id(sp_options->cons);
		if (pinfo && (pre_open = pinfo->policy_ops->pre_open))
		{
			pre_open(name, &phashes, &hashes_count);
			/* plugin layer's job to free phashes */
			free_hashes = 2;
		}
		else
		{
			free_hashes = 1;
			hashes_count = CAPFS_MAXHASHES;
			phashes = (char *) calloc(CAPFS_MAXHASHLENGTH, hashes_count);
			if (phashes == NULL) {
				free_hashes = 0;
				hashes_count = 0;
			}
		}
		/* it is not an error if phashes remains NULL here... */
	}
   /* send off the request to the manager */
	fn = skip_to_filename(name);
	reqp->dsize = strlen(fn); /* have to fix data length to match name */
	reqp->req.open.ackdsize = 0;
	ackdata.type = reqp->type;
	ackdata.u.open.niods = CAPFS_MAXIODS;
	ackdata.u.open.iod = ptr;
	LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[open]: requested %d hashes\n", hashes_count);
	/* Indicate how many hashes you are willing to receive if hcache is enabled for this file-system */
	ackdata.u.open.nhashes = (sp_options->use_hcache == 1) ? hashes_count : 0;
	ackdata.u.open.hashes = phashes;
	
	/* if this function fails, most likely mgr went down or something...communication error */
	if (send_mreq_saddr(&opt, mgr, reqp, fn, &ack, &ackdata) < 0) 
	{
		*errp = -errno;
		free(ptr);
		/* Core allocated hashes */
		if (free_hashes == 1)
		{
			free(phashes);
		}
		else if (free_hashes == 2)
		{
			/* See if plugins have defined a post_open routine, if so cleanup operations... */
			if (pinfo && (post_open = pinfo->policy_ops->post_open))
			{
				post_open(name, phashes, -1);
			}
		}
		return NULL;
	}
	/* actually an error in the opening of the file */
	else if (ack.status) 
	{
		free(ptr);
		/* Core allocated hashes */
		if (free_hashes == 1)
		{
			free(phashes);
		}
		else if (free_hashes == 2)
		{
			/* See if plugins have defined a post_open routine, if so cleanup operations... */
			if (pinfo && (post_open = pinfo->policy_ops->post_open))
			{
				post_open(name, phashes, -1);
			}
		}
		errno = ack.eno;
		*errp = -errno;
		return NULL;
	}
	/* how many hashes did we actually receive if hcache was enabled? */
	if (sp_options->use_hcache == 1)
	{
		nhashes = ackdata.u.open.nhashes;
		if (nhashes < 0) 
		{
			free(ptr);
			/* core allocated hashes */
			if (free_hashes == 1)
			{
				free(phashes);
			}
			else if (free_hashes == 2)
			{
				/* See if plugins have defined a post_open routine, if so cleanup operations... */
				if (pinfo && (post_open = pinfo->policy_ops->post_open))
				{
					post_open(name, phashes, -1);
				}
			}
			errno = -nhashes;
			*errp = nhashes;
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "[open] error in retrieving hashes %s\n", strerror(errno));
			return NULL;
		}
	}
	/* check the dsize, pcount, and sanity */
	if (ack.dsize == 0) /* badness */ 
	{
		free(ptr);
		/* core allocated hashes */
		if (free_hashes == 1)
		{
			free(phashes);
		}
		else if (free_hashes == 2)
		{
			/* See if plugins have defined a post_open routine, if so cleanup operations... */
			if (pinfo && (post_open = pinfo->policy_ops->post_open))
			{
				post_open(name, phashes, -1);
			}
		}
		*errp = -EBADMSG;
		return NULL;
   }
	if ((ct = ack.ack.open.meta.p_stat.pcount) < 1) 
	{
		free(ptr);
		/* core allocated hashes */
		if (free_hashes == 1)
		{
			free(phashes);
		}
		else if (free_hashes == 2)
		{
			/* See if plugins have defined a post_open routine, if so cleanup operations... */
			if (pinfo && (post_open = pinfo->policy_ops->post_open))
			{
				post_open(name, phashes, -1);
			}
		}
		*errp = -EINVAL;
		return NULL;
   }
	if (ack.dsize != sizeof(iod_info) * ct) 
	{
		free(ptr);
		/* core allocated hashes */
		if (free_hashes == 1)
		{
			free(phashes);
		}
		else if (free_hashes == 2)
		{
			/* See if plugins have defined a post_open routine, if so cleanup operations... */
			if (pinfo && (post_open = pinfo->policy_ops->post_open))
			{
				post_open(name, phashes, -1);
			}
		}
		PERROR( "size mismatch receiving iod info\n");
		*errp = -EINVAL;
		return NULL;
	}
	/* receive and fill in the file descriptor structure */
	if ((fp = (fdesc *)calloc(1, sizeof(*fp) + sizeof(iod_info) * (ct-1))) == NULL)
	{
		*errp = -errno;
		free(ptr);
		/* core allocated hashes */
		if (free_hashes == 1)
		{
			free(phashes);
		}
		else if (free_hashes == 2)
		{
			/* See if plugins have defined a post_open routine, if so cleanup operations... */
			if (pinfo && (post_open = pinfo->policy_ops->post_open))
			{
				post_open(name, phashes, -1);
			}
		}
		return NULL;
	}
	/* ptr already holds the iod_information structures */
	memcpy(fp->fd.iod, ptr, ack.dsize);
	/* phashes holds the hashes for this file, put it in the hcache */
	if (sp_options->use_hcache == 1 && nhashes > 0) 
	{
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[open] on %s calling put_hashes from 0 for %Ld hashes\n", 
				name, nhashes);
		/* hcache needs to be told the entire file name */
		put_hashes(name, 0, nhashes, phashes);
	}
	free(ptr);
	/* core allocated hashes */
	if (free_hashes == 1)
	{
		free(phashes);
	}
	else if (free_hashes == 2)
	{
		/* See if plugins have defined a post_open routine, if so call it with the obtained set of hashes... */
		if (pinfo && (post_open = pinfo->policy_ops->post_open))
		{
			post_open(name, phashes, nhashes);
		}
	}

	fp->fd.meta = ack.ack.open.meta;
	fp->fd.cap = ack.ack.open.cap;
	fp->fd.off = 0;
	fp->fs = FS_CAPFS;
	handle = ack.ack.open.meta.u_stat.st_ino;

	/* store fdesc in our file list for use later */
	if (file_list == NULL)
		file_list = pfl_new();
	if (file_list == NULL)
	{
		LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "could not allocate memory\n");
		error = -ENOMEM;
		goto open_capfs_file_error;
	}
	if ((p = pf_new(fp, handle, name)) == NULL) 
	{
		LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "could not allocate memory\n");
		error = -ENOMEM;
		goto open_capfs_file_error;
	}
	if ((error = pf_add(file_list, p)) < 0) 
	{
		PERROR( "Error adding file handle to list.\n");
		goto open_capfs_file_error;
	}

	for (i = 0; i < ct; i++) 
	{
		//sockio_dump_sockaddr((struct sockaddr_in *)&fp->fd.iod[i].addr, stderr);
		/* dont connect to the iod's now. just add them to the iodinfo table */
		if((fp->fd.iod[i].sock = instantiate_iod_entry((struct sockaddr *)&fp->fd.iod[i].addr)) < 0) 
		{
			error = -errno;
			PERROR( "Could not instantiate iod entry!\n");
			goto open_capfs_file_error;
		}
		inc_ref_count(fp->fd.iod[i].sock);
	}
	return fp;

open_capfs_file_error:
	if (p != NULL) {
		pf_free(p); /* frees the structure pointed to by fp as well */
		fp = NULL;
	}
	if (fp != NULL) {
		free(fp);
		fp = NULL;
	}
	if (free_hashes == 2)
	{
		/* See if plugins have defined a post_open routine, if so cleanup operations... */
		if (pinfo && (post_open = pinfo->policy_ops->post_open))
		{
			post_open(name, phashes, -1);
		}
	}
	*errp = error;
	return NULL;
}

/* init_mgr_req()
 *
 * initializes fields in the manager request
 */
static void init_mgr_req(mreq *rp, struct capfs_upcall *op)
{
	memset(rp, 0, sizeof(*rp));
	rp->majik_nr   = MGR_MAJIK_NR;
	rp->release_nr = CAPFS_RELEASE_NR;
	rp->uid        = op->proc.uid;
	rp->gid        = op->proc.gid;
	return;
}

/* init_res() 
 *
 * initializes the fields in a struct capfs_downcall
 */
static void init_res(struct capfs_downcall *resp, struct capfs_upcall *op)
{
	memset(resp, 0, sizeof(*resp));
	resp->magic = CAPFS_DOWNCALL_MAGIC;
	resp->seq = op->seq;
	resp->type = op->type;
	return;
}

/* v1_fmeta_to_v2_meta()
 *
 * Copy values from a v1 fmeta structure into a v2 meta structure.
 */
static void v1_fmeta_to_v2_meta(struct fmeta *fmeta, struct capfs_meta *meta)
{
	meta->valid = V_MODE|V_UID|V_GID|V_SIZE|V_TIMES|V_BLKSIZE|V_BLOCKS;


	meta->handle = fmeta->u_stat.st_ino;
	meta->mode = fmeta->u_stat.st_mode;
	meta->uid = fmeta->u_stat.st_uid;
	meta->gid = fmeta->u_stat.st_gid;
	meta->size = fmeta->u_stat.st_size;
	meta->atime = fmeta->u_stat.atime;
	meta->mtime = fmeta->u_stat.mtime;
	meta->ctime = fmeta->u_stat.ctime;
	LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "atime: %lu mtime: %lu ctime: %lu\n", 
			meta->atime, meta->mtime, meta->ctime);
	meta->blksize = fmeta->p_stat.ssize;
	if (fmeta->p_stat.ssize) {
		meta->blocks = meta->size / fmeta->p_stat.ssize + 
			(meta->size % fmeta->p_stat.ssize ? 1 : 0);
	}
	else meta->blocks = 0;

	return;
}

/* v2_meta_to_v1_fmeta()
 *
 * Copy values from a v2 capfs_meta structure into a v1 fmeta structure.
 */
static void v2_meta_to_v1_fmeta(struct capfs_meta *meta, struct fmeta *fmeta)
{
	memset(fmeta, 0, sizeof(*fmeta));
	fmeta->u_stat.st_ino = meta->handle;
	if (meta->valid & V_MODE) fmeta->u_stat.st_mode = meta->mode;
	if (meta->valid & V_UID) fmeta->u_stat.st_uid = meta->uid;
	if (meta->valid & V_GID) fmeta->u_stat.st_gid = meta->gid;
	if (meta->valid & V_SIZE) {
		fmeta->u_stat.st_size = meta->size;
	}
	if (meta->valid & V_TIMES) {
		fmeta->u_stat.atime = meta->atime;
		fmeta->u_stat.mtime = meta->mtime;
		fmeta->u_stat.ctime = meta->ctime;
	}
#if 0
	if (meta->valid & V_SIZE) fmeta->p_stat.ssize = meta->blksize;
#endif

	return;
}

/* v1_fmeta_to_v2_phys()
 *
 * Copy values from a v1 fmeta structure into a v2 capfs_phys structure.
 */
static void v1_fmeta_to_v2_phys(struct fmeta *fmeta, struct capfs_phys *phys)
{
	phys->blksize = fmeta->p_stat.ssize;
	phys->nodect = fmeta->p_stat.pcount;
	phys->dist = DIST_RROBIN;
	return;
}

/* v2_phys_to_v1_fmeta()
 *
 * Copy values from a v2 capfs_phys structure into a v1 fmeta structure.
 */
static void v2_phys_to_v1_fmeta(struct capfs_phys *phys, struct fmeta *fmeta)
{
	if (phys->blksize > 0)
		fmeta->p_stat.ssize = phys->blksize;
	else
		fmeta->p_stat.ssize = -1;
	if (phys->nodect > 0) 
		fmeta->p_stat.pcount = phys->nodect;
	else
		fmeta->p_stat.pcount = -1;

	/* for now we won't try to do anything with the iod lists */
	/* fmeta->p_stat.base = -1; */
	fmeta->p_stat.base = 0;
	LOG(stderr, INFO_MSG, SUBSYS_CLIENT, "create base [%d], nriods [%d], ssize [%d]\n",
			fmeta->p_stat.base, fmeta->p_stat.pcount, fmeta->p_stat.ssize);
	return;
}

/* v1_ack_to_v2_statfs()
 *
 * Copy values from v1 ack structure into a v2 capfs_statfs structure
 */
static void v1_ack_to_v2_statfs(struct mack *ack, struct capfs_statfs *sfs)
{
	sfs->bsize = CAPFS_OPT_IO_SIZE;
	sfs->blocks = ack->ack.statfs.tot_bytes / (capfs_size_t) CAPFS_OPT_IO_SIZE;
	sfs->bfree = ack->ack.statfs.free_bytes / (capfs_size_t) CAPFS_OPT_IO_SIZE;
	sfs->bavail = sfs->bfree;
	sfs->files = ack->ack.statfs.tot_files;
	sfs->ffree = ack->ack.statfs.free_files;
	sfs->namelen = ack->ack.statfs.namelen;
	return;
}

/* OPEN FILE STORAGE FUNCTIONS
 *
 * So what's the deal here?
 *
 * Well, basically we need a way to hold on to open file information.
 * So we build a list of open files which we can search by handle/name
 * combination.
 *
 * OPERATIONS:
 *
 * pfl_new() - called once, creates a list for us
 * pf_new() - hands back an initialized pf (capfs file) structure
 * pf_add() - adds a pf structure into a pf list
 * pf_search() - looks for a pf matching a given file handle and name
 *
 * etc.
 */

static pfl_t pfl_new(void)
{
	return llist_new();
}

static struct pf *pf_new(fdesc *fp, capfs_handle_t handle, char *name)
{
	struct pf *p;

	/* allocate space for the string along with the structure */
	p = (struct pf *)calloc(1, sizeof(*p) + strlen(name) + 1);
	if (p == NULL) return NULL;

	p->handle = handle;
	p->name = (char *) p + sizeof(*p);
	p->ltime = time(NULL);
	p->fp = fp;
	strcpy(p->name, name);
	return p;
}

static int pf_add(pfl_t pfl, struct pf *p)
{
	if (llist_add(pfl, (void *) p) == 0) return 0;
	else if (errno != 0) return -errno;
	else return -1;
}

/* pf_search() - Searches a list for a matching item using both the
 * handle and the full name (including manager and port).  Updates the
 * ltime field of the entry before returning.
 */
static struct pf *pf_search(pfl_t pfl, capfs_handle_t handle, char *name)
{
	struct pf_cmp cmp;
	struct pf *ret;
	
	cmp.handle = handle;
	cmp.name = name;
	ret = (struct pf *) llist_search(pfl, (void *) &cmp, pf_handle_cmp);
	if (ret != NULL) ret->ltime = time(NULL);
	return ret;
}

static struct pf *pfl_head(pfl_t pfl)
{
	return (struct pf *) llist_head(pfl);
}

/* pf_handle_cmp(cmp, pfp)
 *
 * Compares both handle and manager name; Returns 0 on match, non-zero
 * if no match
 */
static int pf_handle_cmp(void *cmp, void *pfp)
{
	/* do a handle match first because it should be really quick */
	if (((struct pf_cmp *) cmp)->handle != ((struct pf *) pfp)->handle)
		return -1;
	
	/* if we get this far, compare the names using strcmp() */
	return strcmp(((struct pf_cmp *) cmp)->name, ((struct pf *) pfp)->name);
}

/* pf_ltime_olderthan(timep, pfp)
 *
 * Returns 0 if the time stored in the structure is less than (older
 * than) the time passed as comparison.
 */
static int pf_ltime_olderthan(void *time, void *pfp)
{
	if (((struct pf *) pfp)->ltime < *((time_t *) time)) return 0;
	return 1;
}

/* pf_ltime_cmp(timep, pfp)
 *
 * Returns the time value stored in the file structure minus the time
 * value pointed to by timep.  Thus the resulting value is positive if
 * the file has been touched since the time passed in and negative if
 * the file hasn't been touch since that time.
 */
static int pf_ltime_cmp(void *time, void *pfp)
{
	return (((struct pf *) pfp)->ltime - *((time_t *) time));
}

/* pf_rem(pfl, handle)
 *
 * Finds and removes an instance of an open file with matching handle
 * and manager.  Does not free the structure.
 *
 * Returns 0 on successful remove, -1 if no instance was found.
 */
static int pf_rem(pfl_t pfl, capfs_handle_t handle, char *name)
{
	struct pf *p;
	struct pf_cmp cmp;
	
	cmp.handle = handle;
	cmp.name = name;

	p = (struct pf *) llist_rem(pfl, (void *) &cmp, pf_handle_cmp);
	if (p) return 0;
	else return -1;
}

/* pf_free(p)
 *
 * Frees data structures pointed to within a pf structure and then frees
 * the structure itself.
 */
static void pf_free(void *p)
{
	free(((struct pf *)p)->fp);
	free(p);
	return;
}

static void pfl_cleanup(pfl_t pfl)
{
	llist_free(pfl, pf_free);
	return;
}

/* CAPFS NAME-RELATED FUNCTIONS BELOW */

static char *skip_to_filename(char *name)
{
	while (*(++name) != '/' && *name != '\0');

	return name;
}

static int name_to_port(char *name)
{
	char buf[10];

	while (*name != ':') name++;
	name++; /* point past the ':' */
	strncpy(buf, name, 9);
	buf[9] = '\0';
	name = buf;
	while (*name != '/' && *name != '\0') name++;
	*name = '\0';
	return atoi(buf);
}

static void hostcpy(char *host, char *name)
{
	int len;
	char *end;

	end = name;
	while (*end != ':') end++;

	/* we won't copy more than the space available, and we'll let the
	 * resolution code give us the error
	 */
	len = ((end - name) < CAPFSHOSTLEN) ? (end - name) : CAPFSHOSTLEN-1;
	strncpy(host, name, len);
	host[len] = '\0';
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

