#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include "cmgr_internal.h"
#include "gen-locks.h"
#include "rbtree.h"

static rb_root_t tree = RB_ROOT;
static pthread_rwlock_t rbtree_lock = PTHREAD_RWLOCK_INITIALIZER;

static inline void rbtree_rdlock(void)
{
	pthread_rwlock_rdlock(&rbtree_lock);
	return;
}

static inline void rbtree_wrlock(void)
{
	pthread_rwlock_wrlock(&rbtree_lock);
	return;
}

static inline void rbtree_unlock(void)
{
	pthread_rwlock_unlock(&rbtree_lock);
}

static inline int rbtree_tryrdlock(void)
{
	return pthread_rwlock_tryrdlock(&rbtree_lock);
}

static inline int rbtree_trywrlock(void)
{
	return pthread_rwlock_trywrlock(&rbtree_lock);
}

/*
 * redblack tree implementation:
 * In order to avoid the overhead of searching through all the
 * entries in the cache to find the entry with the furthest timestamp
 * in the future, we maintain a red-black tree (sorted by future 
 * timestamps) so that the cost of obtaining the entry with the furthest 
 * timestamp becomes O(lg N). Note that we also pay a cost proportional
 * to the height of the tree when we insert entries
 */
static cm_map_t *rb_search_map(unsigned long ptr, 
		rb_node_t ***rb_link, rb_node_t ** rb_parent) 
{
	rb_node_t **n = &tree.rb_node, *m = NULL;
	cm_map_t *map = NULL;

	while (*n) 
	{
		m = *n;
		map = rb_entry(m, cm_map_t, cm_tree);
		if (ptr < (unsigned long) map->cm_ptr) 
		{
			n = &m->rb_left;
		}
		else if (ptr >= ((unsigned long) map->cm_ptr + map->cm_size)) 
		{
			n = &m->rb_right;
		}
		else 
		{
			return map;
		}
	}
	*rb_link = n;
	*rb_parent = m;
	return NULL;
}

static inline void rb_link_map(cm_map_t *map, 
		rb_node_t ** rb_link, rb_node_t * rb_parent)
{
	rb_link_node(&map->cm_tree, rb_parent, rb_link);
	rb_insert_color(&map->cm_tree, &tree);
	return;
}

static int rb_insert_map(cm_map_t *map)
{
	rb_node_t **rb_link, *rb_parent;

	rbtree_wrlock();
	/* Search for entry and the position at which to insert */
	if (rb_search_map((unsigned long) map->cm_ptr, 
				&rb_link, &rb_parent) == NULL) 
	{
		rb_link_map(map, rb_link, rb_parent);
		rbtree_unlock();
		return 0;
	}
	rbtree_unlock();
	panic("Duplicate mmap block [%ld - %d] detected!",
			(unsigned long) map->cm_ptr, map->cm_size);
	return -1;
}

static cm_map_t* rb_delete_map(unsigned long address, size_t size)
{
	rb_node_t **rb_link, *rb_parent;
	cm_map_t *map = NULL;

	rbtree_wrlock();
	/* Search for the entry to be delete */
	if ((map = rb_search_map(address,
				&rb_link, &rb_parent)) == NULL) 
	{
		/* if we cannot find it, then we cannot delete it */
		rbtree_unlock();
		return NULL;
	}
	if (address != (unsigned long) map->cm_ptr
			|| size != map->cm_size)
	{
		rbtree_unlock();
		panic("Cannot handle munmap's of smaller chunks yet..!\n");
		return NULL;
	}
	/* Erase this element if entire address has been asked to delete */
	rb_erase(&map->cm_tree, &tree);
	rbtree_unlock();
	return map;
}

static cm_map_t* rb_find_map(unsigned long address)
{
	rb_node_t **rb_link, *rb_parent;
	cm_map_t *map = NULL;

	/* return the result of the search */
	rbtree_rdlock();
	map = rb_search_map(address, &rb_link, &rb_parent);
	rbtree_unlock();
	return map;
}

static unsigned char flags_to_OR[8] ={1,2,4,8,16,32,64,128};
static unsigned char flags_to_AND[8]={254,253,251,247,239,223,191,127};

/*
 * Manipulate & Query the fault flags bitmap
 * in the cm_map_t structure.
 */
static inline int set_page_faulted(cm_map_t *map, unsigned long fault_page)
{
	unsigned long map_start_page;
	unsigned char *byte = NULL;
	unsigned  long diff = 0;

	map_start_page = (unsigned long) map->cm_ptr;
	diff = fault_page - map_start_page;
	assert(diff < (map->cm_size / PAGESIZE));
	byte = &map->cm_faulted_flags[diff / 8];
	*byte |= flags_to_OR[diff % 8];
	return 0;
}

