/*
 * Copyright 2005 (C) Murali Vilayannur
 * vilayann@cse.psu.edu
 * Try to get a callback identifier for a particular client host.
 * 
 */
#include "capfs-header.h"
#include "mgr_prot.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <rpc/rpc.h>
#include <rpc/clnt.h>
#include "capfs_config.h"
#include "log.h"
#include "bit.h"
#include "rpcutils.h"
#include "capfsd_prot.h"

enum {USED = 1, UNUSED = 0};

#define MAXCBS 1024

static pthread_mutex_t cb_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct cb_entry cb_entry;

/* callback entry used by meta-server to invalidate capfsd hcaches! */
struct cb_entry {
	int used;
	cb_args cb;
	struct sockaddr_in addr;
	CLIENT *clnt;
	struct sockaddr_in our_addr;
};

static cb_entry cb_table[MAXCBS];
static int cb_count = 0;

/*
 * This variable determines whether or not CLIENT handles to client-nodes for callbacks
 * need to be cached or not.
 * i.e if it is set to 1, we cache the handle, 
 * else we re-connect everytime if we set it to 0.
 * Since the correctness of execution depends heavily on this callback mechanism,
 * we must ensure that it just works. Having the server store state such as cached
 * CLIENT handles is *never* a good idea.
 * So we would rather disable CLIENT callback handles caching and let the system
 * reconnect each time. It is okay, since we expect this code path to be traversed
 * less often for high-performance conscious apps.
 */
static int cache_handle_policy = CAPFS_CALLBACK_CACHE_HANDLES;

static int find_cbid_of_host(struct sockaddr_in *raddr)
{
	int i, j;

	i = 0;
	j = 0;
	while (i < cb_count && j < MAXCBS) {
		if (cb_table[j].used == USED) {
			/* Matching only the host addresses */
			if (cb_table[j].addr.sin_addr.s_addr == raddr->sin_addr.s_addr) {
				return j;
			}
			i++;
		}
		j++;
	}
	return -1;
}

static int find_unused_cbid(void)
{
	int i;

	i = 0;
	while (i < MAXCBS) {
		if (cb_table[i].used != USED) {
			return i;
		}
		i++;
	}
	return -1;
}

int cbreg_svc(cb_args *cb, struct sockaddr_in *raddr)
{
	int cbid;

	pthread_mutex_lock(&cb_mutex);
	if ((cbid = find_cbid_of_host(raddr)) < 0) {
		cbid = find_unused_cbid();
		cb_table[cbid].used = USED;
		cb_table[cbid].clnt = NULL;
		cb_count++;
	}
	if (cbid >= 0) 
	{
		char *cb_host = NULL;
		cb_table[cbid].cb.svc_prog = cb->svc_prog;
		cb_table[cbid].cb.svc_vers = cb->svc_vers;
		cb_table[cbid].cb.svc_proto = cb->svc_proto;
		memcpy(&cb_table[cbid].addr, raddr, sizeof(struct sockaddr_in));
		cb_host = inet_ntoa(raddr->sin_addr);
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "Registered host %s -> callback id %d\n", cb_host, cbid);
	}
	pthread_mutex_unlock(&cb_mutex);
	return cbid;
}

CLIENT** get_cb_handle(int cb_id)
{
	CLIENT *clnt = NULL, **pclnt = NULL;
	static struct timeval new_cb_timeout = {CB_CLNT_TIMEOUT, 0};

	if (cb_id < 0 || cb_id >= MAXCBS)
	{
		return NULL;
	}
	if ((clnt = cb_table[cb_id].clnt) == NULL)
	{
		char *cb_host = NULL;
		cb_host = inet_ntoa(cb_table[cb_id].addr.sin_addr);
		/*
		 * cb_table[cb_id].clnt = clnt = clnt_create(cb_host, cb_table[cb_id].cb.svc_prog, 
		 *		cb_table[cb_id].cb.svc_vers, 
		 *		(cb_table[cb_id].cb.svc_proto == IPPROTO_TCP) ? "tcp" : "udp"); 
		 */
		cb_table[cb_id].clnt = clnt = get_svc_handle(cb_host, cb_table[cb_id].cb.svc_prog,
					cb_table[cb_id].cb.svc_vers, cb_table[cb_id].cb.svc_proto, CB_CLNT_TIMEOUT, 
					(struct sockaddr *)&cb_table[cb_id].our_addr);
		if (clnt == NULL)
		{
			clnt_pcreateerror(cb_host);
		}
		/* else set the timeout to a fairly large value... */
		else {
			if (clnt_control(clnt, CLSET_TIMEOUT, (char *)&new_cb_timeout) == 0)
			{
				LOG(stderr, WARNING_MSG, SUBSYS_META, "Cannot reset timeout.. continuing with 25 second timeout\n");
			}
		}
	}
	pclnt = &cb_table[cb_id].clnt;
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "MGR address to clients is %s\n", inet_ntoa(cb_table[cb_id].our_addr.sin_addr));
	return pclnt;
}

