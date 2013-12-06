#ifndef _DCACHE_H
#define _DCACHE_H

#include <sys/types.h>
#include "cmgr.h"
#include "cmgr_constants.h"

#define EVP_SHA1_SIZE 20

struct handle {
	char hash[EVP_SHA1_SIZE];
};

extern void dcache_init(void);
extern void dcache_finalize(void);
extern int dcache_get(char *hash, void *buf, size_t size);
extern int dcache_put(char *hash, const void *buf, size_t size);
extern void hcache_get_stats(cmgr_stats_t *stats, int reset);

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



