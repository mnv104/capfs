#ifndef _CMGR_H
#define _CMGR_H

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/types.h>

/* 
 * Allocated by the buffer manager upto size handle_size 
 * bytes that can be tuned at init time. 
 */
typedef void 			*cm_handle_t;
typedef void 			*cm_buffer_t;
typedef int64_t			cm_page_t;

/* 
 *  Must be provided by the user of the cache manager. 
 *	 
 */
typedef struct {
	char *co_output_fname;
	int co_bsize;
	int co_bcount;
	int co_block_table_size;
	int co_file_table_size;
	int co_handle_size;
	/* compare_block & block_hash are optional routines */
	int (*compare_block)(void *given_key, void *old_key);
	int (*block_hash)(void *key);
	int (*compare_file)(void *given_key, void *old_key);
	int (*file_hash)(void *key);
	long (*readpage_begin)(cm_handle_t p, 
		int number, cm_buffer_t *buffers, size_t *sizes, int64_t *offsets);
	int* (*readpage_complete)(long _uptr);
	long (*writepage_begin)(cm_handle_t p, 
		int number, cm_buffer_t *buffers, size_t *sizes, int64_t *offsets);
	int* (*writepage_complete)(long _uptr);
} cmgr_options_t;

/* currently, the only method implemented for synchronization is cache invalidation */
typedef enum {CM_DONT_SYNCH, CM_INVALIDATE_SYNCH = 1,} synch_options_t;

typedef struct {
    /* This indicates if the marked range of blocks need to be evicted from the cache. */
    int cs_evict;
    union {
	struct
	{
	    /* This indicates if the marked range of blocks need to be written back */
	    int wb;
	    /* This indicates if the marked range of blocks need to be synchronized with the server */
	    synch_options_t synch;
	} keep;
    } cs_opt;
} cmgr_synch_options_t;

typedef struct {
	int64_t 		hits, misses, fixes, unfixes;
	int64_t			fetches, flushes, invalidates;
	int64_t			evicts, nharvests, nscans;
} cmgr_stats_t;

extern  FILE			*output_fp, *error_fp;

extern int 			CMGR_init(cmgr_options_t *options);
extern void 			CMGR_finalize(void);
extern int64_t			CMGR_get_region(char *buffer, cm_handle_t p,
				    int64_t begin_byte, int64_t count, int64_t prefetch_index, cmgr_synch_options_t *options);
extern int64_t			CMGR_put_region(char *buffer, cm_handle_t p,
				    int64_t begin_byte, int64_t count, cmgr_synch_options_t *options);
extern int			CMGR_synch_region(cm_handle_t p, int64_t start,
				    int64_t new_size, cmgr_synch_options_t *options, int blocking);
extern void	 		CMGR_block_wb_all(void);
extern int			CMGR_get_stats(cmgr_stats_t *, int reset);
extern void			CMGR_invalidate(void);

extern int 			CMGR_simple_init(cmgr_options_t *options);
extern void 			CMGR_simple_finalize(void);
extern int64_t			CMGR_simple_get(char *buffer, cm_handle_t p,
				    int64_t begin_chunk, int64_t nchunks, int64_t prefetch_index);
extern int64_t			CMGR_simple_put(char *buffer, cm_handle_t p,
				    int64_t begin_chunk, int64_t nchunks);
extern int			CMGR_simple_synch_region(cm_handle_t p, int64_t begin_chunk, int64_t nchunks,
				    cmgr_synch_options_t *options, int blocking);
extern void			CMGR_simple_invalidate(void);

#ifdef MMAP_SUPPORT
extern int 			CMGR_add_mappings(cm_handle_t p, unsigned long offset,
						size_t length, void *ptr, int prot, int flags);
extern int 			CMGR_del_mappings(void *ptr, size_t length);
extern int			CMGR_remap_mappings(void *old_address, size_t old_size,
						size_t new_size, int flags);
extern int			CMGR_sync_mappings(unsigned long start_address, size_t length, int flags);
extern int			CMGR_fixup_mappings(unsigned long fault_address);
#endif

#endif
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 *
 */

