#include "plugin.h"

#define POSIX_POLICY_ID 1


static void posix_init(void)
{
	fprintf(stderr, "Initializing POSIX I/O semantics extensions\n");
	return;
}

static void posix_cleanup(void)
{
	fprintf(stderr, "Cleaning POSIX I/O semantics extensions\n");
	return;
}

static void posix_semantics(int *force_commit, int *desire_hcache_coherence, int *delay_commit)
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

/* How many hashes do we wish to fetch at open time? */
static int posix_pre_open(const char *fname, char **phashes, int *hcount)
{
	/* it is ok to fetch none!! */
	*phashes = NULL;
	*hcount = 0;
	return 0;
}

static int posix_post_open(const char *fname, char *phashes, int hcount)
{
	/* nothing to do.. */
	return 0;
}

struct plugin_ops posix_ops = {
	.init = posix_init,
	.cleanup = posix_cleanup,
	.semantics = posix_semantics,
	.pre_open = posix_pre_open,
	.post_open = posix_post_open,
};

struct plugin_info posix_plugin_info = {
	.policy_name = "posix",
	.policy_id = POSIX_POLICY_ID,
	.policy_ops  = &posix_ops,
};

struct plugin_info *get_plugin_info(void)
{
	return &posix_plugin_info;
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