static inline int clear_page_faulted(cm_map_t *map, unsigned long fault_page)
{
	unsigned long map_start_page;
	unsigned char *byte = NULL;
	unsigned  long diff = 0;

	map_start_page = (unsigned long) map->cm_ptr;
	diff = fault_page - map_start_page;
	assert(diff < (map->cm_size / PAGESIZE));
	byte = &map->cm_faulted_flags[diff / 8];
	*byte &= (unsigned char)flags_to_AND[diff % 8];
	return 0;
}

static inline int page_faulted(cm_map_t *map, unsigned long fault_page)
{
	unsigned long map_start_page;
	unsigned char *byte = NULL;
	unsigned  long diff = 0;

	map_start_page = (unsigned long) map->cm_ptr;
	diff = fault_page - map_start_page;
	assert(diff < (map->cm_size / PAGESIZE));
	byte = &map->cm_faulted_flags[diff / 8];
	if (*byte & flags_to_OR[diff % 8])
		return 1;
	return 0;
}

/* 
 * Constructors & Destructors for the cm_map_t 
 * structure/object that is linked in cf_file_t
 * as well as stowed away in the rb tree
 */
static cm_map_t *cmgr_map_alloc(cm_handle_t p, unsigned long offset,
		size_t length, void *ptr, int prot, int flags)
{
	cm_map_t *map = NULL;

	map = (cm_map_t *) calloc(1, sizeof(cm_map_t));
	if (map)
	{
		map->cm_handle = (cm_handle_t) calloc(1, HANDLESIZE);
		if (!map->cm_handle)
		{
			free(map);
			map = NULL;
		}
		else
		{
			memcpy(map->cm_handle, p, HANDLESIZE);
			map->cm_offset = offset;
			map->cm_size = length;
			map->cm_ptr = ptr;
			map->cm_prot = prot;
			map->cm_flags = flags;
			map->cm_faulted_flags = (unsigned char *) calloc(length / PAGESIZE, sizeof(char));
			if (map->cm_faulted_flags == NULL)
			{
				free(map->cm_handle);
				free(map);
				map = NULL;
			}
		}
	}
	return map;
}

void cmgr_map_dealloc(cm_map_t *map)
{
	if (map)
	{
		free(map->cm_faulted_flags);
		free(map->cm_handle);
		free(map);
	}
	return;
}

/*
 * Support for memory mapping of files that are coherent
 * with the file cache! Caller will be mmap() & family of 
 * functions. Currently we don't support mremap().
 */
int CMGR_add_mappings(cm_handle_t p, unsigned long offset,
		size_t length, void *ptr, int prot, int flags)
{
	cm_file_t *filp;
	int err = -EINVAL;

	dprintf("Adding mappings for %p of size %d\n",
			ptr, length);
	filp = CMGRINT_file_get(p);
	/* NOTE: filp is now locked */
	if (filp)
	{
		cm_map_t *map = NULL;
		/* 
		 * We add the mappings to the file list,
		 * and keep it sorted on file offset. In addition,
		 * we also need to add the mapping structure to a 
		 * a red-black tree for efficient search based on the 
		 * pointer value as the key.
		 */
		map = rb_find_map((unsigned long) ptr);
		/* if there is already a mapping for this address, then we go BUG! */
		if (map != NULL)
		{
			err = -EINVAL;
			panic("We already have a mapping for this address %p\n", ptr);
		}
		else
		{
			/* try to allocate a structure for this mapping */
			map = cmgr_map_alloc(p, offset, length, ptr, prot, flags);
			/* if memory allocation failed */
			if (map == NULL)
			{
				err = -ENOMEM;
			}
			else
			{
				/* add it to the file mapping list */
				cmgr_file_add_mapping(map, filp);
				/* add it to the red-black tree as well */
				rb_insert_map(map);
				err = 0;
			}
		}
		CMGRINT_file_put(filp);
	}
	return err;
}

