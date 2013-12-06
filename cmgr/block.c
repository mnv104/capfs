#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "cmgr_internal.h"
#include "mquickhash.h"
#include "gen-locks.h"

/* hash table of all cache blocks */
static struct mqhash_table *cm_block_table = NULL; 

/* block hash & comparison routines */
static comp_fn compare_block_ptr = NULL;
static hash_fn hash_block_ptr = NULL;

/* file hash & comparison routines */
static comp_fn compare_file_ptr = NULL;
static hash_fn hash_file_ptr = NULL;

static int compare_block(void *key, struct mqhash_head *entry)
{
	cm_block_t *f1 = (cm_block_t *) key;
	cm_frame_t *f2 = NULL;
	
	Assert(compare_file_ptr);
	f2 = qlist_entry(entry, cm_frame_t, cm_hash);
	/* check if the file is the same, and the virtual block is the same */
	if (compare_file_ptr(f1->cb_handle, f2->cm_block.cb_handle) == 1)
	{
	    int ret = -1;

	    if ((compare_block_ptr 
			&& (ret = compare_block_ptr(&f1->cb_page,
				&f2->cm_block.cb_page)) == 1)
		    || (ret < 0 && (f1->cb_page == f2->cm_block.cb_page)))
	    {
		    return 1;
	    }
	}
	return 0;
}

static int block_hash(void *key, int table_size)
{
	cm_block_t *f1 = (cm_block_t *)key;
	int hash_value;

	Assert(hash_file_ptr);
	hash_value = abs(hash2(hash_file_ptr(f1->cb_handle), f1->cb_page)); 
	return hash_value % table_size;
}

static cm_frame_t *cmgr_block_alloc(void)
{
	cm_frame_t *p = NULL;

	p = CMGRINT_wait_for_free();
	/* lock p */
	lock_page(p);
	/*
	 * make sure that the harvester thread does not touch us. 
	 * This operations is inherently safe from other threads
	 * since no one else could ever get a reference to this 
	 * frame except us, because it is freshly dequeued off
	 * the free list.
	 */
	p->cm_fix++;
	return p;
}

static void cmgr_block_release(cm_frame_t *fr)
{
	if (fr) 
	{
		/* fr is guaranteed to be unreachable. delete it from the file list */
		CMGRINT_file_del(fr, 0);
		/* the above function unlock frame and we dont need that anyway to return it to the free list */
		CMGRINT_mark_page_free(fr);
	}
}

/* Associate a file block to the cache frame that is assumed to be locked */
static int cmgr_block_fill(cm_frame_t *p, cm_block_t *block, void *private)
{
	cm_file_t  *filp;

	/* try & add it to the file hash table as well */
	if ((filp = CMGRINT_file_get(block->cb_handle)) == NULL) 
	{
		return -ENOMEM;
	}
	/* NOTE that filp is locked at this point as well */
	p->cm_block.cb_page = block->cb_page;
	p->cm_private = private;
	memcpy(p->cm_block.cb_handle, block->cb_handle, HANDLESIZE);
	/* add it to the file-block list as well */
	dprintf("Adding %u -> %Lu to file list\n", p->cm_id, p->cm_block.cb_page);
	CMGRINT_file_add(p, filp);
	/* unlock the filp structure */
	CMGRINT_file_put(filp);
	return 0;
}

void CMGRINT_block_put(cm_frame_t *p)
{
	if (Page_Dirty(p)) 
	{
		p->cm_ref += 2 * CM_GCLOCK_REF;
	}
	else 
	{
		p->cm_ref += CM_GCLOCK_REF;
	}
	if (p->cm_fix > 0)
	{
		p->cm_fix--;
	}
	unlock_page(p);
	return;
}

void CMGRINT_print_valid_regions(cm_frame_t *handle)
{
	int j;

	dprintf("**** valid_regions ****\n");
	dprintf("Handle (%d -> %Ld) has %d regions\n", 
		handle->cm_id, handle->cm_block.cb_page, 
		handle->cm_valid_count);
	for (j = 0; j < handle->cm_valid_count; j++)
	{
		dprintf("(%d): valid_start: %Ld, valid_size: %d\n",
			j, handle->cm_valid_start[j], 
			handle->cm_valid_size[j]);
	}
	dprintf("****			  ****\n");
	return;
}

