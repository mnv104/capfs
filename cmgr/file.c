#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "cmgr_internal.h"
#include "mquickhash.h"
#include "gen-locks.h"

/* file hash & comparison routines */
static comp_fn compare_file_ptr = NULL;
static hash_fn hash_file_ptr = NULL;

static struct mqhash_table *cm_file_table = NULL; /* hash table of all files */

/* file comparison and hash routines */
static int compare_file(void *key, struct mqhash_head *entry)
{
	cm_file_t *f2 = NULL;
	cm_handle_t f1 = (cm_handle_t) key;

	Assert(compare_file_ptr);
	f2 = qlist_entry(entry, cm_file_t, cf_hash);
	return compare_file_ptr(f1, f2->cf_handle);
}

static int file_hash(void *key, int table_size)
{
	int hash_value;

	Assert(hash_file_ptr);
	hash_value = hash_file_ptr(key); 
	return hash_value % table_size;
}

#define lock_filp(x) \
	do {\
		int ret;\
		lock_printf("FILP lock %p\n", (x));\
		if ((ret = pthread_mutex_trylock(&((x)->cf_lock))) != 0) {\
			lock_printf("FILP lock %p is going to BLOCK!\n", (x));\
			pthread_mutex_lock(&((x)->cf_lock));\
		}\
	} while (0);

#define unlock_filp(x) \
	do {\
		lock_printf("FILP unlock %p\n", (x));\
		pthread_mutex_unlock(&(x)->cf_lock);\
	} while (0);

static void dump_filp(cm_file_t *filp) __attribute__((unused));

static void dump_filp(cm_file_t *filp)
{
	lock_printf("filp->handle = %Ld\n", *((int64_t *) filp->cf_handle));
	lock_printf("filp->cf_ref = %d\n", filp->cf_ref);
	lock_printf("filp->cf_error = %d\n", filp->cf_error);
	lock_printf("filp->cf_list = %p, %p\n", filp->cf_list.next, filp->cf_list.prev);
	lock_printf("filp->cf_hash = %p, %p\n", filp->cf_hash.next, filp->cf_hash.prev);
	return;
}

static int cm_hashes_ctor(struct cm_file_hashes *pcm)
{
    if (pcm)
    {
	/* allocate memory for a big hashes array */
	pcm->cm_phashes = (struct hash_entry *) calloc(BCOUNT, sizeof(struct hash_entry));
	if (pcm->cm_phashes == NULL)
	{
		return -ENOMEM;
	}
	pcm->cm_nhashes = BCOUNT;
	return 0;
    }
    else
    {
	return -EINVAL;
    }
}

static void cm_hashes_dtor(struct cm_file_hashes *pcm)
{
    if (pcm && pcm->cm_phashes)
    {
	pcm->cm_nhashes = 0;
	free(pcm->cm_phashes);
	pcm->cm_phashes = NULL;
    }
    return;
}

static cm_file_t* cmgr_file_alloc(cm_handle_t handle, int obtain_lock)
{
	cm_file_t  *p;

	p = (cm_file_t *)calloc(1, sizeof(*p));
	if (p) 
	{
		pthread_mutex_init(&p->cf_lock, NULL);
		if (obtain_lock == 1)
		    lock_filp(p);
		p->cf_handle = (cm_handle_t) calloc(1, HANDLESIZE);
		if (!p->cf_handle) {
			free(p);
			return NULL;
		}
		memcpy(p->cf_handle, handle, HANDLESIZE);
		INIT_QLIST_HEAD(&p->cf_list);
		/* Part of the simpler hcache design */
		if (cm_hashes_ctor(&p->cf_hashes) < 0)
		{
			free(p->cf_handle);
			free(p);
			return NULL;
		}
		/* pin this filp. this is also a part of the simpler hcache design */
		p->cf_pin = 1;
#ifdef MMAP_SUPPORT
		INIT_QLIST_HEAD(&p->cf_map);
#endif
		p->cf_ref = 0;
	}
	else 
	{
		panic("Could not allocate cm_file_t structure!\n");
	}
	lock_printf("FILP alloc %p\n", p);
	return p;
}