int CMGR_del_mappings(void *ptr, size_t length)
{
	cm_map_t *map = NULL;
	int err = -EINVAL;
	/*
	 * lookup the mmap table and get the mapping information.
	 * based on whether this region was opened with MAP_SHARED
	 * or not, we should update the cache also if any of the
	 * pages were dirtied.
	 */
	map = rb_delete_map((unsigned long) ptr, length);
	if (map != NULL && map->cm_handle != NULL)
	{
		cm_file_t *filp = NULL;

		err = -ENOMEM;
		/* obtain a lock on the backing file for this mapping */
		filp = CMGRINT_file_get(map->cm_handle);
		if (filp)
		{
			cmgr_file_del_mapping(map);
			/* and deallocate the mapped information structure */
			cmgr_map_dealloc(map);
			CMGRINT_file_put(filp);
		}
	}
	return err;
}

/*
 * Called from the SIGSEGV signal handler. 
 * What we need to do here is lookup this address in the mmap()
 * tables, find out the mode in which this address had been mapped in
 * as and do the following operations.
 * In any case, we may not be able to copy the data to the mapped
 * virtual address, since that would recursively fault endlessly. Instead
 * we issue a flush of the cache blocks that have been dirtied,
 * and let the mmap routine pick up the data by making the address
 * load/store fault once more.
 */
int CMGR_fixup_mappings(unsigned long fault_address)
{
	cm_map_t *map = NULL;
	unsigned long fault_page;
	int flags, prot;

	/* Find out the mapping structure for the page-faulted address */
	map = rb_find_map(fault_address);
	if (map == NULL)
	{
		panic("mmap: We don't have a mapping for this address! %lx\n", 
				fault_address);
		exit(1);
	}
	/* Align it to the nearest page boundary */
	fault_page = PAGE_ALIGN(fault_address);
	dprintf("Faulted address: %lx, faulted page address = %lx\n",
			fault_address, fault_page);
	/* obtain the mapping flags, protection bits etc for this mapping */
	flags = map->cm_flags;
	prot = map->cm_prot;
	{
		cmgr_synch_options_t options;
		options.cs_evict = 0;
		options.cs_opt.keep.wb = 1;
		options.cs_opt.keep.synch = CM_INVALIDATE_SYNCH;

		/* Issue a file sync, and invalidate it depending upon prot */
		if ((prot & PROT_READ) && !(prot & PROT_WRITE))
		{
			options.cs_opt.keep.synch = CM_DONT_SYNCH;
		}
		CMGR_synch_region(map->cm_handle, 0, -1, &options);
		/* Now restore the permissions on the page */
		if (mprotect((void *)fault_page, PAGESIZE, prot) < 0)
		{
			panic("mmap: mprotect on %lx with prot=%d failed: %s\n",
					fault_page, prot, strerror(errno));
			exit(1);
		}
	}
	return 0;
}

static inline int find_set_of_overlapped_blocks(cm_page_t begin1,
		cm_page_t count1, cm_page_t begin2, cm_page_t count2, 
		cm_page_t *begin3, cm_page_t *count3)
{
	*begin3 = max(begin1, begin2);
	*count3 = min(count1, count2);
	return 0;
}

/*
 * Given a starting file block and the number of blocks, find out all
 * the mmap'ed pages and return them
 */
static int convert_file_blocks_to_mem_pages(cm_file_t *filp,
		cm_page_t begin_block, cm_page_t num_blocks, int *page_count, 
		unsigned long **begin_page, unsigned long **num_pages)
{
	/* walk thru filp's list of blocks and see if we can find any mmappers */
	struct qlist_head *tmp;
	unsigned long *bpage = NULL, *npages = NULL;
	
	*page_count = 0;
	assert(num_blocks > 0);
	qlist_for_each (tmp, &filp->cf_map)
	{
		cm_map_t *map = NULL;
		cm_page_t map_block = 0, map_num_blocks = 0;
		cm_page_t overlapped_block = 0, overlapped_num_blocks = 0;
		unsigned long map_page_start = 0, map_page_count = 0;

		map = qlist_entry(tmp, cm_map_t, cm_next);
		map_block = map->cm_offset >> LOG_BSIZE;
		map_num_blocks = ((map->cm_offset + map->cm_size - 1) >> LOG_BSIZE) 
								- map_block + 1;
		assert(map_num_blocks > 0);
		/* handle the easy case of non-overlap first */
		if ((map_block + map_num_blocks - 1) < begin_block
				|| (begin_block + num_blocks - 1) < map_block)
		{
			continue;
		}
		/* find the overlapping set of file blocks first */
		find_set_of_overlapped_blocks(begin_block, num_blocks,
				map_block, map_num_blocks, &overlapped_block, &overlapped_num_blocks);
		/* convert that to a set of mmapp'ed virtual addresses */
		if (LOG_BSIZE > PAGESHIFT)
		{
			map_page_start = (overlapped_block << (LOG_BSIZE - PAGESHIFT));
			map_page_count = (overlapped_num_blocks << (LOG_BSIZE - PAGESHIFT));
		}
		else
		{
			map_page_start = (overlapped_block >> (PAGESHIFT - LOG_BSIZE));
			map_page_count = (overlapped_num_blocks >> (PAGESHIFT - LOG_BSIZE));
		}
		assert(map_page_count > 0);
		(*page_count)++;
		bpage = (unsigned long *) realloc(bpage, (*page_count * sizeof(unsigned long)));
		if (bpage == NULL)
		{
			free(npages);
			(*page_count) = -1;
			*begin_page = NULL;
			*num_pages = NULL;
			return -ENOMEM;
		}
		npages = (unsigned long *) realloc(npages, (*page_count * sizeof(unsigned long)));
		if (npages == NULL)
		{
			free(bpage);
			(*page_count) = -1;
			*begin_page = NULL;
			*num_pages = NULL;
			return -ENOMEM;
		}
		bpage[(*page_count)-1] = map_page_start;
		npages[(*page_count)-1] = map_page_count;
	}
	*begin_page = bpage;
	*num_pages = npages;
	return 0;
}

