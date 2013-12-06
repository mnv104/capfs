#include "plugin.h"

#define PVFS_POLICY_ID 6

static void pvfs_init(void)
{
	fprintf(stderr, "Initializing PVFS-like I/O semantics extensions\n");
	return;
}

static void pvfs_cleanup(void)
{
	fprintf(stderr, "Cleaning PVFS-like I/O semantics extensions\n");
	return;
}

static void pvfs_semantics(int *force_commit, int *desire_hcache_coherence, int *delay_commit)
{
	/* 
	 * PVFS-like file semantics requires that we 
	 * force commit, we don't desire hcache coherence
	 * and we don't delay commit
	 */
	if (force_commit)
		*force_commit = 1;
	if (desire_hcache_coherence)
		*desire_hcache_coherence = 0;
	if (delay_commit)
		*delay_commit = 0;
	return;
}

/* How many hashes do we wish to fetch at open time? */
static int pvfs_pre_open(const char *fname, char **phashes, int *hcount)
{
	/* it is ok to fetch none!! */
	*phashes = NULL;
	*hcount = 0;
	return 0;
}

static int pvfs_post_open(const char *fname, char *phashes, int hcount)
{
	/* nothing to do.. */
	return 0;
}

struct plugin_ops pvfs_ops = {
	.init = pvfs_init,
	.cleanup = pvfs_cleanup,
	.semantics = pvfs_semantics,
	.pre_open = pvfs_pre_open,
	.post_open = pvfs_post_open,
};

struct plugin_info pvfs_plugin_info = {
	.policy_name = "pvfs",
	.policy_id = PVFS_POLICY_ID,
	.policy_ops  = &pvfs_ops,
};

struct plugin_info *get_plugin_info(void)
{
	return &pvfs_plugin_info;
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