static void cmgr_file_dealloc(cm_file_t *filp)
{
	if (filp && filp->cf_handle) 
	{
		free(filp->cf_handle);
	}
	cm_hashes_dtor(&filp->cf_hashes);
	pthread_mutex_trylock(&filp->cf_lock);
#ifdef MMAP_SUPPORT
	/* now we need to walk the mmap list and free the cm_map_t structures */
	while (filp->cf_map.next != &filp->cf_map)
	{
		cm_map_t *map;

		map = qlist_entry(filp->cf_map.next, cm_map_t, cm_next);
		qlist_del(filp->cf_map.next);
		cmgr_map_dealloc(map);
	}
#endif
	pthread_mutex_unlock(&filp->cf_lock);
	lock_printf("FILP dealloc %p\n", filp);
	free(filp);
	return;
}

/* Assumes we have a lock on file->cf_lock */

void CMGRINT_file_put(cm_file_t *filp)
{
	/* if we have to propogate any failed writeback errors, or if there any refs we dont disappear */
	if (filp->cf_ref == 1 
		&& !filp->cf_error 
		&& qlist_empty(&filp->cf_list)
		&& filp->cf_pin == 0
#ifdef MMAP_SUPPORT
		&& qlist_empty(&filp->cf_map)
#endif
		) 
	{
		/* We also need to delete ourselves from the hash table */
		int hindex;

		hindex = cm_file_table->hash(filp->cf_handle, cm_file_table->table_size);
		/* drop filp's lock and try and delete it from the file hash tables */
		unlock_filp(filp);

		/* Acquire hash chain's lock in WRITE mode */
		mqhash_wrlock(&cm_file_table->lock[hindex]);
		/* re-acquire filp's lock and re-check if it can be de-allocated */
		lock_filp(filp);
		if (filp->cf_ref == 1
			&& !filp->cf_error
			&& qlist_empty(&filp->cf_list)
#ifdef MMAP_SUPPORT
			&& qlist_empty(&filp->cf_map)
#endif
			) 
		{
			/* delete it from the hash chain */
			qlist_del(&filp->cf_hash);
			/* now we drop our own reference to filp */
			filp->cf_ref--;
			lock_printf("FILP put %p (%d)\n", filp, filp->cf_ref);
			/* deallocate filp structure */
			cmgr_file_dealloc(filp);
			/* unlock the hash chain */
			mqhash_unlock(&cm_file_table->lock[hindex]);
			return;
		}
		/* we raced! can't drop filp */
		filp->cf_ref--;
		lock_printf("FILP put %p (%d)\n", filp, filp->cf_ref);
		unlock_filp(filp);
		/* unlock the hash chain */
		mqhash_unlock(&cm_file_table->lock[hindex]);
		return;
	}
	filp->cf_ref--;
	lock_printf("FILP put %p (%d)\n", filp, filp->cf_ref);
	/* just unlock and return */
	unlock_filp(filp);
	return;
}

#define CHAIN_READ  0
#define CHAIN_WRITE 1

/* Search for the file handle in the hash table and return it with the lock held if need be */

cm_file_t* CMGRINT_file_get(cm_handle_t p)
{
	cm_file_t *file = NULL;
	struct mqhash_head *ent;
	int hindex, mode = CHAIN_READ;

	hindex = cm_file_table->hash(p, cm_file_table->table_size);

retry:
	if (mode == CHAIN_READ) 
	{
		/*  hold on to the read lock of the hash chain */
		mqhash_rdlock(&cm_file_table->lock[hindex]);
	}
	else if (mode == CHAIN_WRITE) 
	{
		/*  hold on to the write lock of the hash chain */
		mqhash_wrlock(&cm_file_table->lock[hindex]);
	}
	mqhash_for_each (ent, &(cm_file_table->array[hindex])) 
	{
		if (cm_file_table->compare(p, ent))  /* matches */
		{
			file = qlist_entry(ent, cm_file_t, cf_hash);
			lock_filp(file);
			break;
		}
	}
	if (!file) 
	{
		if (mode == CHAIN_READ) 
		{
			/* reissue the search with mode set to CHAIN_WRITE */
			mode = CHAIN_WRITE;
			/* unlock the hash chain */
			mqhash_unlock(&cm_file_table->lock[hindex]);
			goto retry;
		}
		else if (mode == CHAIN_WRITE) 
		{
			/* We have to allocate a file object and insert it in the hash chain */
			file = cmgr_file_alloc(p, 1);
			if (file) 
			{
				/* add it to the file hash chain */
				mqhash_add(cm_file_table, p, &file->cf_hash);
				file->cf_ref++;
				lock_printf("FILP get %p (%d)\n", file, file->cf_ref);
			}
		}
	}
	else
	{
		if (file->cf_hashes.cm_phashes == NULL)
		{
			/* Darn */
			if (cm_hashes_ctor(&file->cf_hashes) < 0)
			{
				unlock_filp(file);
				/* unlock the hash chain */
				mqhash_unlock(&cm_file_table->lock[hindex]);
				return NULL;
			}
		}
		file->cf_ref++;
		lock_printf("FILP get %p (%d)\n", file, file->cf_ref);
	}
	/* unlock the hash chain */
	mqhash_unlock(&cm_file_table->lock[hindex]);

	return file; /* if !NULL this structure is locked */
}

