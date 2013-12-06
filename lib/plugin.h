#ifndef _PLUGIN_H
#define _PLUGIN_H

#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <capfs_types.h>
#include <ll_capfs.h>
#include <stdlib.h>
#include "list.h"
#include "capfs_config.h"

/* Header file to be used by new plugin writers for defining newer consistency semantics */

struct plugin_ops {
	/* names are pretty self-explanatory */
	void (*init)(void);
	void (*cleanup)(void);
	void (*semantics)(int *force_commit, int *desire_hcache_coherence, int *delay_commit);
	int (*pre_open)(const char *, char **pphashes, int *hcount);
	int (*post_open)(const char *, char *phashes, int hcount);
	int (*write)(const char *, const void *, capfs_off_t, capfs_size_t);
	int (*close)(const char *);
	int (*sync)(const char *);
};

struct plugin_info {
	/* to be filled by the plugin layer */
	const char *policy_name;
	int         policy_id;
	struct plugin_ops *policy_ops; 
	/* below this fields will be initialized by the client core */
	struct list_head next;
	void *handle; /* handle to the dlopen'ed so file */
	char *filename; /* name of the so file */
};

#endif

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 *
 * vim: ts=3
 */



