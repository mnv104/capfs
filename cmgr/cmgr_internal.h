#ifndef _CMGR_INTERNAL_H
#define _CMGR_INTERNAL_H

#include <stdio.h>
#include <sys/types.h>
#include <pthread.h>
#include <execinfo.h>
#include "cmgr_constants.h"
#include "quicklist.h"
#include "mquickhash.h"
#include "rbtree.h"

#include "cmgr.h"
#define CMGR_BACKTRACE_DEPTH 10

static inline void cmgr_backtrace(void)
{
    void* trace[CMGR_BACKTRACE_DEPTH];
    char** messages = NULL;
    int i, trace_size;

    trace_size = backtrace(trace, CMGR_BACKTRACE_DEPTH);
    messages = backtrace_symbols(trace, trace_size);
    for(i=1; i<trace_size; i++)
    {
	lock_printf("\t[bt] %s\n", messages[i]);
    }
    free(messages);
}

typedef struct 
{
	cmgr_options_t		options;
	pthread_t	   	harvester;
	pthread_mutex_t 	mutex;
	pthread_cond_t  	avail, needed;
	int 			low_water, high_water;
	int			batch_ratio;
	int 			num_free_frames;
	int			log_bsize;
	long 			page_size;
	long 			page_shift;
	long 			page_mask;
	cmgr_stats_t		stats;
} cmgr_internal_options_t;

extern cmgr_internal_options_t global_options;

#define BSIZE		(global_options.options.co_bsize)
#define BCOUNT		(global_options.options.co_bcount)
#define BTSIZE		(global_options.options.co_block_table_size)
#define BFTSIZE		(global_options.options.co_file_table_size)
#define HANDLESIZE	(global_options.options.co_handle_size)
#define LOG_BSIZE	(global_options.log_bsize)
#define PAGESIZE	(global_options.page_size)
#define PAGESHIFT	(global_options.page_shift)
#define PAGEMASK	(global_options.page_mask)
#define PAGE_ALIGN(x)	(((x) + PAGESIZE - 1) & PAGEMASK)
#define FIXES		(global_options.stats.fixes)
#define UNFIXES		(global_options.stats.unfixes)
#define HITS	        (global_options.stats.hits)
#define MISSES	        (global_options.stats.misses)
#define FETCHES	        (global_options.stats.fetches)
#define FLUSHES	        (global_options.stats.flushes)
#define INVALIDATES	(global_options.stats.invalidates)
#define EVICTS		(global_options.stats.evicts)
#define HARVESTS	(global_options.stats.nharvests)
#define SCANS		(global_options.stats.nscans)

typedef int32_t 	cm_id_t;

typedef struct 
{
	cm_handle_t	cb_handle;
	cm_page_t	cb_page;
} cm_block_t;

enum {DONT_FETCH = 0, DO_FETCH = 1};
enum {FRAME_READ = 0, FRAME_WRITE = 1};

#define NULL_BLOCK(ret) ((cm_block_t) {cm_handles[(ret)], -1})

typedef int32_t		cm_magic_t;
typedef int32_t		cm_flags_t;
typedef int32_t		cm_size_t;
typedef int64_t		cm_pos_t;
typedef int32_t		cm_count_t;

typedef struct cm_frame cm_frame_t;
typedef struct cm_file  cm_file_t;

#define PG_dirty    0 /* dirty bit */
#define PG_free	    1 /* free bit */
#define PG_invalid  2 /* invalid bit */
#define PG_uptodate 3 /* uptodate bit */
#define PG_file	    4 /* file bit */

#define Page_Dirty(fr)	    test_bit(PG_dirty, &(fr)->cm_flags)
#define SetPageDirty(fr)    set_bit(PG_dirty, &(fr)->cm_flags)
#define ClearPageDirty(fr)  clear_bit(PG_dirty, &(fr)->cm_flags)

#define Page_Free(fr)	    test_bit(PG_free, &(fr)->cm_flags)
#define SetPageFree(fr)     set_bit(PG_free, &(fr)->cm_flags)
#define ClearPageFree(fr)   clear_bit(PG_free, &(fr)->cm_flags)

#define Page_Invalid(fr)     test_bit(PG_invalid, &(fr)->cm_flags)
#define SetPageInvalid(fr)   set_bit(PG_invalid, &(fr)->cm_flags)
#define ClearPageInvalid(fr) clear_bit(PG_invalid, &(fr)->cm_flags)

#define Page_Uptodate(fr)    test_bit(PG_uptodate, &(fr)->cm_flags)
#define SetPageUptodate(fr)    set_bit(PG_uptodate, &(fr)->cm_flags)
#define ClearPageUptodate(fr)  clear_bit(PG_uptodate, &(fr)->cm_flags)

