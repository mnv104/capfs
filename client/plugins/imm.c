#include "plugin.h"

#define IMM_POLICY_ID 3

static void imm_init(void)
{
	fprintf(stderr, "Initializing IMMUTABLE FILES I/O semantics extensions\n");
	return;
}

static void imm_cleanup(void)
{
	fprintf(stderr, "Cleaning IMMUTABLE FILES I/O semantics extensions\n");
	return;
}

static void imm_semantics(int *force_commit, int *desire_hcache_coherence, int *delay_commit)
{
	/* 
	 * Immutable file semantics requires that we 
	 * force commit, we desire hcache coherence
	 * BUT we delay commit
	 * This differs from session semantics in the actual
	 * hashes that are going to be committed on a session
	 * close.
	 */
	if (force_commit)
		*force_commit = 1;
	if (desire_hcache_coherence)
		*desire_hcache_coherence = 1;
	if (delay_commit)
		*delay_commit = 1;
	return;
}

struct plugin_ops imm_ops = {
	.init = imm_init,
	.cleanup = imm_cleanup,
	.semantics = imm_semantics,
};

struct plugin_info imm_plugin_info = {
	.policy_name = "imm",
	.policy_id = IMM_POLICY_ID,
	.policy_ops  = &imm_ops,
};

struct plugin_info *get_plugin_info(void)
{
	return &imm_plugin_info;
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