static void do_find_indexes(cm_frame_t *handle, cm_pos_t valid_start,
		cm_size_t valid_size, int *valid_index)
{
	int j;

	for (j = 0; j < handle->cm_valid_count; j++) 
	{
		if (((valid_start + valid_size) < handle->cm_valid_start[j])
				|| ((handle->cm_valid_start[j] + handle->cm_valid_size[j]) < valid_start))
		{
			valid_index[j] = 0;
		}
		else
		{
			valid_index[j] = 1;
		}
	}
	return;
}

/* find a series of consecutive 1's */
static void find_set_of_overlapped_indexes(int *index, int count, 
		int *seq_start, int *seq_count)
{
	int i, flag = 0, sequence_count = 0;

	assert(count > 0);
	*seq_start = -1;
	*seq_count = 0;
	for (i = 0; i < count; i++)
	{
		if (index[i] == 1)
		{
			if (flag == 0) 
			{
				assert(sequence_count == 0);
				flag = 1;
				*seq_start = i;
			}
			(*seq_count)++;
		}
		else
		{
			if (flag == 1)
			{
				sequence_count++;
				flag = 0;
			}
		}
	}
	return;
}

static int find_which_index(cm_frame_t *handle, cm_pos_t valid_start, 
		cm_size_t valid_size)
{
	int i;

	if (handle->cm_valid_count == 0)
	{
		return -1;
	}
	for (i = handle->cm_valid_count - 1; i >= 0; i--)
	{
		if (valid_start > handle->cm_valid_start[i] + handle->cm_valid_size[i])
			return i;
	}
	return -1;
}

static int do_we_add_new_region(int *index, int count)
{
	int i;

	/* No prior regions. We have to add a new region */
	if (count == 0 || index == NULL)
	{
		return 1;
	}
	for (i = 0; i < count; i++) 
	{
		/* if there is atleast one 1, then we don't have to add a new region */
		if (index[i] == 1)
			return 0;
	}
	/* Nope. we have to add a new region */
	return 1;
}

static int fixup_overlapping_regions(cm_frame_t *handle, cm_pos_t valid_start,
		cm_size_t valid_size, int seq_start, int seq_count)
{
	int i = 0, j = 0;
	/* some overlap or continuity of dirty region(s) */
	cm_pos_t    new_valid_start = 0, handle_valid_start = 0, *start_new = NULL;
	cm_size_t   new_valid_size = 0,  handle_valid_size = 0,  *size_new = NULL;

	assert(seq_count > 0 && seq_count <= handle->cm_valid_count);

	for (i = 0; i < seq_count; i++)
	{
		j = i + seq_start;
		handle_valid_start = handle->cm_valid_start[j];
		handle_valid_size  = handle->cm_valid_size[j];
		/* j is one of the indices that overlap */
		if (i == 0)
		{
			/* compute the valid_start for i == 0 since we are guaranteed that this list is sorted */
			new_valid_start = min(handle_valid_start, valid_start);
		}
		/* handle continuity case */
		if ((valid_start + valid_size) == handle_valid_start
				|| (handle_valid_start + handle_valid_size) == valid_start) 
		{
			new_valid_size += handle_valid_size;
		}
		else 
		{ 
			/* compute the first one's overlap if any */
			if (i == 0)
			{
				if (valid_start > handle_valid_start 
						&& (valid_start + valid_size > handle_valid_start + handle_valid_size))
				{
					new_valid_size += valid_start - handle_valid_start;
				}
			}
			/* and the last one's overlap if any */
			else if (i == seq_count - 1)
			{
				if (valid_start + valid_size < handle_valid_start + handle_valid_size)
				{
					new_valid_size += (handle_valid_start + handle_valid_size - valid_start - valid_size);
				}
			}
		}
	}
	if (seq_count == 1)
	{
		handle_valid_start = handle->cm_valid_start[seq_start];
		handle_valid_size  = handle->cm_valid_size[seq_start];

		if (handle_valid_start <= valid_start) 
		{
			if (valid_start + valid_size <= handle_valid_start + handle_valid_size) 
			{
				new_valid_size = handle_valid_size;
			}
			else 
			{
				new_valid_size = valid_size + (valid_start - handle_valid_start);
			}
		}
		else 
		{
			if (handle_valid_start + handle_valid_size <= valid_start + valid_size)
			{
				new_valid_size = valid_size;
			}
			else
			{
				new_valid_size = handle_valid_size + (handle_valid_start - valid_start);
			}
		}
		handle->cm_valid_start[seq_start] = new_valid_start;
		handle->cm_valid_size[seq_start] = new_valid_size;
	}
	else 
	{
		new_valid_size += valid_size;
		handle->cm_valid_start[seq_start] =  new_valid_start;
		handle->cm_valid_size[seq_start] = new_valid_size;
		/* All entries between (seq_start + 1) & (seq_start + seq_count - 1) get zapped now */
		start_new = (cm_pos_t *) calloc(handle->cm_valid_count - seq_count + 1, sizeof(cm_pos_t));
		if (start_new == NULL)
		{
			return -ENOMEM;
		}
		size_new = (cm_size_t *) calloc(handle->cm_valid_count - seq_count + 1, sizeof(cm_size_t));
		if (size_new == NULL)
		{
			free(start_new);
			return -ENOMEM;
		}
		j = 0;
		for (i = 0; i < handle->cm_valid_count; i++)
		{
			if (i < seq_start || i >= (seq_start + seq_count))
			{
				start_new[j] = handle->cm_valid_start[i];
				size_new[j] = handle->cm_valid_size[i];
				j++;
			}
			else if (i == seq_start)
			{
				start_new[j] = new_valid_start;
				size_new[j] = new_valid_size;
				j++;
			}
		}
		free(handle->cm_valid_start);
		free(handle->cm_valid_size);
		handle->cm_valid_start = start_new;
		handle->cm_valid_size = size_new;
		handle->cm_valid_count -= (seq_count - 1);
		Assert(handle->cm_valid_count > 0);
	}
	return 0;
}

