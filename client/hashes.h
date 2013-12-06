#ifndef _FETCH_HASHES_H
#define _FETCH_HASHES_H

#include <stdio.h>
#include <sys/types.h>
#include <meta.h>

extern void init_hashes(void);
extern void cleanup_hashes(void);
extern int64_t get_hashes(int use_hcache, char *name, int64_t begin_chunk, 
		int64_t nchunks, int64_t prefetch_index, void *buf, fmeta *meta);
extern int64_t put_hashes(char *name, int64_t begin_chunk, int64_t nchunks, void *buf);
extern int clear_hashes(char *name);
extern void hashes_stats(int64_t *hits, int64_t *misses, int64_t *fetches, int64_t *inv, int64_t *evict);
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