static void lockless_cmgr_file_put(cm_file_t *filp)
{
	filp->cf_ref--;
	lock_printf("FILP put %p (%d)\n", filp, filp->cf_ref);
	return;
}

/* This function tries toget a reference to a filp without attempting to lock it */
static cm_file_t* lockless_cmgr_file_get(cm_handle_t p)
{
	cm_file_t *file = NULL;
	struct mqhash_head *ent;
	int hindex, mode = CHAIN_READ;

	hindex = cm_file_table->hash(p, cm_file_table->table_size);
retry:
	if (mode == CHAIN_READ) 
	{
		/*  hold on to the read lock of the hash chain */
		mqhash_rdlock(&cm_file_table->lock[hindex]);
	}
	else if (mode == CHAIN_WRITE) 
	{
		/*  hold on to the write lock of the hash chain */
		mqhash_wrlock(&cm_file_table->lock[hindex]);
	}
	mqhash_for_each (ent, &(cm_file_table->array[hindex])) 
	{
		if (cm_file_table->compare(p, ent))  /* matches */
		{
			file = qlist_entry(ent, cm_file_t, cf_hash);
			break;
		}
	}
	if (!file) 
	{
		if (mode == CHAIN_READ) 
		{
			/* reissue the search with mode set to CHAIN_WRITE */
			mode = CHAIN_WRITE;
			/* unlock the hash chain */
			mqhash_unlock(&cm_file_table->lock[hindex]);
			goto retry;
		}
		else if (mode == CHAIN_WRITE) 
		{
			/* We have to allocate a file object and insert it in the hash chain */
			file = cmgr_file_alloc(p, 0);
			if (file) 
			{
				/* add it to the file hash chain */
				mqhash_add(cm_file_table, p, &file->cf_hash);
				file->cf_ref++;
				lock_printf("FILP get %p (%d)\n", file, file->cf_ref);
			}
		}
	}
	else
	{
		if (file->cf_hashes.cm_phashes == NULL)
		{
			/* Darn */
			if (cm_hashes_ctor(&file->cf_hashes) < 0)
			{
				/* unlock the hash chain */
				mqhash_unlock(&cm_file_table->lock[hindex]);
				return NULL;
			}
		}
		file->cf_ref++;
		lock_printf("FILP get %p (%d)\n", file, file->cf_ref);
	}
	/* unlock the hash chain */
	mqhash_unlock(&cm_file_table->lock[hindex]);

	return file;
}


/* This function tries to remove space allocated to cf_hashes. Assumes that we have already obtained a lock */
void CMGRINT_file_freespace(cm_file_t *filp)
{
	if (filp && filp->cf_hashes.cm_phashes)
	{
	    free(filp->cf_hashes.cm_phashes);
	    filp->cf_hashes.cm_phashes = NULL;
	    filp->cf_hashes.cm_nhashes = 0;
	}
}

/* 
 * This functions tries to re-allocate space allocated to cf_hashes.cm_phashes. 
 * Assumes that we have already obtained a lock 
 */
