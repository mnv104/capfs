#include "plugin.h"

#define FORCE_POLICY_ID 5

static void force_init(void)
{
	fprintf(stderr, "Initializing FORCE-COMMIT I/O semantics extensions\n");
	return;
}

static void force_cleanup(void)
{
	fprintf(stderr, "Cleaning FORCE-COMMIT I/O semantics extensions\n");
	return;
}

static void force_semantics(int *force_commit, int *desire_hcache_coherence, int *delay_commit)
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

/* How many hashes do we wish to fetch at open time? */
static int force_pre_open(const char *fname, char **phashes, int *hcount)
{
	/* it is ok to fetch none!! */
	*phashes = NULL;
	*hcount = 0;
	return 0;
}

static int force_post_open(const char *fname, char *phashes, int hcount)
{
	/* nothing to do.. */
	return 0;
}

struct plugin_ops force_ops = {
	.init = force_init,
	.cleanup = force_cleanup,
	.semantics = force_semantics,
	.pre_open = force_pre_open,
	.post_open = force_post_open,
};

struct plugin_info force_plugin_info = {
	.policy_name = "force",
	.policy_id = FORCE_POLICY_ID,
	.policy_ops  = &force_ops,
};

struct plugin_info *get_plugin_info(void)
{
	return &force_plugin_info;
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
