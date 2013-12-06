#include "plugin.h"

#define TRANS_POLICY_ID 4


static void trans_init(void)
{
	fprintf(stderr, "Initializing TRANSACTIONAL I/O semantics extensions\n");
	return;
}

static void trans_cleanup(void)
{
	fprintf(stderr, "Cleaning TRANSACTIONAL I/O semantics extensions\n");
	return;
}

static void trans_semantics(int *force_commit, int *desire_hcache_coherence, int *delay_commit)
{
	/* 
	 * Transactional file semantics requires that we don't
	 * force commit, and that we desire hcache coherence
	 * BUT we delay commit
	 */
	if (force_commit)
		*force_commit = 0;
	if (desire_hcache_coherence)
		*desire_hcache_coherence = 1;
	if (delay_commit)
		*delay_commit = 1;
	return;
}

struct plugin_ops trans_ops = {
	.init = trans_init,
	.cleanup = trans_cleanup,
	.semantics = trans_semantics,
};

struct plugin_info trans_plugin_info = {
	.policy_name = "trans",
	.policy_id = TRANS_POLICY_ID,
	.policy_ops  = &trans_ops,
};

struct plugin_info *get_plugin_info(void)
{
	return &trans_plugin_info;
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