int CMGRINT_file_resize(cm_file_t *filp, int64_t nchunks)
{
	/* allocate only if there is a need to */
	if (nchunks <= filp->cf_hashes.cm_nhashes)
	{
		return 0;
	}
	filp->cf_hashes.cm_phashes = (struct hash_entry *) 
	    realloc(filp->cf_hashes.cm_phashes, (nchunks * sizeof(struct hash_entry)));
	if (filp->cf_hashes.cm_phashes == NULL)
	{
		filp->cf_hashes.cm_nhashes = 0;
		return -ENOMEM;
	}
	filp->cf_hashes.cm_nhashes = nchunks;
	return 0;
}

/* Assumes that we have a lock on the filp() structure */
void CMGRINT_file_add(cm_frame_t *p, cm_file_t *filp)
{
	/* Add it to the file list */
	qlist_add_tail(&p->cm_file, &filp->cf_list);
	/* mark that we are now on a file list */
	SetPageFile(p);
	return;
}

#ifdef MMAP_SUPPORT
/* Assumes that we have a lock on the filp() structure */
void cmgr_file_add_mapping(cm_map_t *map, cm_file_t *filp)
{
	struct qlist_head *tmp = NULL;

	qlist_for_each (tmp, &filp->cf_map)
	{
		cm_map_t *entry = NULL;
		
		entry = qlist_entry(tmp, cm_map_t, cm_next);
		if ((unsigned long) entry->cm_ptr >= (unsigned long) map->cm_ptr)
			break;
		/* make sure that this list is non-overlapping in file offsets */
		assert(((unsigned long) map->cm_ptr + map->cm_size) < (unsigned long) entry->cm_ptr
				 || ((unsigned long) entry->cm_ptr + entry->cm_size) < (unsigned long) map->cm_ptr);
	}
	__qlist_add(&map->cm_next,tmp->prev, tmp);
	return;
}

/* Assumes that we have a lock on the filp structure */
void cmgr_file_del_mapping(cm_map_t *map)
{
	qlist_del(&map->cm_next);
}
#endif

/* Note that we come here with p locked and we return with no lock on p */
void CMGRINT_file_del(cm_frame_t *p, int failed_wb)
{
	cm_file_t *filp = NULL;
	cm_handle_t handle = NULL;
	int handle_alloc = 1;

	/* not a part of any file list */
	if (!Page_File(p)) 
	{
		unlock_page(p);
		return;
	}
	handle = (cm_handle_t )calloc(1, HANDLESIZE);
	if (!handle) 
	{
		handle_alloc = 0;
		handle = p->cm_block.cb_handle;
	}
	else {
		memcpy(handle, p->cm_block.cb_handle, HANDLESIZE);
	}
	/*
	 * Note that since we are assured at this point
	 * that no new block consumers could get this
	 * block, the handle of this block cannot change
	 * but for paranoid sake, I am memcopying it.
	 * The only people with whom this function(called
	 * by the harvester thread) can race are the file
	 * syncer's and file invalidaters who already have
	 * the lock on the file hash chain and are walking
	 * down the list of pages/blocks of the file
	 */
	/* drop the lock on p */
	unlock_page(p);
	/* Try to get a write lock on the file */
	if ((filp = CMGRINT_file_get(handle)) == NULL) 
	{
		panic("Cannot delete block from a NULL filp list\n");
		exit(1);
	}
	if (handle_alloc)
		free(handle);
	/* store the errors from any failed write backs */
	filp->cf_error = failed_wb;
	/* re-obtain the write lock on p */
	lock_page(p);
	/* recheck if we can delete p from the file list and do so only if it is still on it */
	if (Page_File(p)) 
	{
		dprintf("Deleting %u -> %Lu from file list\n",
			p->cm_id, p->cm_block.cb_page);
		qlist_del(&p->cm_file);
		ClearPageFile(p);
	}
	else {
		dprintf("Frame %u must have been removed from file list by truncate() or unlink()\
			or WB-on-close\n", p->cm_id);
	}
	unlock_page(p);
	/* filp may or may not be valid anymore after the put() */
	CMGRINT_file_put(filp);
	return;
}

/* 
 * Helper function that tries to evict a range of blocks
 * of a certain file from the cache or atleast marks them 
 * as candidates for eviction.
 * NOTE: What this function does is mark all those blocks
 * that fall outside of the range <start, start+new_size>
 * as candidates for eviction, not the ones that fall within
 * this range!
 * Special case if start == 0 && new_size == -1, then evict
 * all the blocks of this file.
 */