/*
 * We try and maintain a sorted list of <valid_offsets and length> pairs
 * in the cache frame object, and this way it it easy for us to do the merging
 * when a subsequent write operation spans already existing pairs.
 * e.g. write from <offset 10, size 5>, <offset 15, size 5>
 */
int CMGRINT_fixup_valid_regions(cm_frame_t *handle,
		cm_pos_t valid_start, cm_size_t valid_size)
{
	int addnew = 0, *valid_index = NULL;

	assert(valid_size > 0);

	if (handle->cm_valid_count == 0) 
	{
		assert(handle->cm_valid_start == NULL);
		assert(handle->cm_valid_size == NULL);
	}
	else 
	{
		assert(handle->cm_valid_start != NULL);
		assert(handle->cm_valid_size != NULL);
		valid_index = (int *) calloc(handle->cm_valid_count, sizeof(int));
		if (valid_index == NULL)
		{
			return -ENOMEM;
		}
	}
	do_find_indexes(handle, valid_start, valid_size, valid_index);
	
	/* do we have to add a new region? If there are no 1's or if valid_index is NULL we have to add */
	addnew = do_we_add_new_region(valid_index, handle->cm_valid_count);

	/* if we have to add a new region */
	if (addnew)
	{
		int i, whatindex = 0;
		cm_pos_t *new_valid_start = NULL;
		cm_size_t *new_valid_size = NULL;

		/* find out where we have to add this. */
		whatindex = find_which_index(handle, valid_start, valid_size);

		handle->cm_valid_count++;
		new_valid_start = (cm_pos_t *) calloc(handle->cm_valid_count, sizeof(cm_pos_t));
		if (!new_valid_start)
		{
			free(valid_index);
			return -ENOMEM;
		}
		new_valid_size = (cm_size_t *) calloc(handle->cm_valid_count, sizeof(cm_size_t));
		if (!new_valid_size)
		{
			free(valid_index);
			free(new_valid_start);
			return -ENOMEM;
		}
		if (handle->cm_valid_start && handle->cm_valid_size)
		{
			Assert(handle->cm_valid_count > 1);
			memcpy(new_valid_start, handle->cm_valid_start, (handle->cm_valid_count - 1) * sizeof(cm_pos_t));
			memcpy(new_valid_size , handle->cm_valid_size , (handle->cm_valid_count - 1) * sizeof(cm_size_t));
		}
		for (i = handle->cm_valid_count - 2; i > whatindex; i--)
		{
			new_valid_start[i+1] = handle->cm_valid_start[i];
			new_valid_size[i+1] = handle->cm_valid_size[i];
		}
		new_valid_start[whatindex + 1] = valid_start;
		new_valid_size[whatindex + 1] = valid_size;
		free(handle->cm_valid_start);
		free(handle->cm_valid_size);
		handle->cm_valid_start = new_valid_start;
		handle->cm_valid_size = new_valid_size;
	}
	else
	{
		int seq_start = 0, seq_count = 0;

		find_set_of_overlapped_indexes(valid_index, handle->cm_valid_count,
				&seq_start, &seq_count);

		/* fixup all the overlapping regions now */
		if (fixup_overlapping_regions(handle, valid_start, valid_size, 
				seq_start, seq_count) < 0)
		{
			free(valid_index);
			return -ENOMEM;
		}
	}
	free(valid_index);
#if 0
	dprintf("After fixup_valid_regions\n");
	CMGRINT_print_valid_regions(handle);
#endif
	return 0;
}

