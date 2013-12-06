#include "plugin.h"

#define OFORCE_POLICY_ID 7

static void oforce_init(void)
{
	fprintf(stderr, "Initializing FORCE-COMMIT I/O semantics extensions with open time hcache fill\n");
	return;
}

static void oforce_cleanup(void)
{
	fprintf(stderr, "Cleaning FORCE-COMMIT I/O semantics extensions with open time hcache fill\n");
	return;
}

static void oforce_semantics(int *force_commit, int *desire_hcache_coherence, int *delay_commit)
{
	/* 
	 * Force-commit file semantics dictates that we 
	 * force commit, we desire hcache coherence
	 * and we don't delay commit
	 */
	if (force_commit)
		*force_commit = 1;
	if (desire_hcache_coherence)
		*desire_hcache_coherence = 1;
	if (delay_commit)
		*delay_commit = 0;
	return;
}

struct plugin_ops oforce_ops = {
	.init = oforce_init,
	.cleanup = oforce_cleanup,
	.semantics = oforce_semantics,
};

struct plugin_info oforce_plugin_info = {
	.policy_name = "oforce",
	.policy_id = OFORCE_POLICY_ID,
	.policy_ops  = &oforce_ops,
};

struct plugin_info *get_plugin_info(void)
{
	return &oforce_plugin_info;
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
