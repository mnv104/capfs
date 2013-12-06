#include "rpcutils.h"
#include "capfs-header.h"
#include "mgr_prot.h"
#include <rpc/rpc.h>
#include <rpc/clnt.h>
#include <netinet/in.h>
#include <syslog.h>
#include <grp.h>
#include <pwd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <req.h>
#include <capfs_config.h>
#include <meta.h>
#include <desc.h>
#include <log.h>
#include <sockio.h>
#include "mgr_prot_common.h"
#include "mgr.h"
#include "cmgr.h"
#include "hcache.h"
#include "quicklist.h"
#include "mquickhash.h"
#include "capfsd_prot.h"

static int do_noop(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_chmod(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_chown(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_access(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_truncate(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_ctime(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_utime(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_fstat(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_stat(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_lookup(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_statfs(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_unlink(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_close(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_open(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_rename(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_link(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_readlink(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_iod_info(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_mkdir(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_fchown(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_fchmod(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_rmdir(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_getdents(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);
static int do_gethashes(struct capfs_options*, struct sockaddr *, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p);

/* GLOBALS */
static int (*reqfn[])(struct capfs_options*, struct sockaddr *, mreq_p, void *, mack_p, struct ackdata_c *) = {
	do_chmod,
	do_chown,
	do_close,
	do_stat, /*really this is do_lstat*/
	do_noop,
	do_open,
	do_unlink,
	do_noop,
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
/*** ADD NEW CALLS ABOVE THIS LINE ***/
	"error",
	"error",
	"error",
	"error",
	"error"
};

extern int64_t get_hashes(int use_hcache, char *name, int64_t begin_chunk, 
	int64_t nchunks, int64_t prefetch_index, void *buf, fmeta *meta);

static inline void init_defaults(mack *ack, int type)
{
	ack->majik_nr = MGR_MAJIK_NR;
	ack->release_nr = CAPFS_RELEASE_NR;
	ack->type = type;
	return;
}

static inline void init_ackstatus(mack *ack, opstatus *status)
{
	if (status) {
		ack->status = status->status;
		ack->eno = status->eno; 
	}
	return;
}

enum {USED = 1, UNUSED = 0};

#define MAXMGRS 16

static pthread_mutex_t mgr_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct mgr_entry mgr_entry;

struct mgr_entry {
	int used;
	struct sockaddr_in addr;
	CLIENT *clnt;
	struct sockaddr_in our_addr;
};
static mgr_entry mgr_table[MAXMGRS];
static int mgr_count = 0;

/* callback id to be used on opens/closes of files */
static int my_cb_id = -1;
/* This variable decides if the callback registration is needed or not, */
extern int check_for_registration;

/* 
 * This variable determines whether or not CLIENT handles to meta-servers
 * need to be cached.
 * i.e we cache handles if it is set to 1
 * and re-connect everytime if it is set to 0.
 * It is okay if the client-side daemon experiences a disconnect
 * if and when the manager is restarted for whatever reason, but
 * that is expected to be fairly rare. So the rationale here is that
 * it is a good thing if the client-side daemon caches CLIENT handles to
 * md server and iod server
 */
static int cache_handle_policy = CAPFS_MGR_CACHE_HANDLES;

static int convert_to_errno(enum clnt_stat rpc_error)
{
	if (rpc_error == RPC_SUCCESS) {
		return 0;
	}
	if (rpc_error == RPC_CANTDECODEARGS
			|| rpc_error == RPC_CANTDECODEARGS
			|| rpc_error == RPC_CANTSEND
			|| rpc_error == RPC_CANTRECV
			|| rpc_error == RPC_CANTDECODERES
			|| rpc_error == RPC_AUTHERROR) {
		return EREMOTEIO;
	}
	if (rpc_error == RPC_TIMEDOUT) {
		return ETIMEDOUT;
	}
	if (rpc_error == RPC_PROGNOTREGISTERED
			|| rpc_error == RPC_PROCUNAVAIL
			|| rpc_error == RPC_VERSMISMATCH) {
		return ECONNREFUSED;
	}
	/* I know this will cause incorrect error messages to pop up,but theres no choice */
	return EINVAL;
}

static int find_id_of_host(struct sockaddr_in *raddr)
{
	int i, j;

	i = 0;
	j = 0;
	while (i < mgr_count && j < MAXMGRS) {
		if (mgr_table[j].used == USED) {
			/* Matching the host addresses and port #s */
			if (mgr_table[j].addr.sin_addr.s_addr == raddr->sin_addr.s_addr
					&& mgr_table[j].addr.sin_port == raddr->sin_port) {
				return j;
			}
			i++;
		}
		j++;
	}
	return -1;
}

static int find_unused_id(void)
{
	int i;

	i = 0;
	while (i < MAXMGRS) {
		if (mgr_table[i].used != USED) {
			return i;
		}
		i++;
	}
	return -1;
}

static CLIENT** get_clnt_handle(int tcp, struct sockaddr_in *mgraddr)
{
	int id = -1;
	CLIENT *clnt = NULL, **pclnt = NULL;
	static struct timeval new_mgr_timeout = {MGR_CLNT_TIMEOUT, 0};

	pthread_mutex_lock(&mgr_mutex);

	if ((id = find_id_of_host(mgraddr)) < 0) {
		char *mgr_host = NULL;

		id = find_unused_id();
		mgr_host = inet_ntoa(mgraddr->sin_addr);
		mgr_table[id].used = USED;
		memcpy(&mgr_table[id].addr, mgraddr, sizeof(struct sockaddr_in));
		mgr_count++;
		
		/*
		 * mgr_table[id].clnt = clnt = clnt_create(mgr_host, CAPFS_MGR,
		 * mgrv1, (tcp == 1) ? "tcp" : "udp");
		 */
		mgr_table[id].clnt = clnt = get_svc_handle(mgr_host, CAPFS_MGR, mgrv1, 
				(tcp == 1) ? IPPROTO_TCP : IPPROTO_UDP, MGR_CLNT_TIMEOUT, (struct sockaddr *)&mgr_table[id].our_addr);
		if (clnt == NULL) {
			clnt_pcreateerror (mgr_host);
		}
		/* set the timeout to a fairly large value... */
		else {
			if (clnt_control(clnt, CLSET_TIMEOUT, (char *)&new_mgr_timeout) == 0)
			{
				LOG(stderr, WARNING_MSG, SUBSYS_META, "Cannot reset timeout.. continuing with 25 second timeout\n");
			}
		}
	}
	else {
		if ((clnt = mgr_table[id].clnt) == NULL) {
			char *mgr_host = NULL;

			mgr_host = inet_ntoa(mgraddr->sin_addr);
			/*
			 * mgr_table[id].clnt = clnt = clnt_create(mgr_host, CAPFS_MGR, 
			 * mgrv1, (tcp == 1) ? "tcp":"udp");
			 */
			mgr_table[id].clnt = clnt = get_svc_handle(mgr_host, CAPFS_MGR, mgrv1,
					(tcp == 1) ? IPPROTO_TCP : IPPROTO_UDP, MGR_CLNT_TIMEOUT, (struct sockaddr *)&mgr_table[id].our_addr);
			if (clnt == NULL) {
				clnt_pcreateerror (mgr_host);
			}
			else
			{
				if (clnt_control(clnt, CLSET_TIMEOUT, (char *)&new_mgr_timeout) == 0)
				{
					LOG(stderr, WARNING_MSG, SUBSYS_META, "Cannot reset timeout.. continuing with 25 second timeout\n");
				}
			}
		}
	}
	pclnt = &mgr_table[id].clnt;
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "My address to MGR is %s\n", inet_ntoa(mgr_table[id].our_addr.sin_addr));
	pthread_mutex_unlock(&mgr_mutex);
	return pclnt;
}

static void put_clnt_handle(CLIENT **pclnt, int force_put)
{
	if (pclnt == NULL || 
			*pclnt == NULL) {
		return;
	}
	/* This will be set on an error condition only */
	if (force_put) 
	{
		clnt_destroy(*pclnt);
		/* make it reconnect next time around */
		*pclnt = NULL;
		/* we need to re-register also */
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "put_clnt_handle forcing re-registering callback\n");
		check_for_registration = 1;
		/* we also need to invalidate our hcaches here */
		hcache_invalidate();
		return;
	}
	/* This is more of a performance thing than any error condition */
	if (cache_handle_policy == 0) {
		clnt_destroy(*pclnt);
		/* make it reconnect */
		*pclnt = NULL;
	}
	return;
}

/* What address should we register for callback with this manager? */
static struct sockaddr* get_registration_address(struct sockaddr_in *mgr_addr)
{
	int id;
	struct sockaddr* register_addr = NULL;

	pthread_mutex_lock(&mgr_mutex);
	/* bummer! How come we did not find this address???? */
	if ((id = find_id_of_host(mgr_addr)) < 0) {
		pthread_mutex_unlock(&mgr_mutex);
		return NULL;
	}
	register_addr = (struct sockaddr *) &mgr_table[id].our_addr;
	pthread_mutex_unlock(&mgr_mutex);
	return register_addr;
}

/*
 * Register your local service's program/version/protocol
 * information with the manager host if necessary.
 * Returns 0 on success and -1 on failure.
 */
int capfs_cbreg(struct capfs_options* opt, struct sockaddr *mgr_host, int prog, int vers, int proto)
{
	cb_args args;
	cb_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat result;
	int tcp;
	char *my = NULL;
	struct sockaddr_in *my_address;


	/* Already registered! */
	if (my_cb_id >= 0) {
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "Re-registering callback id...\n");
	}
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr_host);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		return -1;
	}
	/* get our registration address for this manager! */
	my_address = (struct sockaddr_in *) get_registration_address((struct sockaddr_in *)mgr_host);
	if (my_address == NULL)
	{
		errno = EINVAL;
		/* make it reconnect? */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	args.svc_addr = my_address->sin_addr.s_addr;
	args.svc_prog = prog;
	args.svc_vers = vers;
	args.svc_proto = proto;

	result = capfs_cbreg_1(args, &resp, *clnt);
	if (result != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_cbreg_1 :");
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		errno = convert_to_errno(result);
		return -1;
	}
	else {
		my = inet_ntoa(my_address->sin_addr);
		/* drop it if the cache handle policy says so! */
		put_clnt_handle(clnt, 0);
		if (resp.status.status != 0) {
			errno = resp.status.eno;
			LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "[%s] Could not register prog #: 0x%x, version #: %d, proto: %s -> %s\n",
					my, prog, vers, proto == IPPROTO_TCP ? "tcp" : "udp", strerror(errno));
			return -1;
		}
		my_cb_id = resp.cb_id;
		LOG(stderr, CRITICAL_MSG, SUBSYS_META, "[%s] Successfully registered prog #: %x, version #: %d, proto: %s -> callback id = %d\n",
				my, prog, vers, proto == IPPROTO_TCP ? "tcp" : "udp", my_cb_id);
		return 0;
	}
}

/* 
 * Client-side equivalent of marshalling the legacy pvfs1 protocol 
 * into the capfs RPC layer 
 */
#define MAX_REASONABLE_TRAILER 16384
int encode_compat_req(struct capfs_options* opt, struct sockaddr* mgr, mreq *req, mack *ack, char *buf_p, struct ackdata_c *recv_p)
{
	int err = 0;
   struct passwd *user  = NULL;
   struct group  *group = NULL;

	memset(ack, 0, sizeof(*ack));
	user  = getpwuid(req->uid);
	group = getgrgid(req->gid);
	LOG(stderr,  DEBUG_MSG, SUBSYS_META, "Request: type=[%s] user=[%s] group=[%s]\n",
             (req->type < 0 || req->type > MAX_MGR_REQ) ? "Invalid Request Type" : reqtext[req->type],
             (user==NULL)?"NULL":user->pw_name,
             (group==NULL)?"NULL":group->gr_name);
	if (req->majik_nr != MGR_MAJIK_NR) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META, "encode_compat_req: invalid magic number; ignoring request\n");
		ack->majik_nr   = MGR_MAJIK_NR;
		ack->release_nr = CAPFS_RELEASE_NR;
		ack->type       = req->type;
		ack->status     = -1;
		errno				 = EINVAL;
		ack->eno        = EINVAL;
		ack->dsize      = 0;
		return -1;
	}
	if (req->release_nr != CAPFS_RELEASE_NR) {
		LOG(stderr,CRITICAL_MSG, SUBSYS_META,  "encode_compat_req: release number from client (%d.%d.%d)"
				"does not match release number of this mgr (%d.%d.%d);"
				"returning error\n",
				(req->release_nr / 10000),
			   (req->release_nr / 100) % 10,
			   (req->release_nr % 10),
			   (CAPFS_RELEASE_NR / 10000),
			 	(CAPFS_RELEASE_NR / 100) % 10,
			 	(CAPFS_RELEASE_NR % 10));
		LOG(stderr,CRITICAL_MSG, SUBSYS_META,  "encode_compat_req: an administrator needs to verify"
				"that all CAPFS servers and clients are at the same release number.\n");
		ack->majik_nr   = MGR_MAJIK_NR;
		ack->release_nr = CAPFS_RELEASE_NR;
		ack->type       = req->type;
		ack->status     = -1;
		errno 			 = EINVAL;
		ack->eno        = EINVAL;
		ack->dsize      = 0;
		return -1;
	}
	/*
	 * Remember that meta-server may have crashed. So in such cases, we detect the
	 * failure and before proceeding with any other operation, we re-register
	 * our callback identifier.
	 */
	if (check_for_registration == 1 || my_cb_id < 0) {
		LOG(stderr, INFO_MSG, SUBSYS_META,  "encode_compat_req: negative callback identifier. registering...(%d,%d)\n",
				check_for_registration, my_cb_id);
		/* do the registration ourselves. We will use tcp for callbacks! */
		if (capfs_cbreg(opt, mgr, CAPFS_CAPFSD, clientv1,
						IPPROTO_TCP) < 0) 
		{
			ack->majik_nr   = MGR_MAJIK_NR;
			ack->release_nr = CAPFS_RELEASE_NR;
			ack->type       = req->type;
			ack->status     = -1;
			errno				 = EINVAL;
			ack->eno        = EINVAL;
			ack->dsize      = 0;
			LOG(stderr, CRITICAL_MSG, SUBSYS_META, "Could not register a callback identifier for cache invalidation...\n");
			return -1;
		}
		check_for_registration = 0;
	}
	if ((err=(req->dsize >= MAX_REASONABLE_TRAILER))
			|| req->type < 0 || req->type > MAX_MGR_REQ) { 
		/* this is ridiculous - throw request out */
		if (err) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_META, "encode_compat_req: crazy huge dsize = %d\n;"
					"ignoring request\n", (int) req->dsize);
		}
		else {
			LOG(stderr,  CRITICAL_MSG, SUBSYS_META, "invalid request (%d)", req->type);
		}
		ack->majik_nr   = MGR_MAJIK_NR;
		ack->release_nr = CAPFS_RELEASE_NR;
		ack->type       = req->type;
		ack->status     = -1;
		errno				 = EINVAL;
		ack->eno        = EINVAL;
		ack->dsize      = 0;
		return -1;
	}
	if (req->dsize > 0) { /* make sure that there is trailing data that is NULL terminated */
		if (buf_p == NULL || buf_p[req->dsize] != '\0') {
			LOG(stderr, CRITICAL_MSG, SUBSYS_META, "encode_compat_req  - getting NULL trailing data\n"
					"or non NULL terminated string for type=[%s]; ignoring request\n", reqtext[req->type]);
			if (buf_p) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_META, "encode_compat_req - dsize = [%Ld], trailing data = [%c]\n",
						req->dsize, buf_p[req->dsize]);
			}
			ack->majik_nr   = MGR_MAJIK_NR;
			ack->release_nr = CAPFS_RELEASE_NR;
			ack->type       = req->type;
			ack->status     = -1;
			errno				 = EINVAL;
			ack->eno        = EINVAL;
			ack->dsize      = 0;
			return -1;
		}
		else {
			LOG(stderr, DEBUG_MSG, SUBSYS_META, "Trailing Data=[%s]\n", buf_p);
		}
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "req: %s\n", reqtext[req->type]);
	err = (reqfn[req->type])(opt, mgr, req, buf_p, ack, recv_p); /* handle request */
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "Completed: type=[%s]\n", 
		 (req->type < 0 || req->type > MAX_MGR_REQ) ? "Invalid Request Type" : reqtext[req->type]);
	return err;
}

static int do_noop(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	CLIENT **clnt = NULL;
	struct timeval tv = {25, 0};
	enum clnt_stat ans;
	int tcp;

	init_defaults(ack_p, MGR_NOOP);
	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	/* Null call */
	if ((ans = clnt_call(*clnt, NULLPROC, (xdrproc_t) xdr_void, (caddr_t) NULL, 
				(xdrproc_t) xdr_void, (caddr_t) NULL, tv)) != RPC_SUCCESS) {
		clnt_perror(*clnt, "do_noop: \n");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	ack_p->status = 0;
	ack_p->eno = 0;
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static inline int copy_to_credentials(mreq_p req_p, creds *pcreds)
{
	pcreds->uid = req_p->uid;
	pcreds->gid = req_p->gid;
	return 0;
}

static int do_chmod(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	chmod_args args;
	chmod_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.name = data_p;
	args.mode = req_p->req.chmod.mode;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_chmod_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_chmod_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_chown(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	chown_args args;
	chown_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.name = data_p;
	args.force_group_change = req_p->req.chown.force_group_change;
	args.owner = req_p->req.chown.owner;
	args.group = req_p->req.chown.group;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_chown_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_chown_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_close(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	close_args args;
	close_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	copy_from_fmeta_to_fm(&req_p->req.close.meta, &args.meta);
	/* use what is provided, else default to not using hcache */
	args.use_hcache = (opt ? opt->use_hcache : 0);
	args.cb_id = my_cb_id;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_close_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_close_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_stat(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	stat_args args;
	stat_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.name = data_p;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	if (req_p->type == MGR_LSTAT) {
		ans = capfs_lstat_1(args, &resp, *clnt);
	}
	else /* if (req_p->type == MGR_STAT) */ {
		ans = capfs_stat_1(args, &resp, *clnt);
	}
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_[l]stat_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	copy_from_fm_to_fmeta(&resp.meta, &ack_p->ack.stat.meta);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

#define minimum(a, b) ((a) < (b) ? (a) : (b))

static void convert_from_iods_info(iods_info *iinfo, int *niods, iod_info *pinfo)
{
	int i, exp = *niods; /* number of iods' expected by client */

	if (iinfo->iods_info_len == 0) {
		*niods = 0;
		return;
	}
	if (iinfo->iods_info_len > exp) {
		LOG(stderr, WARNING_MSG, SUBSYS_META,
				"WARNING! convert_from_iods_info obtained more iodinfo's (%d) than what was requested (%d)\n", iinfo->iods_info_len, exp);
	}
	exp = minimum(iinfo->iods_info_len, *niods);
	/* Copy only the relevant socket addresses of the iod servers here */
	for (i = 0; i < exp; i++) {
		memcpy(&pinfo[i].addr, &iinfo->iods_info_val[i].addr, sizeof(s_addr));
		//sockio_dump_sockaddr(&pinfo[i].addr, stderr);
	}
	*niods = exp;
	return;
}

static void copy_resp_to_current_hashes(sha1_hashes *resp_current_hashes, sha1_info *current_hashes)
{
	int i;
	if (resp_current_hashes == NULL || current_hashes == NULL) {
		LOG(stderr, WARNING_MSG, SUBSYS_META, "Cannot copy with NULL pointer\n");
		return;
	}
	assert(current_hashes->sha1_info_len >= resp_current_hashes->sha1_hashes_len);
	current_hashes->sha1_info_len = resp_current_hashes->sha1_hashes_len;
	for (i = 0; i < resp_current_hashes->sha1_hashes_len; i++) {
		memcpy(current_hashes->sha1_info_ptr[i], resp_current_hashes->sha1_hashes_val[i], CAPFS_MAXHASHLENGTH);
	}
	return;
}

static void copy_sha1_hashes(sha1_hashes *h, int64_t *nhashes, char *phashes)
{
	int i;
	int64_t exp = *nhashes;/* Number of hashes expected by client */

	if (phashes == NULL) {
		return;
	}
	if (h->sha1_hashes_len == 0) {
		*nhashes = 0;
		return;
	}
	if (h->sha1_hashes_len > exp) {
		LOG(stderr, WARNING_MSG, SUBSYS_META, "WARNING! copy_sha1_hashes obtained more hashes (%d) than what was requested (%Ld)\n",
				h->sha1_hashes_len, exp);
	}
	exp = minimum(h->sha1_hashes_len, *nhashes);
	for (i = 0; i < exp; i++) {
		memcpy(phashes + i * CAPFS_MAXHASHLENGTH, h->sha1_hashes_val[i], CAPFS_MAXHASHLENGTH);
	}
	*nhashes = exp;
	return;
}

static int hash_ctor(sha1_hashes *h)
{
	h->sha1_hashes_val = (sha1_hash *) calloc(CAPFS_MAXHASHES, sizeof(sha1_hash));
	if (h->sha1_hashes_val == NULL) {
		return -ENOMEM;
	}
	return 0;
}

static void hash_dtor(sha1_hashes *h)
{
	free(h->sha1_hashes_val);
	return;
}

static int iodinfo_ctor(iods_info *info)
{
	info->iods_info_val = (i_info *) calloc(CAPFS_MAXIODS, sizeof(i_info));
	if (info->iods_info_val == NULL) {
		return -ENOMEM;
	}
	return 0;
}

static void iodinfo_dtor(iods_info *info)
{
	free(info->iods_info_val);
	return;
}

static int open_ctor(open_resp *resp)
{
	/* allocate memory for the open's response's fields */
	if (iodinfo_ctor(&resp->info) < 0) {
		return -ENOMEM;
	}
	if (hash_ctor(&resp->h) < 0) {
		iodinfo_dtor(&resp->info);
		return -ENOMEM;
	}
	return 0;
}

static void open_dtor(open_resp *resp)
{
	hash_dtor(&resp->h);
	iodinfo_dtor(&resp->info);
	return;
}

static int do_open(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	open_args args;
	open_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	iod_info *pinfo = (iod_info *) recv_p->u.open.iod;
	int *niods = &recv_p->u.open.niods;
	char *phashes = recv_p->u.open.hashes;
	int64_t *nhashes = &recv_p->u.open.nhashes;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.name = data_p;
	copy_from_fmeta_to_fm(&req_p->req.open.meta, &args.meta);
	args.flag = req_p->req.open.flag;
	args.mode = req_p->req.open.mode;
	/*
	 * If the client daemon indicates a non-zero number of hashes to be fetched at open
	 * time, then we set this flag. Else we indicate that we are not interested..
	 *
	 * a) If opt is non-NULL, then use the opt->use_hcache flag
	 * b) Else check if (*nhashes was != 0), and if so set use_hcache to 1.
	 * c) Else set it 0.
	 */
	args.request_hashes = (opt ? opt->use_hcache : (*nhashes != 0) ? 1 : 0);
	args.cb_id = my_cb_id;

	/* allocate memory for response */
	if (open_ctor(&resp) < 0) {
		errno = ENOMEM;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		open_dtor(&resp);
		return -1;
	}
	ans = capfs_open_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) 
	{
		clnt_perror(*clnt, "capfs_open_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		open_dtor(&resp);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	copy_from_fm_to_fmeta(&resp.meta, &ack_p->ack.open.meta);
	ack_p->ack.open.cap = resp.cap;
	convert_from_iods_info(&resp.info, niods, pinfo);
	/* set ack's dsize here */
	ack_p->dsize = resp.info.iods_info_len * sizeof(iod_info);
	/* also copy the hashes if there are any */
	if (resp.hash_status.status) 
	{
		/* -ve error code */
		*nhashes = -(resp.hash_status.eno);
	}
	else 
	{
		if (*nhashes != 0)
		{
			copy_sha1_hashes(&resp.h, nhashes, phashes);
		}
	}
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	open_dtor(&resp);
	return 0;
}

static int do_unlink(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	unlink_args args;
	unlink_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.name = data_p;

	/* Use what is provided, else default to no hcache coherence */
	args.desire_hcache_coherence = (opt ? opt->desire_hcache_coherence : 0);
	args.cb_id = my_cb_id;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_unlink_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_unlink_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_fstat(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	fstat_args args;
	fstat_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.meta.fs_ino = req_p->req.fstat.meta.fs_ino;
	args.meta.f_ino = req_p->req.fstat.meta.u_stat.st_ino;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_fstat_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_fstat_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	copy_from_fm_to_fmeta(&resp.meta, &ack_p->ack.fstat.meta);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_rename(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	rename_args args;
	rename_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	char *ptr = (char *) data_p;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.oldname = ptr;
	for (;*ptr;ptr++); /* theres got to be a better way */
	args.newname = ++ptr;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_rename_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_rename_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_iod_info(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	iodinfo_args args;
	iodinfo_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	iod_info *pinfo = (iod_info *) recv_p->u.iodinfo.pinfo;
	int tcp, *niods = &recv_p->u.iodinfo.niods;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.name = data_p;
	/* allocate memory for the response */
	if (iodinfo_ctor(&resp.info) < 0) {
		errno = ENOMEM;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		iodinfo_dtor(&resp.info);
		return -1;
	}
	ans = capfs_iodinfo_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_iodinfo_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		iodinfo_dtor(&resp.info);
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	/* set ack's dsize */
	ack_p->dsize = resp.info.iods_info_len * sizeof(iod_info);
	ack_p->ack.iod_info.nr_iods = resp.info.iods_info_len;
	convert_from_iods_info(&resp.info, niods, pinfo);
	iodinfo_dtor(&resp.info);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_mkdir(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	mkdir_args args;
	mkdir_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.name = data_p;
	args.mode = req_p->req.mkdir.mode;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_mkdir_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_mkdir_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_fchown(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	fchown_args args;
	fchown_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.fhandle.fs_ino = req_p->req.fchown.fs_ino;
	args.fhandle.f_ino  = req_p->req.fchown.file_ino;
	args.owner  = req_p->req.fchown.owner;
	args.group  = req_p->req.fchown.group;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_fchown_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_fchown_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_fchmod(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	fchmod_args args;
	fchmod_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.fhandle.fs_ino = req_p->req.fchmod.fs_ino;
	args.fhandle.f_ino = req_p->req.fchmod.file_ino;
	args.mode = req_p->req.fchmod.mode;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_fchmod_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_fchmod_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_rmdir(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	rmdir_args args;
	rmdir_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.name = data_p;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_rmdir_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_rmdir_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_access(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	access_args args;
	access_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.name = data_p;
	args.mode = req_p->req.access.mode;
	args.to_follow = req_p->req.access.to_follow;
	
	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_access_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_access_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	copy_from_fm_to_fmeta(&resp.meta, &ack_p->ack.access.meta);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_truncate(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	truncate_args args;
	truncate_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.name = data_p;
	args.length = req_p->req.truncate.length;
	/* Use what is provided, else default to no hcache coherence */
	args.desire_hcache_coherence = (opt ? opt->desire_hcache_coherence : 0);
	args.cb_id = my_cb_id;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_truncate_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_truncate_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_utime(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	utime_args args;
	utime_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.name = data_p;
	args.actime = req_p->req.utime.actime;
	args.modtime = req_p->req.utime.modtime;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_utime_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_utime_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static inline int dentries_ctor(dentries *ent)
{
	int i, j;
	
	ent->dentries_val = (dentry *) calloc(CAPFS_MAXDENTRY, sizeof(dentry));
	if (ent->dentries_val == NULL) {
		return -ENOMEM;
	}
	for (i = 0; i < CAPFS_MAXDENTRY; i++) {
		ent->dentries_val[i].entry = (filename) calloc(CAPFS_MAXNAMELEN, sizeof(char));
		if (ent->dentries_val[i].entry == NULL) {
			break;
		}
	}
	if (i < CAPFS_MAXDENTRY) {
		for (j = 0; j < i; j++) {
			free(ent->dentries_val[j].entry);
		}
		free(ent->dentries_val);
		return -ENOMEM;
	}
	return 0;
}

static inline void dentries_dtor(dentries *ent)
{
	int i;

	for (i = 0; i < CAPFS_MAXDENTRY; i++) {
		if (ent->dentries_val[i].entry) {
			free(ent->dentries_val[i].entry);
		}
	}
	free(ent->dentries_val);
	return;
}

static inline void copy_to_capfsdirent(dentries *pent, int *nentries, struct capfs_dirent *pdir)
{
	int i, exp = *nentries;

	if (pent->dentries_len == 0) {
		*nentries = 0;
		return;
	}
	if (pent->dentries_len > exp) {
		LOG(stderr, WARNING_MSG, SUBSYS_META, "WARNING! copy_to_capfsdirent: obtained more dentries (%d) than what was requested (%d)\n",
				pent->dentries_len, exp);
	}
	exp = minimum(pent->dentries_len, *nentries);
	for (i = 0; i < exp; i++) {
		pdir[i].handle = pent->dentries_val[i].handle;
		pdir[i].off    = pent->dentries_val[i].off;
		strcpy(pdir[i].name, pent->dentries_val[i].entry);
	}
	*nentries = exp;
	return;
}

static int do_getdents(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	getdents_args args;
	getdents_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	struct capfs_dirent *pdir = (struct capfs_dirent *) recv_p->u.getdents.pdir;
	int *nentries = &recv_p->u.getdents.nentries;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.name = data_p;
	args.offset = req_p->req.getdents.offset;
	args.length = req_p->req.getdents.length;
	/* allocate memory for the responses */
	if (dentries_ctor(&resp.entries) < 0) {
		errno = ENOMEM;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		dentries_dtor(&resp.entries);
		return -1;
	}
	ans = capfs_getdents_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_getdents_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		dentries_dtor(&resp.entries);
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	ack_p->dsize = resp.entries.dentries_len * sizeof(struct capfs_dirent);
	ack_p->ack.getdents.offset = resp.offset;
	copy_to_capfsdirent(&resp.entries, nentries, pdir);
	dentries_dtor(&resp.entries);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_statfs(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	statfs_args args;
	statfs_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.name = data_p;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_statfs_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_statfs_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	ack_p->ack.statfs.tot_bytes = resp.tot_bytes;
	ack_p->ack.statfs.free_bytes = resp.free_bytes;
	ack_p->ack.statfs.tot_files = resp.tot_files;
	ack_p->ack.statfs.free_files = resp.free_files;
	ack_p->ack.statfs.namelen = resp.namelen;
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_lookup(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	lookup_args args;
	lookup_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.name = data_p;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_lookup_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_lookup_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	copy_from_fm_to_fmeta(&resp.meta, &ack_p->ack.lookup.meta);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_ctime(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	ctime_args args;
	ctime_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.name = data_p;
	args.createtime = req_p->req.ctime.createtime;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_ctime_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_ctime_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int do_link(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	link_args args;
	link_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	char *ptr = (char *) data_p;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.link_name = ptr;
	for (;*ptr;ptr++); /* theres got to be a better way */
	args.target_name = ++ptr;
	args.soft = req_p->req.link.soft;
	copy_from_fmeta_to_fm(&req_p->req.link.meta, &args.meta);

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	ans = capfs_link_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_link_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}

static int readlink_ctor(readlink_resp *resp)
{
	resp->link_name = (char *) calloc(CAPFS_MAXNAMELEN, 1);
	if (resp->link_name == NULL) {
		return -ENOMEM;
	}
	return 0;
}

static void readlink_dtor(readlink_resp *resp)
{
	free(resp->link_name);
	return;
}

static void copy_link_name(readlink_resp *resp, int *link_len, char *link_name)
{
	int exp = *link_len, act = strlen(resp->link_name);

	if (act == 0) {
		*link_len = 0;
		return;
	}
	if (act > exp) {
		LOG(stderr, WARNING_MSG, SUBSYS_META, "WARNING! copy_link_name obtained more chars (%d) than expected (%d)\n",
				act, exp);
	}
	exp = minimum(act, *link_len);
	strncpy(link_name, resp->link_name, exp);
	*link_len = exp;
	return;
}

static int do_readlink(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	readlink_args args;
	readlink_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	char *link_name = (char *) recv_p->u.readlink.link_name;
	int  *link_len  = &recv_p->u.readlink.link_len;
	int tcp;

	/* Use what is provided, else default to tcp */
	tcp = (opt ? opt->tcp : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	init_defaults(ack_p, req_p->type);
	copy_to_credentials(req_p, &args.credentials);
	args.link_name = data_p;
	/* Allocate memory for the response */
	if (readlink_ctor(&resp) < 0) {
		errno = ENOMEM;
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		ack_p->status = -1;
		ack_p->eno = errno;
		readlink_dtor(&resp);
		return -1;
	}
	ans = capfs_readlink_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(*clnt, "capfs_readlink_1:");
		errno = convert_to_errno(ans);
		ack_p->status = -1;
		ack_p->eno = errno;
		readlink_dtor(&resp);
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	init_ackstatus(ack_p, &resp.status);
	/* set the dsize field in the ack */
	ack_p->dsize = strlen(resp.link_name);
	copy_link_name(&resp, link_len, link_name);
	readlink_dtor(&resp);
	/* drop it if the cache handle policy says so! */
	put_clnt_handle(clnt, 0);
	return 0;
}


/* library way of getting hashes */
static int do_gethashes(struct capfs_options* opt, struct sockaddr *mgr, mreq_p req_p, void *data_p, mack_p ack_p, struct ackdata_c *recv_p)
{
	int64_t ret;

	init_defaults(ack_p, req_p->type);
	/* No hcache prefetches for the library */
	ret = get_hashes(opt->use_hcache, data_p, req_p->req.gethashes.begin_chunk, 
		req_p->req.gethashes.nchunks, -1, recv_p->u.gethashes.buf, NULL);
	if (ret < 0) {
		ack_p->status = -1;
		ack_p->eno = errno;
		return -1;
	}
	recv_p->u.gethashes.nhashes = ret;
	return 0;
}

/* 
 * Copied the functions from capfsd source here, since file names in the hcache
 * have the full address/port information of the manager etc/
 */
static char *skip_to_filename(char *name)
{
	while (*(++name) != '/' && *name != '\0');

	return name;
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
	len = ((end - name) < 1024) ? (end - name) : 1023;
	strncpy(host, name, len);
	host[len] = '\0';
}

static int get_mgr_addr(capfs_char_t host[], uint16_t port, struct sockaddr *sp)
{
	if (init_sock(sp, host, port) < 0) 
		return -1;
	return 0;
}

enum {H_READ = 0, H_WRITE = 1};

struct user_ptr 
{
	/* Note that names have to be of the form hostname:<port>/filename */
	struct handle*    p;
	int 				mode;
	int				nframes;
	char 			  **buffers;
	size_t		  *sizes;
	int64_t		  *offsets;
	int			  *completed;
};

/*
* NOTE that we dont free the ->completed integer pointer
* since that is returned to the cache manager
*/
static void dealloc_user_ptr(struct user_ptr *ptr)
{
	if (ptr)
	{
		free(ptr);
	}
}

static struct user_ptr* alloc_user_ptr(void* p, int mode, int nframes,
		char **buffers, size_t *sizes, int64_t *offsets)
{
		struct user_ptr *ptr = NULL;

		ptr = (struct user_ptr *) calloc(1, sizeof(struct user_ptr));
		if (ptr)
		{
			ptr->p = p;
			ptr->mode = mode;
			ptr->nframes = nframes;
			ptr->buffers = buffers;
			ptr->sizes = sizes;
			ptr->offsets = offsets;
			/* We try to allocate this at the last, so 
			 * that if we had to call dealloc_user_ptr()
			 * we still would not have to free this!
			 */
			ptr->completed = (int *) 
				calloc(nframes, sizeof(int));
			if (!ptr->completed)
			{
				goto error_exit;
			}
		}
		return ptr;
error_exit:
		dealloc_user_ptr(ptr);
		return NULL;
}

static struct user_ptr* 
post_io(void* p, int nframes, char **buffers,
		size_t *sizes, int64_t *offsets, int mode, int *error)
{
		struct user_ptr *uptr = NULL;

		/* try to allocate the user ptr to keep track of the state */
		uptr = alloc_user_ptr(p, mode, nframes, buffers, sizes, offsets);
		if (!uptr)
		{
			*error = -ENOMEM;
			return NULL;
		}
		return uptr;
}

/*
 * RPC call to meta-data server to fetch the hashes
 * for this file.
 */
static int fetch_hashes(int tcp, struct sockaddr* mgr, gethashes_args *args,
		gethashes_resp *resp)
{
	enum clnt_stat ans;
	CLIENT **clnt = NULL;

	clnt = get_clnt_handle(tcp, (struct sockaddr_in *)mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		return -1;
	}
	ans = capfs_gethashes_1(*args, resp, *clnt);
	if (ans != RPC_SUCCESS) {
		errno = convert_to_errno(ans);
		clnt_perror(*clnt, "capfs_gethashes_1 :");
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	/* drop the handle according to the cache handle policy */
	put_clnt_handle(clnt, 0);
	return 0;
}

/*
 * this is the hcache callback routine that will invoke the fetch hashes
 * RPC call to actually do the real job of fetching the hashes
 * from the meta-data server.
 */
static void do_fetch_hashes(struct user_ptr *uptr)
{
	char *name;
	gethashes_args args;
	gethashes_resp resp;
	int64_t nchunks = 0, i;
	char host[1024];
	struct sockaddr mgr;

	memset(&args, 0, sizeof(gethashes_args));
	memset(&resp, 0, sizeof(gethashes_resp));
	name = ((struct handle *) uptr->p)->name;
	hostcpy(host, name);

	if (get_mgr_addr(host, 0, &mgr) < 0) {
		errno = ENXIO;
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "Could not fetch hashes: Invalid host/name? %s: %s\n", name, strerror(errno));
		for (i = 0; i < uptr->nframes; i++) {
			uptr->completed[i] = -errno;
		}
		return;
	}
	/* Prepare request for the hashes */
	args.type.type = HASHBYNAME;
	args.cb_id = my_cb_id;
	/* server understands only the last part */
	args.type.hbytype_u.name = skip_to_filename(name);
	args.begin_chunk = uptr->offsets[0]/uptr->sizes[0];
	args.nchunks = nchunks = uptr->nframes;
	/*
	 * FIXME: We have to break things up if nchunks is > CAPFS_MAXHASHES 
	 * This routine assumes incorrectly that the caller initiated a fetch
	 * for consecutive hashes of the same file. This means that we will die
	 * a horrible death with hcache_clear_range(), I think.
	 * There are 2 possibilities to solve this issue
	 * a) Use only hcache_clear() instead of hcache_clear_range() but that would
	 * be way too bad on performance, I think...
	 * b) Make the underlying cmgr code aware of non-contiguity or lack of RPC
	 * support for non-contiguous requests and ask it to just refetch everything...
	 *
	 * Currently, we are going for the latter option.
	 */
	if (nchunks > CAPFS_MAXHASHES) {
		errno = EINVAL;
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "Panic! Unimplemented case! Nchunk (%Ld) is > than CAPFS_MAXHASHES (%d)\n",
				nchunks, CAPFS_MAXHASHES);
		for (i = 0; i < uptr->nframes; i++) {
			uptr->completed[i] = -errno;
		}
		return;
	}
	if (hash_ctor(&resp.h) < 0) {
		errno = ENOMEM;
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "Could not allocate memory\n");
		for (i = 0; i < uptr->nframes; i++) {
			uptr->completed[i] = -errno;
		}
		return;
	}
	
	LOG(stderr, DEBUG_MSG, SUBSYS_META,  "RPC fetch hashes for file %s, begin_chunk: %Ld, nchunks: %Ld\n",
			skip_to_filename(name), args.begin_chunk, args.nchunks);

	if (fetch_hashes(1, &mgr, &args, &resp) < 0) {
		for (i = 0; i < uptr->nframes; i++) {
			uptr->completed[i] = -errno;
		}
		hash_dtor(&resp.h);
		return;
	}
	/* copy the responses to the buffers if the operation was a success. else set the error appropriately */
	if (resp.status.status == 0) {
		/* sanity of parameters */
		if (resp.h.sha1_hashes_len < 0 || resp.h.sha1_hashes_len > uptr->nframes) {
			errno = EINVAL;
			LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "insanity: get_hashes fetched more hashes "
					"(%d) than requested (%d)\n", resp.h.sha1_hashes_len, uptr->nframes);
			for (i = 0; i < uptr->nframes; i++) {
				uptr->completed[i] = -errno;
			}
			hash_dtor(&resp.h);
			return;
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "RPC yielded %d hashes out of %d\n", 
				resp.h.sha1_hashes_len, uptr->nframes);
		/* copy the data from the response. */
		for (i = 0; i < resp.h.sha1_hashes_len; i++) {
			memcpy(uptr->buffers[i], resp.h.sha1_hashes_val[i], CAPFS_MAXHASHLENGTH);
			uptr->completed[i] = CAPFS_MAXHASHLENGTH;
		}
		/* Indicate that the rest did not exist */
		for (i = resp.h.sha1_hashes_len; i < uptr->nframes; i++) {
			uptr->completed[i] = 0;
			memset(uptr->buffers[i], 0, CAPFS_MAXHASHLENGTH);
		}
	}
	/* operation was an error.. */
	else {
		for (i = 0; i < uptr->nframes; i++) {
			uptr->completed[i] = -resp.status.eno;
		}
	}
	hash_dtor(&resp.h);
	return;
}

/*
 * readpage_begin() routine must return an opaque handle
 * on success and negative error code on failure.
 * This routine is invoked only on a cache miss.
 * Use this to just setup information for computing hashes if need be
 */ 
static long hash_buffered_read_begin(void* p, 
		int number, cm_buffer_t *buffers, size_t *sizes, int64_t *offsets)
{
		int ret;
		struct user_ptr *uptr = NULL;

		/* Actually the bulk of the work is done only at the time of the complete routine */
		if ((uptr = post_io(p, number, (char **) buffers,
						sizes, offsets, H_READ, &ret)) == NULL)
		{
			panic("hash_buffered_read: could not post read %d\n", ret);
			return ret;
		}
		return (long) uptr;
}

/*
 * Complete the I/O operation that was posted asynchronously
 * earlier. We return a pointer to an array of integers
 * that indicate the error codes in case of failed I/O
 * or amount of I/O completed.
 * Callers responsibility to free it.
 */
static int* hash_buffered_read_complete(long _uptr)
{
		struct user_ptr *uptr = NULL;
		int *completed = NULL;

		uptr = (struct user_ptr *) _uptr;
		completed = (uptr->completed);
		/*
		 * this is an RPC call, 
		 */
		do_fetch_hashes(uptr);
		/* Deallocate the user pointer */
		dealloc_user_ptr(uptr);
		return completed;
}

/*
 * writepage_begin() routine must return an opaque handle
 * on success and negative error code on failure.
 * This routine is invoked only on a cache miss.
 * There is no need to WB crypto-hashes!!
 * So these will be simple no-ops.
 */ 
static long hash_buffered_write_begin(cm_handle_t p, 
		int number, cm_buffer_t *buffers, size_t *sizes, int64_t *offsets)
{
		int ret;
		struct user_ptr *uptr = NULL;

		if ((uptr = post_io(p, number, (char **) buffers,
						sizes, offsets, H_WRITE, &ret)) == NULL)
		{
			panic("hash_buffered_write: could not post write %d\n", ret);
			return ret;
		}
		return (long) uptr;
}

/*
 * We don't have to writeback the hashes from the cache.
 */
static int* hash_buffered_write_complete(long _uptr)
{
		struct user_ptr *uptr = NULL;
		int *completed = NULL, i;

		uptr = (struct user_ptr *) _uptr;
		completed = (uptr->completed);
		for (i = 0; i < uptr->nframes; i++) {
			completed[i] = uptr->sizes[i];
		}
		/* Deallocate the user pointer */
		dealloc_user_ptr(uptr);
		return completed;
}

static struct hcache_options opt = {
	organization: CAPFS_HCACHE_SIMPLE,
	hr_begin: hash_buffered_read_begin,
	hr_complete: hash_buffered_read_complete,
	hw_begin: hash_buffered_write_begin,
	hw_complete: hash_buffered_write_complete,
};

int init_hashes(void)
{
	/* initialize the hash cache with these callbacks */
	hcache_init(&opt);
	return 0;
}

void cleanup_hashes(void)
{
	hcache_finalize();
	return;
}

/* 
 * For a given file name (NOTE: that file names need to have the 
 * entire address of the manager embedded),
 * Try to get the specified number of hashes into the specified buffer,
 * Returns the actual number of hashes that could be retrieved successfully.
 */
int64_t get_hashes(int use_hcache, char *name, int64_t begin_chunk, 
		int64_t nchunks, int64_t prefetch_index, void *buf, fmeta *meta)
{
	/* 
	 * This is the place, where we can choose to disable the hcache by not
	 * accessing it at all. Instead every call should be made out to the
	 * meta-data server. I dont know if this is a such good thing for 
	 * performance
	 */
	if (use_hcache == 1) 
	{
		int64_t ret;
		if ((ret = hcache_get(name, begin_chunk, nchunks, prefetch_index, buf)) < 0) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "get_hashes: could not get hashes: %s\n", strerror(errno));
			return -1;
		}
		/* ret is in number of bytes */
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "get_hashes: hcache_get returned %lld\n", ret);
		fflush(stdout);
		/* number of bytes / sizeof(sha1-hash) */
		return (ret / CAPFS_MAXHASHLENGTH);
	}
	else {
		/* Issue an RPC to fetch the hashes for the file */
		gethashes_args args;
		gethashes_resp resp;
		char host[256];
		struct sockaddr mgr;
		static char mgr_host[]="xxx.xxx.xxx.xxx\0";
		unsigned char *uc = (unsigned char *)&(((struct sockaddr_in *)&mgr)->sin_addr);

		memset(&args, 0, sizeof(gethashes_args));
		memset(&resp, 0, sizeof(gethashes_resp));
		hostcpy(host, name);

		if (nchunks == 0) {
			errno = EINVAL;
			return -1;
		}
		if (get_mgr_addr(host, 0, &mgr) < 0) {
			errno = ENXIO;
			LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "Could not fetch hashes: Invalid host/name? %s: %s\n", name, strerror(errno));
			return -1;
		}
		sprintf(mgr_host, "%d.%d.%d.%d", uc[0], uc[1], uc[2], uc[3]);
		/* Prepare request for the hashes */
		args.type.type = HASHBYNAME;
		args.cb_id = my_cb_id;
		/* server understands only the last part */
		args.type.hbytype_u.name = skip_to_filename(name);
		args.begin_chunk = begin_chunk;
		args.nchunks = nchunks;

		LOG(stderr, DEBUG_MSG, SUBSYS_META, "Issuing a fetch hashes for file %s, begin_chunk: %Ld "
				"nchunks: %Ld from host %s\n", skip_to_filename(name), args.begin_chunk, 
				args.nchunks, host);
		/*
		 * FIXME: We have to break things up if nchunks is > CAPFS_MAXHASHES 
		 */
		if (nchunks > CAPFS_MAXHASHES) {
			errno = EINVAL;
			LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "Panic! Unimplemented case! Nchunk (%Ld) is > than CAPFS_MAXHASHES (%d)\n",
					nchunks, CAPFS_MAXHASHES);
			return -1;
		}
		if (hash_ctor(&resp.h) < 0) {
			errno = ENOMEM;
			LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "Could not allocate memory\n");
			return -1;
		}

		if (fetch_hashes(1, &mgr, &args, &resp) < 0) {
			hash_dtor(&resp.h);
			return -1;
		}
		/* copy the responses to the buffers if the operation was a success. else set the error appropriately */
		if (resp.status.status == 0) {
			int i;
			/* sanity of parameters */
			if (resp.h.sha1_hashes_len < 0 || resp.h.sha1_hashes_len > nchunks) {
				errno = EINVAL;
				LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "insanity: get_hashes fetched more hashes "
						"(%d) than requested (%Ld)\n", resp.h.sha1_hashes_len, nchunks);
				hash_dtor(&resp.h);
				return -1;
			}
			LOG(stderr, DEBUG_MSG, SUBSYS_META, "meta-data server returned %d hashes\n", resp.h.sha1_hashes_len);
			/* copy the data from the response. */
			for (i = 0; i < resp.h.sha1_hashes_len; i++) {
				memcpy(buf + i * CAPFS_MAXHASHLENGTH, resp.h.sha1_hashes_val[i], CAPFS_MAXHASHLENGTH);
			}
			hash_dtor(&resp.h);
			/* also copy the meta data structure */
			if (meta) {
				copy_from_fm_to_fmeta(&resp.meta, meta);
			}
			return resp.h.sha1_hashes_len;
		}
		else {
			errno = resp.status.eno;
			hash_dtor(&resp.h);
			return -1;
		}
	}
}

/* 
 * For a given file name (NOTE: that file names need to have the 
 * entire address of the manager embedded),
 * Try to put the specified number of hashes into the specified buffer,
 * This will most likely be called by the UPDATE method of
 * the capfsd's local RPC service or after a write has just committed.
 */
int64_t put_hashes(char *name, int64_t begin_chunk, int64_t nchunks, void *buf)
{
	int64_t ret;
	
	if ((ret = hcache_put(name, begin_chunk, nchunks, buf)) < 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "put_hashes: could not put hashes: %s\n", strerror(errno));
		return -1;
	}
	return ret;
}

/*
 * Analogous routine for the INVALIDATE method of the capfsd's local RPC service.
 */
int clear_hashes(char *name)
{
	return hcache_clear(name);
}


void hashes_stats(int64_t *hits, int64_t *misses, int64_t *fetches, int64_t *invalidates, int64_t *evicts)
{
	cmgr_stats_t stats;

	/* internally reset the hcache stats */
	hcache_get_stats(&stats, 1);
	*hits = stats.hits;
	*misses = stats.misses;
	*fetches = stats.fetches;
	*invalidates = stats.invalidates;
	*evicts = stats.evicts;
	return;
}

static int wcommit_ctor(wcommit_args *args, sha1_info *old_hashes, sha1_info *new_hashes)
{
	int i;

	args->old_hashes.sha1_hashes_len =
		(old_hashes->sha1_info_len > 0) ? old_hashes->sha1_info_len : 0;
	if (args->old_hashes.sha1_hashes_len > 0) 
	{
		args->old_hashes.sha1_hashes_val = 
			(sha1_hash *) calloc(args->old_hashes.sha1_hashes_len, sizeof(sha1_hash));
		if (args->old_hashes.sha1_hashes_val == NULL) {
			return -ENOMEM;
		}
		for (i = 0; i < args->old_hashes.sha1_hashes_len; i++) {
			memcpy(args->old_hashes.sha1_hashes_val[i], old_hashes->sha1_info_ptr[i], CAPFS_MAXHASHLENGTH);
		}
	}
	args->new_hashes.sha1_hashes_len =
		(new_hashes->sha1_info_len > 0) ? new_hashes->sha1_info_len : 0;
	if (args->new_hashes.sha1_hashes_len > 0) 
	{
		args->new_hashes.sha1_hashes_val = 
			(sha1_hash *) calloc(args->new_hashes.sha1_hashes_len, sizeof(sha1_hash));
		if (args->new_hashes.sha1_hashes_val == NULL) 
		{
			free(args->old_hashes.sha1_hashes_val);
			args->old_hashes.sha1_hashes_val = NULL;
			return -ENOMEM;
		}
		for (i = 0; i < args->new_hashes.sha1_hashes_len; i++) 
		{
			memcpy(args->new_hashes.sha1_hashes_val[i], new_hashes->sha1_info_ptr[i], CAPFS_MAXHASHLENGTH);
		}
	}
	return 0;
}

static void wcommit_dtor(wcommit_args *args)
{
	if (args->new_hashes.sha1_hashes_val) {
		free(args->new_hashes.sha1_hashes_val);
	}
	if (args->old_hashes.sha1_hashes_val) {
		free(args->old_hashes.sha1_hashes_val);
	}
	return;
}

/*
 * Committing the hashes on a
 * write to the file. Remember that
 * fname is also the long version that includes the machine name,
 * port number etc...
 */
int commit_write(struct capfs_options *opt, char *fname, int64_t begin_chunk, int64_t write_size, 
		sha1_info *old_hashes, sha1_info *new_hashes, sha1_info *current_hashes)
{
	wcommit_args args;
	wcommit_resp resp;
	CLIENT **clnt = NULL;
	enum clnt_stat ans;
	struct sockaddr mgr;
	char host[1024];
	int desire_hcache_coherence, use_tcp, force_commit;

	/* Use what is provided, else default to no hcache */
	if (opt && opt->use_hcache == 1)
	{
		desire_hcache_coherence = (opt ? opt->desire_hcache_coherence : 0);
	}
	else
	{
		desire_hcache_coherence = 0;
	}
	/* Use what is provided, else default to tcp */
	use_tcp    = (opt ? opt->tcp        : 1);
	/* Use what is provided, else default to force commit */
	force_commit = (opt ? opt->force_commit : 1);
	memset(&args, 0, sizeof(args));
	memset(&resp, 0, sizeof(resp));
	hostcpy(host, fname);
	if (get_mgr_addr(host, 0, &mgr) < 0) {
		errno = ENXIO;
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "Could not commit write: Invalid host/name?"
				"%s: %s\n", fname, strerror(errno));
		return -1;
	}
	clnt = get_clnt_handle(use_tcp, (struct sockaddr_in *)&mgr);
	if (*clnt == NULL) {
		errno = ECONNREFUSED;
		return -1;
	}
	/* initialize arguments to the committer */
	args.type.type = HASHBYNAME;
	args.type.hbytype_u.name = skip_to_filename(fname);
	args.begin_chunk = begin_chunk;
	args.write_size = write_size;
	args.desire_hcache_coherence = desire_hcache_coherence;
	args.cb_id = my_cb_id;
	args.force_wcommit = force_commit;

	/* Construct the RPC arguments */
	if (wcommit_ctor(&args, old_hashes, new_hashes) < 0) {
		errno = ENOMEM;
		put_clnt_handle(clnt, 0);
		return -1;
	}
	/* allocate memory for the response */
	if (hash_ctor(&resp.current_hashes) < 0) {
		errno = ENOMEM;
		wcommit_dtor(&args);
		put_clnt_handle(clnt, 0);
		return -1;
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "commit hashes for file %s, begin_chunk: %Ld, write_size: %Ld\n",
			skip_to_filename(fname), args.begin_chunk, args.write_size);

	ans = capfs_wcommit_1(args, &resp, *clnt);
	if (ans != RPC_SUCCESS) {
		errno = convert_to_errno(ans);
		clnt_perror(*clnt, "capfs_wcommit_1 :");
		wcommit_dtor(&args);
		hash_dtor(&resp.current_hashes);
		/* make it reconnect */
		put_clnt_handle(clnt, 1);
		return -1;
	}
	/* drop the handle according to the cache policy */
	put_clnt_handle(clnt, 0);
	wcommit_dtor(&args);
	/* Right now, I am not using this to retry the whole operation */
	if (resp.status.status) {
		errno = resp.status.eno;
	}
	/* copy the response from the server i.e. the current hashes to the caller regardless of success/failure of operation */
	copy_resp_to_current_hashes(&resp.current_hashes, current_hashes);
	hash_dtor(&resp.current_hashes);
	return resp.status.status;
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * vim: ts=3
 * End:
 */ 