void put_cb_handle(CLIENT **pclnt, int force_put)
{
	if (pclnt == NULL || *pclnt == NULL)
	{
		return;
	}
	if (force_put)
	{
		clnt_destroy(*pclnt);
		/* make it reconnect next time around */
		*pclnt = NULL;
		return;
	}
	if (cache_handle_policy == 0)
	{
		clnt_destroy(*pclnt);
		/* make it reconnect next time around */
		*pclnt = NULL;
		return;
	}
	return;
}

/*
 * constructor and destructor for the arguments to the update rpc routines.
 */
static int upd_ctor(upd_args *upd, int64_t nchunks, char *phashes)
{
	/* This is going to be a problem at sometime... */
	upd->hashes.sha1_list_len = (int) nchunks;
	upd->hashes.sha1_list_val = (_sha1 *) calloc(upd->hashes.sha1_list_len, sizeof(_sha1));
	if (upd->hashes.sha1_list_val == NULL)
	{
		return -ENOMEM;
	}
	else
	{
		int i;
		for (i = 0; i < upd->hashes.sha1_list_len; i++)
		{
			memcpy(upd->hashes.sha1_list_val[i].h, phashes + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH);
		}
		return 0;
	}
}

static void upd_dtor(upd_args *upd)
{
	free(upd->hashes.sha1_list_val);
	return;
}

/*
 * As the name implies, this routine
 * tries to update the entire sharer set's hcache for a particular
 * file for a specific set of chunks.
 * We do not resort to this routine, unless we know for sure
 * that there is *exactly* 1 sharer.
 */
void cb_update_hashes(char *fname, int cb_id, int64_t begin_chunk, int64_t nchunks, char *phashes)
{
	CLIENT **pclnt = NULL;
	if (fname == NULL)
	{
		return;
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "cb_update_hashes called to update cb_id %d for %s from %Ld for %Ld chunks\n",
			cb_id, fname, begin_chunk, nchunks);
	pclnt = get_cb_handle(cb_id);
	if (*pclnt == NULL)
	{
		LOG(stderr, WARNING_MSG, SUBSYS_META, "cb_update_hashes: connection refused\n");
		errno = ECONNREFUSED;
	}
	else
	{
		enum clnt_stat result;
		upd_args arg;
		upd_resp resp;

		if (upd_ctor(&arg, nchunks, phashes) < 0)
		{
			LOG(stderr, WARNING_MSG, SUBSYS_META, "cb_update_hashes: could not malloc upd_args!\n");
			errno = ENOMEM;
			put_cb_handle(pclnt, 0);
		}
		else 
		{
			arg.id.type = FILEBYNAME;
			arg.id.identify_u.name = fname;
			arg.begin_chunk = begin_chunk;
			result = capfsd_update_1(arg, &resp, *pclnt);
			if (result != RPC_SUCCESS) 
			{
				LOG(stderr, DEBUG_MSG, SUBSYS_META, "capfsd_update_1 to %d returned %d\n", cb_id, result);
				clnt_perror(*pclnt, "capfs_update_1 :");
				/* make it reconnect */
				put_cb_handle(pclnt, 1);
			}
			else {
				LOG(stderr, DEBUG_MSG, SUBSYS_META, "capfsd_update_1 to %d returned %d\n", cb_id, resp.status);
				put_cb_handle(pclnt, 0);
			}
			upd_dtor(&arg);
		}
	}
	return;
}

/*
 * As the name implies, this routine
 * tries to invalidate the entire sharer set's hcache for a particular
 * file for a specific set of chunks.
 * We take care to ensure that the owner node's hcache
 * is excluded, since the owner initiated the operation.
 */
void cb_invalidate_hashes(char *fname, unsigned long bitmap, int owner_cb_id, int64_t begin_chunk, int64_t nchunks)
{
	int i;

	if (fname == NULL)
	{
		return;
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "cb_invalidate_hashes called by owner %d for %s from %Ld for %Ld chunks\n",
			owner_cb_id, fname, begin_chunk, nchunks);
	i = FindFirstBit((unsigned char *)&bitmap, sizeof(bitmap));
	while (i >= 0)
	{
		CLIENT **pclnt;

		/* i is the cb_index using which we can now make RPC callbacks */
		if (i == owner_cb_id)
		{
			LOG(stderr, DEBUG_MSG, SUBSYS_META, "skipping callback id %d\n", i);
			i = FindNextBit((unsigned char *)&bitmap, 4, i + 1);
			continue;
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "callback id %d\n", i);
		pclnt = get_cb_handle(i);
		if (*pclnt == NULL)
		{
			errno = ECONNREFUSED;
		}
		else
		{
			enum clnt_stat result;
			inv_args arg;
			inv_resp resp;

			arg.id.type = FILEBYNAME;
			arg.id.identify_u.name = fname;
			arg.begin_chunk = begin_chunk;
			arg.nchunks = nchunks;
			result = capfsd_invalidate_1(arg, &resp, *pclnt);
			if (result != RPC_SUCCESS) {
				LOG(stderr, DEBUG_MSG, SUBSYS_META, "capfsd_invalidate_1 [1] to %d returned %d\n", i, result);
				clnt_perror(*pclnt, "capfs_invalidate_1 [1]:");
				/* make it reconnect */
				put_cb_handle(pclnt, 1);
			}
			else {
				LOG(stderr, DEBUG_MSG, SUBSYS_META, "capfsd_invalidate_1 [1] to %d returned %d\n", i, resp.status);
				put_cb_handle(pclnt, 0);
			}
		}
		i = FindNextBit((unsigned char *)&bitmap, 4, i + 1);
	}
	return;
}

/*
 * Clear out the hcache for this particular file 
 * from all the client nodes' hcaches.
 * We ensure that the owner who issued the operation
 * is not called back.
 */
void cb_clear_hashes(char *fname, unsigned long bitmap, int owner_cb_id)
{
	int i;

	if (fname == NULL)
	{
		return;
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "cb_clear_hashes called by owner %d for %s\n", 
			owner_cb_id, fname);
	i = FindFirstBit((unsigned char *)&bitmap, sizeof(bitmap));
	while (i >= 0)
	{
		CLIENT **pclnt;

		/* i is the cb_index using which we can now make RPC callbacks */
		if (i == owner_cb_id)
		{
			LOG(stderr, DEBUG_MSG, SUBSYS_META, "skipping callback id %d\n", i);
			i = FindNextBit((unsigned char *)&bitmap, 4, i + 1);
			continue;
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "callback id %d\n", i);
		pclnt = get_cb_handle(i);
		if (*pclnt == NULL)
		{
			errno = ECONNREFUSED;
		}
		else
		{
			enum clnt_stat result;
			inv_args arg;
			inv_resp resp;

			arg.id.type = FILEBYNAME;
			arg.id.identify_u.name = fname;
			/* Special callback identifier to indicate entire file */
			arg.begin_chunk = -1;
			arg.nchunks = 0;
			result = capfsd_invalidate_1(arg, &resp, *pclnt);
			if (result != RPC_SUCCESS) 
			{
				LOG(stderr, DEBUG_MSG, SUBSYS_META, "capfsd_invalidate_1 [2] to %d returned %d\n", i, result);
				clnt_perror(*pclnt, "capfs_invalidate_1 [2] :");
				/* make it reconnect */
				put_cb_handle(pclnt, 1);
			}
			else 
			{
				LOG(stderr, DEBUG_MSG, SUBSYS_META, "capfsd_invalidate_1 [2] to %d returned %d\n", i, resp.status);
				put_cb_handle(pclnt, 0);
			}
		}
		i = FindNextBit((unsigned char *)&bitmap, 4, i + 1);
	}
	return;
}