#define Page_File(fr)	    test_bit(PG_file, &(fr)->cm_flags)
#define SetPageFile(fr)    set_bit(PG_file, &(fr)->cm_flags)
#define ClearPageFile(fr)  clear_bit(PG_file, &(fr)->cm_flags)

/*
 * NOTE: The routines below are non-atomic!
 * But we dont need any atomic versions of these
 * routines, since they operate with a higher-level
 * mutex lock. Hence atomicity is guaranteed when 
 * such routines are called.
 */
static inline int test_bit(int nr, void *addr)
{
	unsigned int *p = (unsigned int *) addr;

	return ((p[nr >> 5] >> (nr & 0x1f)) & 1) != 0;
}

static inline void clear_bit(int nr, void *addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);

	*p &= ~mask;
	return;
}

static inline void set_bit(int nr, void *addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);

	*p |= mask;
	return;
}
/*
 * cm_hash is used to chain cm_frame structures
 * in the hash table and cm_file is used
 * to chain cm_frame strcutures in the cm_file
 * structure.
 */
struct cm_frame {
	/* lock to protect the page when it is being read from or written to */
	pthread_mutex_t cm_lock;
	cm_magic_t cm_magic;
	cm_id_t   cm_id;
	/* Valid portions of the cache frame */
	cm_count_t cm_valid_count;
	cm_pos_t   *cm_valid_start;
	cm_size_t  *cm_valid_size;
	/* private information */
	void	   *cm_private;
	/* what logical file block is housed in this cache object frame */
	cm_block_t cm_block;
	/* pointer to the payload itself */
	cm_buffer_t cm_buffer;
	/* meta-data used to manage the cache */
	cm_flags_t cm_ref, cm_error;
	/* Number of references to this cache frame */
	cm_flags_t cm_fix;
	/* state of this cache frame */
	cm_flags_t cm_flags;
	/*
	 * PG_free == 0 means it is on the BLOCK hash list 
	 	   == 1 means it is on the FREE list.
	 * PG_file == 0 means it is not on the FILE list.
	  	   == 1 means it is on the FILE list
	 * PG_invalid == 0 means it is a valid frame
		       == 1 means that is an invalid frame,but
     			 it is still on the block hash tables.
	 * PG_uptodate == 0 means that is a valid frame, but it is not uptodate.
	 	       == 1 means that it is a valid and uptodate frame.
	 */
	/* chain cm_frame structures in a hash table */
	struct qlist_head cm_hash;
	/* chain cm_frame structures within the same file */
	struct qlist_head cm_file;
};

static inline void lock_page(cm_frame_t *fr)
{
	int ret;

	lock_printf("LOCK page[%u - %p] (%u)\n",
			fr->cm_id, fr, fr->cm_fix);
	if ((ret = pthread_mutex_trylock(&fr->cm_lock)) != 0) {
		lock_printf("LOCK page[%u - %p] is going to BLOCK!(%u)\n",
				fr->cm_id, fr, fr->cm_fix);
		pthread_mutex_lock(&fr->cm_lock);
	}
	return;
}

static inline int trylock_page(cm_frame_t *fr)
{
	int ret;

	lock_printf("TRYLOCK page[%u - %p] (%u)\n",
			fr->cm_id, fr, fr->cm_fix);
	if ((ret = pthread_mutex_trylock(&fr->cm_lock)) != 0) {
		lock_printf("TRYLOCK page[%u - %p] failed (%u)\n",
				fr->cm_id, fr, fr->cm_fix);
	}
	return ret;
}

static inline void unlock_page(cm_frame_t *fr)
{
	lock_printf("UNLOCK page [%u - %p] (%u)\n",
		fr->cm_id, fr, fr->cm_fix);
	pthread_mutex_unlock(&fr->cm_lock);
	return;
}

#ifdef MMAP_SUPPORT
/*
 * Description of mappings used to support mmap() style operations.
 * These structures are chained together in the cf_file structures 
 * and they are kept sorted there by file offset.
 * In addition, they are also kept in a Red-black tree,
 * so that the SIGSEGV handler can retrieve this mapping without having
 * to search the list of all mappings for all files.
 */
typedef struct {
	cm_handle_t			cm_handle;
	unsigned long			cm_offset;
	size_t		  		cm_size;
	void			  	*cm_ptr;
	int			  	cm_prot;
	int 			  	cm_flags;
	/* bitmask of whether or not a page has been dirtied or not */
	unsigned char			*cm_faulted_flags;
	/* cm_next chains these structures together that are anchored at cm_file->cf_map */
	struct qlist_head		cm_next;
	/* cm_tree keeps it a part of the RB tree */
	rb_node_t			cm_tree;
} cm_map_t;