/*
 * Our intention here is as follows,
 * if a file has been written to that has not yet been propagated
 * to the servers and subsequently a read for that comes along,
 * then we try to see if we can satisfy the reads locally, and if so
 * return. else we have 2 possibilities,
 * a) EASY alternative: flush the page if it is dirty.
 *    and then issue a fresh-refetch of the page.
 * b) HARDER alternative: issue a fetch for the page,
 * and apply the diffs on top of the fetched page and mark the page dirty.
 * For now, I am going with the EASY alternative.
 * Return values dictate whether or not we need to issue a fetch.
 * 1 - means issue a fetch.
 * 0 - means don't issue a fetch.
 * -ve - some kind of error.
 */
static int check_for_local_reads(cm_frame_t *p, cm_pos_t valid_start,
	cm_size_t valid_size)
{
	if (!Page_Uptodate(p))
	{
		return 1;
	}
	/* problem arises only if page was dirty! */
	if (Page_Dirty(p))
	{
		int i = 0, ret = 0;

		Assert(p->cm_valid_count > 0);
		for (i = 0; i < p->cm_valid_count; i++) 
		{
			/* check if the user requested portion lies within */
			if (valid_start < p->cm_valid_start[i] 
				|| valid_start > (p->cm_valid_start[i] + p->cm_valid_size[i]))
			{
				continue;
			}
			if (valid_start + valid_size <= 
				(p->cm_valid_start[i] + p->cm_valid_size[i]))
			{
				break;
			}
		}
		/* ah, so the read is outside the dirty regions */
		if (i == p->cm_valid_count)
		{
			/* for now, I am going to use the easy alternative! i.e flush & fetch */
			dprintf("Correctness WB of %d\n", p->cm_id);
			ret = __CMGRINT_wb_sync(1, &p);
			if (ret < 0)
			{   
				/* writeback failed */
				return ret;
			}
			ClearPageUptodate(p);
			return 1;
		}
	}
	/* we don't have to fetch because the page is clean and uptodate! */
	return 0;
}

/* 
 *  search for the block in the hash table and return it with the lock held
 *  and if not found, then allocate one, 
 *  and return it with the lock held on the cm_frame_t object.
 *  Note that we may find objects that match the handle given, but if
 *  they have been invalidated one must re-search and find a newer 
 *  frame if any.
 */
#define CHAIN_READ  0
#define CHAIN_WRITE 1

