#include "plugin.h"

#define OPOSIX_POLICY_ID 8


static void oposix_init(void)
{
	fprintf(stderr, "Initializing POSIX I/O semantics extensions with open time hcache fill\n");
	return;
}

static void oposix_cleanup(void)
{
	fprintf(stderr, "Cleaning POSIX I/O semantics extensions with open time hcache fill\n");
	return;
}

static void oposix_semantics(int *force_commit, int *desire_hcache_coherence, int *delay_commit)
{
	/* 
	 * POSIX requires that we do not force commits,
	 * and we desire hcaches to be coherent if they are used at all
	 * and we do not delay the commit.
	 */
	if (force_commit)
		*force_commit = 0;
	if (desire_hcache_coherence)
		*desire_hcache_coherence = 1;
	if (delay_commit)
		*delay_commit = 0;
	return;
}

struct plugin_ops oposix_ops = {
	.init = oposix_init,
	.cleanup = oposix_cleanup,
	.semantics = oposix_semantics,
};

struct plugin_info oposix_plugin_info = {
	.policy_name = "oposix",
	.policy_id = OPOSIX_POLICY_ID,
	.policy_ops  = &oposix_ops,
};

struct plugin_info *get_plugin_info(void)
{
	return &oposix_plugin_info;
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
