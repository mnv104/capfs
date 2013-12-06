#ifndef _HCACHE_H
#define _HCACHE_H

#include <sys/types.h>
#include "cmgr.h"
#include "cmgr_constants.h"

#define MAXNAMELEN 256
#define CAPFS_HCACHE_SIMPLE  0 
#define CAPFS_HCACHE_COMPLEX 1

struct handle {
	char name[MAXNAMELEN];
};

typedef long (*hread_begin)(void* p, 
		int number, cm_buffer_t *buffers, size_t *sizes, int64_t *offsets);
typedef int* (*hread_complete)(long _uptr);
typedef long (*hwrite_begin)(void * p, 
		int number, cm_buffer_t *buffers, size_t *sizes, int64_t *offsets);
typedef int* (*hwrite_complete)(long _uptr);

struct hcache_options {
	int organization;
	hread_begin hr_begin;
	hread_complete hr_complete;
	hwrite_begin hw_begin;
	hwrite_complete hw_complete;
};

extern void hcache_init(struct hcache_options *options);
extern void hcache_finalize(void);
extern int64_t hcache_get(char *fname, int64_t begin_chunk, int64_t nchunks, int64_t prefetch_index, void *buf);
extern int64_t hcache_put(char *fname, int64_t begin_chunk, int64_t nchunks, const void *buf);
extern int hcache_clear(char *filename);
extern int hcache_clear_range(char *filename, int64_t begin_chunk, int nchunks);
extern void hcache_get_stats(cmgr_stats_t *stats, int);
extern void hcache_invalidate(void);

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