cm_frame_t* CMGRINT_block_get(cm_block_t *block, 
	void *private, int *error, int account_miss)
{
	cm_frame_t *p = NULL, *q = NULL;
	struct mqhash_head *ent = NULL;
	int hindex = 0, mode = CHAIN_READ;

	*error = 0;
	if (!block || !private)
	{   
		panic("Invalid options specified\n");
		*error = -EINVAL;
		return NULL;
	}
	/* Allocate a temporary cache block */
	q = cmgr_block_alloc();
	/* start searching */
	hindex = cm_block_table->hash(block, cm_block_table->table_size);

retry:
	if (mode == CHAIN_READ) 
	{
		/* hold on to the read lock of the hash chain */
		mqhash_rdlock(&cm_block_table->lock[hindex]);
	}
	else if (mode == CHAIN_WRITE) 
	{
		/*  hold on to the write lock of the hash chain */
		mqhash_wrlock(&cm_block_table->lock[hindex]);
	}
	mqhash_for_each (ent, &(cm_block_table->array[hindex])) 
	{
		if (cm_block_table->compare(block, ent)) /* matches! */
		{
			p = qlist_entry(ent, cm_frame_t, cm_hash);
			lock_page(p);
			/*
			 * if the page was marked invalid, we have to drop the lock
			 * and continue the  search as if we didnt find it in the
			 * block hash tables.
			 */
			if (Page_Invalid(p)) 
			{
				unlock_page(p);
				p = NULL;
				continue;
			}
			/*
			 * This could have been an inherently racy statement, with multiple reader
			 * threads accessing the same cached page, all of which would enter 
			 * inside and try and update it. Unfortunately asm/atomic.h is not universally
			 * allowed to be included from user-space programs like PowerPC for instance.
			 * Otherwise, I would like cm_fix to be of type atomic_t.
			 * For now, I am hoping this won't be a show-stopper :)
			 */
			p->cm_fix++;
			break;
		}
	}
	/* missed in the cache or any error */
	if (!p) 
	{
		if (mode == CHAIN_READ) 
		{
			/* reissue the search with mode set to CHAIN_WRITE */
			mode = CHAIN_WRITE;
			/* unlock the hash chain */
			mqhash_unlock(&cm_block_table->lock[hindex]);
			goto retry;
		}
		else if (mode == CHAIN_WRITE) 
		{
			/* we have already hopefully allocated a page and locked it as well */
			p = q;
			*error = -ENOMEM;
			/* Should we account for this in MISS stats */
			if (account_miss == 1)
			{
				MISSES++;
			}
			if (p) 
			{
				/* fill it with the desired block and add it to the file list etc */
				if ((*error = cmgr_block_fill(p, block, private)) < 0) 
				{
					panic("(1) Error in cmgr_block_fill %d\n", *error);
					/* could not get a handle on filp */
					cmgr_block_release(p);
					p = NULL;
				}
				else
				{
					/* Add it to the block hash table */
					mqhash_add(cm_block_table, block, &p->cm_hash);
					/* mark it as not being free */
					ClearPageFree(p);
					/* mark it as not being invalid also */
					ClearPageInvalid(p);
					/* A newly created page will start out !uptodate */
					ClearPageUptodate(p);
				}
			}
		}
	}
	else
	{
		HITS++;
		/* We hit in the cache! Release the allocated block */
		cmgr_block_release(q);
	}
	/* unlock the hash chain */
	mqhash_unlock(&cm_block_table->lock[hindex]);
	return p; /* p is locked, and fetched if we were able to allocate memory for it ! */
}

/* return 1 if 2 logical blocks are indeed one and the same. 0 if not and -1 on error */

static inline int same_block(cm_block_t *block1,
		cm_block_t *block2)
{
	if (!block1 || !block2 
		|| !block1->cb_handle || !block2->cb_handle) 
	{
		return -1;
	}
	Assert(compare_file_ptr);
	/* Check if the file is the same */
	if (compare_file_ptr(block1->cb_handle, block2->cb_handle) == 1)
	{
	    int ret = -1;

	    if ((compare_block_ptr 
			&& (ret = compare_block_ptr(&block1->cb_page, &block2->cb_page)) == 1)
		    || (ret < 0 && block1->cb_page == block2->cb_page))
	    {
		return 1;
	    }
	}
	return 0;
}