static int evict_blocks(cm_handle_t p, int64_t start, int64_t size)
{
	int err = -EINVAL;
	cm_file_t *filp = NULL;
	int64_t begin = 0, end = 0;
	size_t remaining[2] = {0}, where[2] = {0};
	int log_bsize = LOG_BSIZE;

	/* Ascertain if it needs to be applied for the whole file or not */
	if (start == 0 && size == -1)
	{
		end = -1;
	}
	else
	{
		Assert(start >= 0 && size >= 0);
		if (log_bsize < 0) {
		    begin = (start / BSIZE);
		    end = (start + size - 1) / BSIZE;
		}
		else {
		    begin = (start >> log_bsize);
		    end   = (start + size - 1) >> log_bsize;
		}
		where[0] = 0;
		if (log_bsize < 0) {
		    remaining[0] = start - (begin * BSIZE);
		    where[1] = (start + size) % BSIZE;
		}
		else {
		    remaining[0] = start - (begin << log_bsize);
		    where[1] = (start + size) & (BSIZE - 1);
		}
		remaining[1] = BSIZE - where[1];
		if (remaining[1] == BSIZE)
		{
		    remaining[1] = 0;
		}
		/* 
		 * We may have to zero out the first & last pages.
		 * i.e from <where[0], remaining[0]>
		 * and <where[1], remaining[1]>
		 */
	}
	filp = CMGRINT_file_get(p);
	/* filp is now LOCKED */
	if (filp)
	{
		struct qlist_head *entry = NULL;

		for (entry = filp->cf_list.next; entry != &filp->cf_list;)
		{
			cm_frame_t *fr = NULL;

			fr = qlist_entry(entry, cm_frame_t, cm_file);
			lock_page(fr);
			/* Mark these pages for eviction */
			if (end == -1 
				|| fr->cm_block.cb_page < begin 
				|| fr->cm_block.cb_page > end)
			{
				/* Clear the Dirty bit. We dont want this written back */
				ClearPageDirty(fr);
				/* Make sure that we are still on the file list */
				Assert(Page_File(fr));
				/* delete it from the file list */
				dprintf("Evicting %u -> %Lu from file list\n", 
					fr->cm_id, fr->cm_block.cb_page);
				qlist_del(&fr->cm_file);
				/* not a part of any file list */
				ClearPageFile(fr);
				/* mark it invalid as well */
				SetPageInvalid(fr);
				EVICTS++;
			}
			/* zero out any remnant on the first block */
			if (fr->cm_block.cb_page == begin)
			{
				memset(fr->cm_buffer + where[0], 0, remaining[0]);
			}
			/* zero out any remnant on the last block */
			if (fr->cm_block.cb_page == end)
			{
				memset(fr->cm_buffer + where[1], 0, remaining[1]);
			}
			/* else remaining pages are valid */
			entry = entry->next;
			/* drop the page lock */
			unlock_page(fr);
		}
		CMGRINT_file_put(filp);
		err = 0;
	}
	return err;
}


static void synch_page(cm_frame_t *fr, synch_options_t synch)
{
	/* No need to synchronize */
	if (synch == CM_DONT_SYNCH)
	{
		return;
	}
	else if (synch == CM_INVALIDATE_SYNCH) 
	{
		/* Mark it clean as well */
		ClearPageDirty(fr);
		/* Mark it as not uptodate */
		ClearPageUptodate(fr);
		INVALIDATES++;
	}
	/* Any other synchronization method can come in here */
	return;
}

/*
 * This helper function tries to
 * writeback specified blocks of a file
 * and/or synchronizes the cached blocks with the server
 * according to the specified synch method.
 * Currently the only implemented synchronization
 * method is to invalidate the local cache contents.
 *
 * if wb == 0 && synch == CM_DONT_SYNCH
 *  this function returns immediately. noop.
 * if wb == 0 && synch == CM_INVALIDATE_SYNCH
 *  this function marks the specified blocks by clearing the UptoDate flag
 *  on the frame that would cause subsequent references to issue a fetch
 *  for this page.
 * if wb == 1 && synch == CM_DONT_SYNCH,
 *  this function tries to writeback the dirty pages alone.
 * if wb == 1 && synch == CM_INVALIDATE_SYNCH,
 *  it not only tries to writeback the dirty blocks, but also
 *  marks them as not UptoDate, so that subsequent references get re-fetched.
 */