/*
 * called from the write routine, that dirties the user-level cache,
 * and in order to support coherent mmap'ed execution, we need to mprotect
 * those regions of virtual addresses so that they could be made to fault again.
 */
int cmgr_invalidate_mappings(cm_handle_t p, cm_page_t begin_block, 
		cm_page_t num_blocks)
{
	cm_file_t *filp = NULL;
	int err = -EINVAL;

	filp = CMGRINT_file_get(p);
	if (filp)
	{
		int page_count = 0, i;
		unsigned long *begin_page = NULL, *num_pages = NULL;

		if ((err = convert_file_blocks_to_mem_pages(filp, begin_block, num_blocks,
				&page_count, &begin_page, &num_pages)) < 0)
		{
			goto out;
		}
		/* no mmappers is not an error */
		if (page_count == 0)
		{
			err = 0;
			goto out;
		}
		err = 0;
		for (i = 0; i < page_count; i++)
		{
			if ((err = mprotect((void *) begin_page[i], 
							num_pages[i] * PAGESIZE, PROT_NONE)) < 0)
			{
				panic("invalidate_mmap: mprotect on %lx with prot=PROT_NONE failed: %s\n",
						begin_page[i], strerror(errno));
			}
		}
		/* caller's responsibility to free up */
		free(begin_page);
		free(num_pages);
out:
		CMGRINT_file_put(filp);
	}
	return err;
}

int CMGR_sync_mappings(unsigned long start, size_t length, int flags)
{
	/* 
	 * TODO: Add infrastructure to flush/invalidate portions of the
	 * file rather than the whole file since that is wasteful.
	 */
	cm_map_t *map = NULL;
	cmgr_synch_options_t options;

	/* Find out the mapping structure for the start address */
	map = rb_find_map(start);
	if (map == NULL)
	{
		panic("mmap: We don't have a mapping for this address! %lx\n", 
				(unsigned long) start);
		exit(1);
	}
	options.cs_evict = 0;
	options.cs_opt.keep.wb = 1;
	options.cs_opt.keep.synch = CM_INVALIDATE_SYNCH;
	/* Invalidate the file blocks */
	CMGR_synch_region(map->cm_handle, 0, -1, &options);
	return 0;
}

int CMGR_remap_mappings(void *old_address, size_t old_size, size_t new_size,
		int flags)
{
	cm_map_t *map = NULL;
	unsigned char *new_faulted_flags = NULL;

	/* Find out the mapping structure for the page-faulted address */
	map = rb_find_map((unsigned long) old_address);
	if (map == NULL)
	{
		panic("mmap: We don't have a mapping for this address! %p\n", 
				old_address);
		exit(1);
	}
	/* cannot handle unequal mapping lengths */
	assert(map->cm_size == old_size);
	new_faulted_flags = (unsigned char *) calloc(1, new_size / PAGESIZE);
	if (new_faulted_flags == NULL)
	{
		panic("mremap: We could not allocate memory\n");
		exit(1);
	}
	memcpy(new_faulted_flags, map->cm_faulted_flags, map->cm_size / PAGESIZE);
	map->cm_size = new_size;
	free(map->cm_faulted_flags);
	map->cm_faulted_flags = new_faulted_flags;
	return 0;
}

int cmgr_mmap_init(void)
{
	return 0;
}

void cmgr_mmap_finalize(void)
{
	return;
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