/* Delete the block from the block-hash tables if possible */
int CMGRINT_block_del(cm_frame_t *p, cm_block_t *old_block, int force)
{
	int ret, hindex;

	hindex = cm_block_table->hash(&p->cm_block, cm_block_table->table_size);
	/* drop the lock and then try delete it from the hash chain */
	unlock_page(p);
	/* Write lock the hash chain */
	mqhash_wrlock(&cm_block_table->lock[hindex]);
	if (!force) 
	{
		/* 
		 * We have potentially raced under 2 conditions
		 * a) Try to get a lock fails.
		 * b) We obtained the lock, but found that someone yanked
		 * 	this frame from under!!
		 */
		if ((ret = trylock_page(p)) != 0
				|| !same_block(&p->cm_block, old_block)) 
		{
			/* Damn! we just raced! */
			if (ret == 0) 
			{
				/* drop the page lock if we were successfull */
				unlock_page(p);
			}
			/* drop the hash chain lock */
			mqhash_unlock(&cm_block_table->lock[hindex]);
			/* Indicate that this victim could not be freed */
			return -1;
		}
	}
	else 
	{
		/* Wait for the lock on the page */
		lock_page(p);
		if (!same_block(&p->cm_block, old_block)) 
		{
			/* Damn! we just raced! */
			unlock_page(p);
			/* drop the hash chain lock */
			mqhash_unlock(&cm_block_table->lock[hindex]);
			/* Indicate that this victim could not be freed */
			return -1;
		}
	}
	/* we hold a write lock on the hash chain and a lock on the frame now */

	/* delete the cache frame from the hash chain */
	mqhash_del(&p->cm_hash);
	/* unlock the hash chain */
	mqhash_unlock(&cm_block_table->lock[hindex]);
	/* frame is still locked though */
	return 0;
}

int64_t CMGRINT_complete_fetch(int nframes, int nfetch, cm_frame_t **fr, cm_size_t *valid_size)
{
	int i = 0, j = 0, *comp = NULL, count = 0, get_only_missing = 0;
	int64_t *file_offsets = NULL;
	int32_t *file_sizes = NULL, flag = 0;
	cm_buffer_t *buffers = NULL;
	int64_t handle = 0;
	int64_t comp_size = 0;

	get_only_missing = (nframes == nfetch) ? 0 : 1;
	
	file_sizes = (int32_t *) 
		calloc(nfetch, sizeof(int32_t));
	file_offsets = (int64_t *) 
		calloc(nfetch, sizeof(int64_t));
	buffers = (cm_buffer_t *)
		calloc(nfetch, sizeof(cm_buffer_t));

	if (!file_sizes || !buffers || !file_sizes)
	{
		free(buffers);
		free(file_offsets);
		free(file_sizes);
		return -ENOMEM;
	}
	count = 0;
	for (i = 0; i < nframes; i++)
	{
		/* if we are getting only missing blocks, set variables only for missing blocks, else set it for all */
		if ((get_only_missing == 1 && !Page_Uptodate(fr[i])) || get_only_missing == 0)
		{
			file_offsets[count] = (BSIZE * fr[i]->cm_block.cb_page);
			file_sizes[count] = BSIZE;
			buffers[count] = (cm_buffer_t) (fr[i]->cm_buffer);
			count++;
		}
	}
	if (get_only_missing == 1) 
	{
		Assert(count == nfetch);
	}
	else 
	{
		Assert(count == nframes);
	}
	FETCHES++;
	/* Initiate the fetch */
	handle = global_options.options.readpage_begin(
		fr[0]->cm_block.cb_handle, nfetch,
		buffers, file_sizes, file_offsets);
	if (handle < 0)
	{
		free(buffers);
		free(file_offsets);
		free(file_sizes);
		return handle;
	}
	else
	{
		/* wait for completion */
		comp = global_options.options.readpage_complete(handle);
		Assert(comp != NULL);
	}
	j = 0;
	for (i = 0; i < nframes; i++)
	{
		/* Also mark the valid zones for this page */
		if ((get_only_missing == 1 && !Page_Uptodate(fr[i])) || get_only_missing == 0)
		{
			if (flag == 0 && comp[j] >= 0) 
			{
				/* This has bitten me in the past. */
				if (comp[j] > 0) {
				    CMGRINT_fixup_valid_regions(fr[i], 
					    0, comp[j]); 
				    SetPageUptodate(fr[i]);
				}
				comp_size += comp[j];
				valid_size[i] = comp[j];
			}
			else {
			    flag = 1;
			    comp_size = comp[j];
			}
			j++;
		}
	}
	free(comp);
	free(buffers);
	free(file_offsets);
	free(file_sizes);
	return comp_size;
}