#endif

struct hash_entry {
    unsigned char h_valid;
    unsigned char h_hash[20];
};

struct cm_file_hashes {
    /* nhashes is the amount allocated */
    int64_t cm_nhashes;
    struct  hash_entry   *cm_phashes;
};

/*
 * cf_hash is used to chain cm_file structures
 * in the hash table and cf_list is 
 * used to keep track of all pages
 * of a particular file
 */
struct cm_file {
	pthread_mutex_t   cf_lock; /* protects the cf_list list */
	cm_handle_t	  cf_handle; /* handle of this file */
	int		  cf_pin;   /* pin this pointer */
	int		  cf_ref; /* Any holders of a filp pointer increment this counter */
	int		  cf_error; /* errors on writebacks are recorded here */
	struct qlist_head cf_hash; /* chains cm_file structures in the hash chain */
	struct qlist_head cf_list; /* chains all cm_frame_t structures of this file together */
	struct cm_file_hashes cf_hashes; /* simpler hcache core anchor */
	/*
	 * on a close() traverse the cm_frame_t structures,
	 * and find out if any of them have errors,
	 * propogate it at time of close()
	 */
#ifdef MMAP_SUPPORT
	struct qlist_head cf_map; /* chains all cf_map_t structures of this file together */ 
#endif
};

/* check whether a number is a power-of-two */
static inline int check_power_of_two(int bsize)
{
	if (bsize & (bsize - 1)) 
	{
		/* not a power of 2 */
		return 0;
	}
	return 1;
}

/* get the logarithm of a number that is a power-of-2 */
static inline int LOG_2(int bsize)
{
	int val = 1, count = 0;

	if (bsize <= 0
		|| (bsize & (bsize - 1)))
	{
		return -1;
	}
	do
	{
		count++;
		val = (bsize & 1);
		bsize >>= 1;
	} while(val == 0);
	return (count - 1);
}



typedef int 			(*comp_fn)(void *, void*);
typedef int 			(*hash_fn)(void *);

extern void	 	     	CMGRINT_mark_page_free(cm_frame_t *fr);
extern cm_frame_t*		CMGRINT_wait_for_free(void);
extern void			CMGRINT_do_sanity_checks(cm_frame_t *frame);

extern int			CMGRINT_file_init(cmgr_options_t *options, int file_table_size);
extern void 			CMGRINT_file_finalize(void);
extern cm_file_t*		CMGRINT_file_get(cm_handle_t p);
extern void 			CMGRINT_file_put(cm_file_t *file);
extern void 			CMGRINT_file_add(cm_frame_t *p, cm_file_t *filp);
extern void 			CMGRINT_file_del(cm_frame_t *p, int failed_wb);
extern void			CMGRINT_file_freespace(cm_file_t *filp);
extern int			CMGRINT_file_resize(cm_file_t *filp, int64_t nchunks);
extern void			CMGRINT_simple_invalidate(void);

extern int			CMGRINT_block_init(cmgr_options_t *options, int block_table_size);
extern void			CMGRINT_block_finalize(void);
extern cm_frame_t*		CMGRINT_block_get(cm_block_t *block, void *private, int *error, int account_miss);
extern void 			CMGRINT_block_put(cm_frame_t *p);
extern int	 		CMGRINT_block_del(cm_frame_t *p, cm_block_t *old_block, int force);
extern int			CMGRINT_fixup_valid_regions(cm_frame_t *handle,
					cm_pos_t valid_start, cm_size_t valid_size);
extern void			CMGRINT_print_valid_regions(cm_frame_t *handle);

/* routines to issue and wait for I/O completion on a cache frame object */
extern int64_t			__CMGRINT_wb_sync(int nframes, cm_frame_t **fr);
extern int64_t			CMGRINT_wb_sync(int nframes, cm_frame_t **fr);
extern int64_t			__CMGRINT_fetch_sync(int nframes, cm_frame_t **fr, cm_pos_t*, cm_size_t*);
extern int64_t			CMGRINT_fetch_sync(int nframes, cm_frame_t **fr, cm_pos_t*, cm_size_t*);

#ifdef MMAP_SUPPORT
extern void	 		cmgr_file_add_mapping(cm_map_t *map, cm_file_t *filp);
extern void			cmgr_file_del_mapping(cm_map_t *map);
extern int 			cmgr_mmap_init(void);
extern void			cmgr_mmap_finalize(void);
extern void			cmgr_map_dealloc(cm_map_t *map);
extern int 			cmgr_invalidate_mappings(cm_handle_t p, cm_page_t begin_block, 
						cm_page_t num_blocks);
#endif

#endif
/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */


