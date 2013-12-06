#include "plugin.h"

#define SESSION_POLICY_ID 2


static void session_init(void)
{
	fprintf(stderr, "Initializing SESSION I/O semantics extensions\n");
	return;
}

static void session_cleanup(void)
{
	fprintf(stderr, "Cleaning SESSION I/O semantics extensions\n");
	return;
}

static void session_semantics(int *force_commit, int *desire_hcache_coherence, int *delay_commit)
{
	/* 
	 * Session requires that we force commits,
	 * and that we desire hcaches to be coherent
	 * BUT we delay the commit to the time of a close!
	 */
	if (force_commit)
		*force_commit = 1;
	if (desire_hcache_coherence)
		*desire_hcache_coherence = 1;
	if (delay_commit)
		*delay_commit = 1;
	return;
}

/* How many hashes do we wish to fetch at open time? */
static int session_pre_open(const char *fname, char **phashes, int *hcount)
{
	/* We wish to get all the hashes of the file actually here... */
	*hcount = CAPFS_MAXHASHES;
	*phashes = (char *) calloc(*hcount, CAPFS_MAXHASHLENGTH);
	if (*phashes == NULL)
	{
		*hcount = 0;
	}
	return 0;
}

static int session_post_open(const char *fname, char *phashes, int hcount)
{
	/* We need to free hashes in case hcount is -ve */
	if (hcount < 0)
	{
		free(phashes);
	}
	else
	{
		/* we just need to add a data-structure to ensure that we have
		 * sufficient state to remember and free at the time of
	 	 * close
		 */
	}
	return 0;
}

struct plugin_ops session_ops = {
	.init = session_init,
	.cleanup = session_cleanup,
	.semantics = session_semantics,
	.pre_open = session_pre_open,
	.post_open = session_post_open,
};

struct plugin_info session_plugin_info = {
	.policy_name = "session",
	.policy_id = SESSION_POLICY_ID,
	.policy_ops  = &session_ops,
};

struct plugin_info *get_plugin_info(void)
{
	return &session_plugin_info;
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