int64_t __CMGRINT_fetch_sync(int nframes, cm_frame_t **fr,
	cm_pos_t *valid_start, cm_size_t *valid_size)
{
	int i, total_not_uptodate = 0, nfetch = 0;
	int non_contig = 0, flag_uptodate = 0, flag_notuptodate = 0, get_only_missing = 0;
	int64_t comp_size = 0;

	comp_size = 0;
	total_not_uptodate = 0;
	for (i = 0; i < nframes; i++)
	{
		int ret;

		CMGRINT_do_sanity_checks(fr[i]);
		/* Make sure that we fetch a page that is not uptodate */
		ret = check_for_local_reads(fr[i], valid_start[i], valid_size[i]);
		if (ret == 1)
		{
			/* Since this page is not uptodate */
			valid_size[i] = 0;
			total_not_uptodate++;
			if (flag_uptodate == 1)
			    non_contig++;
			flag_notuptodate = 1;
		}
		else {
			comp_size += valid_size[i];
			if (flag_notuptodate == 1)
			    non_contig++;
			flag_uptodate = 1;
		}
	}
	if (total_not_uptodate == 0)
	{
		/* nothing needs to be done here */
		return comp_size;
	}
	/*
	 * non_contig == 0
	 *  implies that all pages are not-upto-date if we get to this point.
	 * non_contig == 1
	 *  implies one of two things
	 *  a) Transition only from Not-Uptodate pages to Uptodate pages 
	 *  or
	 *  b) Transitions only from Uptodate pages to Not-Uptodate pages.
	 *  and no other transitions.
	 *  Why is this important? This means that we can fetch the missing/not-uptodate
	 *  blocks from server in 1 RPC message atomically..
	 *
	 *  If  non_contig is > 1, there is non-contiguity in Uptodate and non-uptodate
	 *  blocks, and if we dont have a way to express RPC fetches of non-contiguous blocks,
	 *  we would rather fetch all the blocks....
	 */
	if (non_contig == 0 || non_contig == 1)
	{
		nfetch = total_not_uptodate;
		get_only_missing = 1;
	}
	else if (non_contig > 1) 
	{
		/* fetch everything... */
		nfetch = nframes;
		get_only_missing = 0;
	}
	if (get_only_missing == 1)
	{
		dprintf("Only missing %d page frames need to be fetched\n", nfetch);
	}
	else if (get_only_missing == 0)
	{
		dprintf("All %d page frames need to be fetched\n", nfetch);
	}
	return CMGRINT_complete_fetch(nframes, nfetch, fr, valid_size);
}

/*
 * Returns the amount of data fetched for each cache frame object 
 */
int64_t CMGRINT_fetch_sync(int nframes, cm_frame_t **fr, 
	cm_pos_t *valid_start, cm_size_t *valid_size)
{
	int i;
	int64_t ret = 0;

	/* get a lock on all the frames and issue a fetch */
	for (i = 0; i < nframes; i++)
	{
	    lock_page(fr[i]);
	}
	ret = __CMGRINT_fetch_sync(nframes, fr, valid_start, valid_size);
	for (i = 0; i < nframes; i++)
	{
	    unlock_page(fr[i]);
	}
	return ret;
}

