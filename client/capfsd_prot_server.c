/*
 * Implements callbacks for hcache coherence traffic.
 * Murali Vilayannur (C) 2005.
 */
#include "capfs-header.h"
#include "capfsd_prot.h"
#include <capfs_config.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include "log.h"
#include "hcache.h"
#include "sha.h"

struct hcache_cb_stats {
	int64_t hcache_inv;
	int64_t hcache_inv_range;
	int64_t hcache_upd;
};

static struct hcache_cb_stats hcache_cbs;

inline int64_t inc_hcache_inv(void)
{
	return hcache_cbs.hcache_inv++;
}

inline int64_t inc_hcache_inv_range(void)
{
	return hcache_cbs.hcache_inv_range++;
}

inline int64_t inc_hcache_upd(void)
{
	return hcache_cbs.hcache_upd++;
}

/* This routine also resets the counters */
void hcache_get_cb_stats(int64_t *hcache_inv, int64_t *hcache_inv_range, int64_t *hcache_upd)
{
	*hcache_inv = hcache_cbs.hcache_inv;
	*hcache_inv_range = hcache_cbs.hcache_inv_range;
	*hcache_upd = hcache_cbs.hcache_upd;
	hcache_cbs.hcache_inv = hcache_cbs.hcache_inv_range = hcache_cbs.hcache_upd = 0;
	return;
}

bool_t
capfsd_invalidate_1_svc(inv_args arg1, inv_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	fname handle = NULL;
	int ret = 0;


	if (arg1.id.type == FILEBYNAME)
	{
		struct sockaddr_in *mgr_addr = NULL;
		char *mgr_host = NULL;
		fname modified_handle = NULL;
		int64_t counter = 0;

		handle = arg1.id.identify_u.name;
		/*
		 * HACK
		 * Server does not understand our way of constructing filenames.
		 * We need to slap on the metadata URL etc etc before the filename.
		 */
		mgr_addr = svc_getcaller(rqstp->rq_xprt);
		mgr_host = inet_ntoa(mgr_addr->sin_addr);
		modified_handle = (fname) calloc(1, CAPFS_MAXNAMELEN);
		if (modified_handle == NULL)
		{
			ret = -ENOMEM;
		}
		else
		{
			/* port number should always be set to 0? */
			sprintf(modified_handle, "%s:0%s", mgr_host, handle);
			if (arg1.begin_chunk < 0)
			{
				counter = inc_hcache_inv();
				LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[%Ld] hcache_clear() callback for %s\n",
						counter, modified_handle);
				ret = hcache_clear(modified_handle);
			}
			else
			{
				counter = inc_hcache_inv_range();
				LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[%Ld] hcache_clear_range() callback for %s starting at %Ld for %Ld chunks\n",
						counter, modified_handle, arg1.begin_chunk, arg1.nchunks);
				ret = hcache_clear_range(modified_handle, arg1.begin_chunk, arg1.nchunks);
			}
			LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[%Ld] callback finished\n", counter);
			free(modified_handle);
		}
	}
	else
	{
		LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "Cannot hcache invalidates by handle yet!\n");
		ret = -ENOSYS;
	}
	result->status = ret;
	return retval;
}

bool_t
capfsd_update_1_svc(upd_args arg1, upd_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	int ret = 0;
	fname handle = NULL;
	void *updated_hashes = NULL;
	
	if (arg1.id.type == FILEBYNAME)
	{
		struct sockaddr_in *mgr_addr = NULL;
		char *mgr_host = NULL;
		fname modified_handle = NULL;
		int64_t counter = 0;

		counter = inc_hcache_upd();
		handle = arg1.id.identify_u.name;
		/*
		 * HACK
		 * Server does not understand our way of constructing filenames.
		 * We need to slap on the metadata URL etc etc before the filename.
		 */
		mgr_addr = svc_getcaller(rqstp->rq_xprt);
		mgr_host = inet_ntoa(mgr_addr->sin_addr);
		modified_handle = (fname) calloc(1, CAPFS_MAXNAMELEN);
		if (modified_handle == NULL)
		{
			ret = -ENOMEM;
		}
		else 
		{
			/* port number should always be set to 0? */
			sprintf(modified_handle, "%s:0%s", mgr_host, handle);
			LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[%Ld] hcache_put() callback for %s starting at %Ld for %d chunks\n",
					counter, modified_handle, arg1.begin_chunk, arg1.hashes.sha1_list_len);
			if (arg1.hashes.sha1_list_len <= 0)
			{
				ret = -EINVAL;
			}
			else
			{
				int i;

				updated_hashes = (void *) calloc(arg1.hashes.sha1_list_len, CAPFS_MAXHASHLENGTH);
				if (updated_hashes == NULL)
				{
					ret = -ENOMEM;
				}
				else
				{
					for (i = 0; i < arg1.hashes.sha1_list_len; i++)
					{
#ifdef DEBUG
						char str[256];
						hash2str(arg1.hashes.sha1_list_val[i].h, CAPFS_MAXHASHLENGTH, str);
						LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "%d: %s\n", i, str);
#endif
						memcpy(updated_hashes + i * CAPFS_MAXHASHLENGTH, arg1.hashes.sha1_list_val[i].h, CAPFS_MAXHASHLENGTH);
					}
					/* FIXME: Could this deadlock with a locally issued RPC call for hcache_get() */
					ret = hcache_put(modified_handle, arg1.begin_chunk, arg1.hashes.sha1_list_len, updated_hashes);
					free(updated_hashes);
				}
			}
			free(modified_handle);
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_CLIENT, "[%Ld] callback finished\n", counter);
	}
	else
	{
		LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "Cannot hcache updates by handle yet!\n");
		ret = -ENOSYS;
	}
	result->status = ret;
	return retval;
}

int
capfs_capfsd_1_freeresult (SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
	xdr_free (xdr_result, result);

	/*
	 * Insert additional freeing code here, if needed
	 */

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