static int synch_blocks(cm_handle_t p, int64_t start, int64_t size,
	int wb, synch_options_t synch, int obtain_lock)
{
	int64_t begin = 0, end = 0;
	cm_file_t *filp = NULL;
	int err = -EINVAL;
	int log_bsize = LOG_BSIZE;

	/* make sure all parameters are sane */
	Assert((wb == 0) || (wb == 1));
	Assert((synch == CM_DONT_SYNCH) || (synch == CM_INVALIDATE_SYNCH));

	/* Check for no-ops */
	if (wb == 0 && synch == CM_DONT_SYNCH)
	{
		return 0;
	}
	if (start == 0 && size == -1)
	{
		end = -1;
	}
	else
	{
		if (log_bsize < 0) {
		    begin = start / BSIZE;
		    end = (start + size - 1) / BSIZE;
		}
		else {
		    begin = (start >> log_bsize);
		    end = (start + size - 1) >> log_bsize;
		}
	}
	filp = CMGRINT_file_get(p);
	/* NOTE filp is now locked */
	if (filp)
	{
		int failed_wb = 0, i;
		struct qlist_head *entry = NULL;
		cm_frame_t **all_frames = NULL;
		int nframes = 0, ret = 0;
		int64_t tot = 0;

		/* propogate previous errors if any and if asked for */
		if (wb == 1 && filp->cf_error) 
		{
			failed_wb = 1;
			err = filp->cf_error;
			filp->cf_error = 0;
		}
		/* walk thru the list of blocks for this file */
		nframes = 0;
		for (entry = filp->cf_list.next; entry != &filp->cf_list;) 
		{
			cm_frame_t *fr = NULL;

			fr = qlist_entry(entry, cm_frame_t, cm_file);
			if (obtain_lock == 1)
				lock_page(fr);
			/* Do operation only for specified blocks */
			if (end == -1 || 
				(fr->cm_block.cb_page >= begin 
				 && fr->cm_block.cb_page <= end))
			{
				/* If writeback is necessary and asked for */
				if (wb == 1 && Page_Dirty(fr))
				{
					nframes++;
					all_frames = (cm_frame_t **)
					    realloc(all_frames, nframes * sizeof(cm_frame_t *));
					Assert(all_frames);
					all_frames[nframes - 1] = fr;
				}
				else
				{
					synch_page(fr, synch);
					if (obtain_lock == 1)
						unlock_page(fr);
				}
			}
			else
			{
				if (obtain_lock == 1)
					unlock_page(fr);
			}
			/* skip to the next page */
			entry = entry->next;
		}
		/* call the writepages() method if there is a need to */
		if (wb == 1 && nframes > 0
			&& all_frames)
		{
			ret = __CMGRINT_wb_sync(nframes, all_frames);
			if (ret < 0)
			{
				failed_wb = 1;
				err = ret;
			}
			else
			{   
				tot = ret;
			}
			for (i = 0; i < nframes; i++)
			{
				/* mark it clean */
				ClearPageDirty(all_frames[i]);
				/* Synchronize it if need be */
				synch_page(all_frames[i], synch);
				/* drop the lock on the page */
				if (obtain_lock == 1)
					unlock_page(all_frames[i]);
			}
		}
		/* drop the lock on the file */
		CMGRINT_file_put(filp);
		if (wb == 1 && failed_wb == 0) 
		{
			dprintf("on file close wroteback %Lu bytes\n", tot);
			err = 0;
		}
		if (all_frames)
		{
			free(all_frames);
		}
	}
	return err;
}

/*
 * FIXME: Do we really need to use the blocking variable here?
 * Yup, we do
 */