int64_t __CMGRINT_wb_sync(int nframes, cm_frame_t **fr)
{
	int i, j, total_valid_count = 0, *comp = NULL, count = 0;
	int64_t *file_offsets = NULL;
	int32_t *file_sizes = NULL;
	cm_buffer_t *buffers = NULL;
	int64_t handle, total = 0;

	for (i = 0; i < nframes; i++)
	{
		CMGRINT_do_sanity_checks(fr[i]);
		/* if page was really dirty */
		if (Page_Dirty(fr[i]))
		{
		    total_valid_count += fr[i]->cm_valid_count;
		}
	}
	file_sizes = (int32_t *) 
		calloc(total_valid_count, sizeof(int32_t));
	file_offsets = (int64_t *) 
		calloc(total_valid_count, sizeof(int64_t));
	buffers = (cm_buffer_t *)
		calloc(total_valid_count, sizeof(cm_buffer_t));

	for (i = 0; i < nframes; i++)
	{
		if (Page_Dirty(fr[i])) 
		{
			if (!file_offsets || !buffers || !file_sizes)
			{
				/* Memory allocation failed */
				total = fr[i]->cm_error = -ENOMEM;
				ClearPageDirty(fr[i]);
			}
			else
			{
				int64_t offset = 0;

				offset = (BSIZE * fr[i]->cm_block.cb_page);
				for (j = 0; j < fr[i]->cm_valid_count; j++)
				{
				    file_offsets[count] = fr[i]->cm_valid_start[j]
							  + offset;
				    buffers[count] = (cm_buffer_t) (fr[i]->cm_valid_start[j]
							  + (char *) fr[i]->cm_buffer);
				    file_sizes[count] = fr[i]->cm_valid_size[j];
				    count++;
				}
			}
		}
	}
	if (count == 0)
	{
		free(buffers);
		free(file_offsets);
		free(file_sizes);
		return -ENOMEM;
	}
	else 
	{
		Assert(count == total_valid_count);
	}
	FLUSHES++;
	/* Initiate the writeback */
	handle = global_options.options.writepage_begin(
		fr[0]->cm_block.cb_handle, total_valid_count,
		buffers, file_sizes, file_offsets);
	if (handle < 0)
	{
		for (i = 0; i < nframes; i++)
		{
			if (Page_Dirty(fr[i]))
			{
				total = fr[i]->cm_error = handle;
				ClearPageDirty(fr[i]);
			}
		}
		free(buffers);
		free(file_offsets);
		free(file_sizes);
		return handle;
	}
	else
	{
		comp = global_options.options.writepage_complete(handle);
		Assert(comp != NULL);
		total = 0;
		for (i = 0; i < total_valid_count; i++)
		{
			if (comp[i] > 0)
			{
				total += comp[i];
			}
			else
			{
				total = comp[i];
				break;
			}
		}
		free(comp);
	}
	for (i = 0; i < nframes; i++)
	{
		if (total < 0)
		{
		    fr[i]->cm_error = total;
		}
		ClearPageDirty(fr[i]);
	}
	free(buffers);
	free(file_offsets);
	free(file_sizes);
	return total;
}

int64_t CMGRINT_wb_sync(int nframes, cm_frame_t **fr)
{
	int64_t total = 0;
	int i;

	/* get a lock on all the frames and flush out dirty frames */
	for (i = 0; i < nframes; i++)
	{
	    lock_page(fr[i]);
	}
	total = __CMGRINT_wb_sync(nframes, fr);
	for (i = 0; i < nframes; i++)
	{
	    unlock_page(fr[i]);
	}
	return total;
}

/* Writes back all dirty buffers */
void CMGR_block_wb_all(void)
{
	int i;

	for ( i = 0; i < cm_block_table->table_size; i++) 
	{
		struct qlist_head *entry;
	
		/* read lock the hash chain */
		mqhash_rdlock(&cm_block_table->lock[i]);
		qlist_for_each (entry, &(cm_block_table->array[i])) 
		{
			cm_frame_t *fr;

			fr = qlist_entry(entry, cm_frame_t, cm_hash);
			dprintf("Flush-all WB of %d\n", fr->cm_id);
			CMGRINT_wb_sync(1, &fr);
		}
		mqhash_unlock(&cm_block_table->lock[i]);
	}
	return;
}

/* Initialize and cleanup routines */
int CMGRINT_block_init(cmgr_options_t *options, int block_table_size)
{
	/* stash away the function pointers for later use */
	compare_file_ptr = options->compare_file;
	hash_file_ptr    = options->file_hash;
	compare_block_ptr = options->compare_block;
	hash_block_ptr = options->block_hash;

	if ((cm_block_table = mqhash_init(compare_block, block_hash, block_table_size))
			== NULL) 
	{
		panic("Could not allocate and initialize block hash tables\n");
		return -ENOMEM;
	}
	return 0;
}

void CMGRINT_block_finalize(void)
{
	mqhash_finalize(cm_block_table);
	compare_file_ptr = NULL;
	hash_file_ptr = NULL;
	compare_block_ptr = NULL;
	hash_block_ptr = NULL;
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
