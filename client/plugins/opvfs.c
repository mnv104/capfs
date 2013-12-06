#include "plugin.h"

#define OPVFS_POLICY_ID 9

static void opvfs_init(void)
{
	fprintf(stderr, "Initializing PVFS-like I/O semantics extensions with open time hcache file\n");
	return;
}

static void opvfs_cleanup(void)
{
	fprintf(stderr, "Cleaning PVFS-like I/O semantics extensions with open time hcache file\n");
	return;
}

static void opvfs_semantics(int *force_commit, int *desire_hcache_coherence, int *delay_commit)
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

struct plugin_ops opvfs_ops = {
	.init = opvfs_init,
	.cleanup = opvfs_cleanup,
	.semantics = opvfs_semantics,
};

struct plugin_info opvfs_plugin_info = {
	.policy_name = "opvfs",
	.policy_id = OPVFS_POLICY_ID,
	.policy_ops  = &opvfs_ops,
};

struct plugin_info *get_plugin_info(void)
{
	return &opvfs_plugin_info;
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