int CMGR_simple_synch_region(cm_handle_t p, int64_t begin_chunk, int64_t nchunks,
	cmgr_synch_options_t *options, int blocking)
{
	cm_file_t *filp = NULL;
    
	if (blocking == 1)
	{
	    filp = CMGRINT_file_get(p);
	}
	else
	{
	    filp = lockless_cmgr_file_get(p);
	}
	if (filp)
	{
		/* In the simpler design, all we do is free space of
		*  cf_hashes */
		if (options->cs_evict)
		{
			CMGRINT_file_freespace(filp);
		}
		else
		{
			/* We mark the requested regions as invalid
			*  */
			int64_t i;

			for (i = 0; i < nchunks; i++)
			{
				/* make sure we have even allocated space for it */
				if ((begin_chunk + i) < filp->cf_hashes.cm_nhashes)
				{
					/* mark as invalid */
					filp->cf_hashes.cm_phashes[begin_chunk + i].h_valid = 0;
				}
			}
		}
		if (blocking == 1)
		{
		    CMGRINT_file_put(filp);
		}
		else
		{
		    lockless_cmgr_file_put(filp);
		}
		return 0;
	}
	return -ESRCH;
}

/*
 * This is the API that is going to be made visible
 * for explicit synchronization of the locally cached
 * file with that of the server.
 * For a given file, and a given extent within the file,
 * synchronize the contents of the cache according to 
 * the options specified.
 * To specify the whole file, one would set start to 0
 * and new_size to -1.
 * KLUDGE:
 * This routine will not attempt to take any locks on the blocks!
 * This is needed because the hcache code is deadlocking
 * properly when a hcache_clear_range() callback is racing with
 * a hcache_get(). Clearly, for correctness it is ok if this routine
 * does not grab the locks since the hcache_get() will anyway get the right
 * set of hashes!!
 * This implies that synch_blocks() need not obtain the lock on the page
 * frames!
 */
int CMGR_synch_region(cm_handle_t p, int64_t start, int64_t size,
	cmgr_synch_options_t *options, int blocking)
{
	/* Summarily evict all the specified blocks */
	if (options->cs_evict)
	{
		/* Evict all blocks that fall outside of this range */
		return evict_blocks(p, start, size);
	}
	/* If asked to writeback and/or synchronize cached data */
	return synch_blocks(p, start, size, 
		options->cs_opt.keep.wb, options->cs_opt.keep.synch, blocking);
}

/* Initialize and cleanup routines */
int CMGRINT_file_init(cmgr_options_t *options, int file_table_size)
{
	/* Stash away the function pointers for later use */
	compare_file_ptr = options->compare_file;
	hash_file_ptr    = options->file_hash;

	if ((cm_file_table = mqhash_init(compare_file, file_hash, file_table_size)) 
				== NULL) 
	{
		panic("Could not allocate and initialize file hash tables\n");
		return -ENOMEM;
	}
	/* Initialize the mmap interface */
#ifdef MMAP_SUPPORT
	cmgr_mmap_init();
#endif
	return 0;
}

void CMGRINT_file_finalize(void)
{
	int ret = 0;

#ifdef MMAP_SUPPORT
	cmgr_mmap_finalize();
#endif
	for (ret = 0; ret < cm_file_table->table_size; ret++) 
	{
		struct mqhash_head *ent;

		mqhash_wrlock(&cm_file_table->lock[ret]);
		ent = &cm_file_table->array[ret];
		while (ent->next != ent) 
		{
			cm_file_t *file;

			file = qlist_entry(ent->next, cm_file_t, cf_hash);
			qlist_del(ent->next);
			cmgr_file_dealloc(file);
		}
		mqhash_unlock(&cm_file_table->lock[ret]);
	}
	mqhash_finalize(cm_file_table);
	compare_file_ptr = NULL;
	hash_file_ptr    = NULL;
	return;
}

void CMGRINT_simple_invalidate(void)
{
	int ret = 0;

	for (ret = 0; ret < cm_file_table->table_size; ret++) 
	{
		struct mqhash_head *ent;

		mqhash_wrlock(&cm_file_table->lock[ret]);
		qlist_for_each (ent, &(cm_file_table->array[ret]))
		{
			cm_file_t *file;

			file = qlist_entry(ent, cm_file_t, cf_hash);
			lock_filp(file);
			/* dealloc the hashes */
			cm_hashes_dtor(&file->cf_hashes);
			unlock_filp(file);
		}
		mqhash_unlock(&cm_file_table->lock[ret]);
	}
	return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */


