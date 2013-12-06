#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <semaphore.h>
#include "cmgr_internal.h"
#include "gen-locks.h"

#ifdef VERBOSE_DEBUG
#include "sha.h"
#endif

static int cmgr_initialized = 0;

static cm_frame_t *cm_frames; /* array of all cache frames */
static cm_handle_t *cm_handles; /* array of all allocated handles */
static QLIST_HEAD(cm_free); /* Free list */
static cm_buffer_t cm_bufferpool = NULL; /* buffer pool */

/* one time initialization variable */
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_key_t  key;

cmgr_internal_options_t global_options;
FILE *output_fp = NULL;
FILE *error_fp  = NULL;

static void* cm_harvester(void *);

static void setup_output(cmgr_options_t *options)
{
	char *output_fname = NULL;

	output_fname = options->co_output_fname;
	/* if output_fname is not NULL, then we open a new file stream */
	if (output_fname != NULL) 
	{
		if ((output_fp = fopen(output_fname, "w+")) == NULL) 
		{
			/* revert to stdout/stderr if output_fname could not be created! */
			output_fp = stdout;
			error_fp = stderr;
		}
		else 
		{
			/* make standard error also point to the new file */
			error_fp = output_fp;
		}
	}
	else
	{
		output_fp = stdout;
		error_fp = stderr;
	}
	PAGESIZE = sysconf(_SC_PAGE_SIZE);
	PAGESHIFT = LOG_2(PAGESIZE);
	PAGEMASK = ~(PAGESIZE - 1);
	return;
}

static void cleanup_output(void) __attribute__((unused));
static void cleanup_output(void)
{
	if (output_fp) 
	{
		fclose(output_fp);
	}
	if (error_fp) 
	{
		fclose(error_fp);
	}
	return;
}

static void key_create(void)
{
	struct sigaction sig_handler;
	static void handler(int sig, siginfo_t *info, void *unused);

	pthread_key_create(&key, NULL);
	memset(&sig_handler, 0, sizeof(sig_handler));
	sig_handler.sa_sigaction = handler;
	sig_handler.sa_flags = SA_SIGINFO;
	sigaction(SIGUSR2, &sig_handler, NULL);

	return;
}

static int cmgr_buffer_init(cmgr_options_t *options)
{
	int bsize, i, bcount, handle_size;
	long page_size;

	page_size = sysconf(_SC_PAGE_SIZE);
	bsize = options->co_bsize > 0 ? options->co_bsize : CM_BSIZE;
	bcount = options->co_bcount > 0 ? options->co_bcount : CM_BCOUNT;
	handle_size = options->co_handle_size > 0 ? options->co_handle_size : CM_HANDLE_SIZE;

	/*
	if (bsize % page_size != 0)
	{
		panic("Cannot have a block size(%u) that is a \
				non-multiple of page size(%lu)\n", bsize, page_size);
		return -EINVAL;
	}
	*/
	cm_bufferpool = valloc(bsize * bcount);
	if (!cm_bufferpool) 
	{
		panic( "Could not allocate buffer pool of size %u KB\n",
				(bsize * bcount) >> 10);
		return -ENOMEM;
	}
	/* zero out all the bufffers */
	memset(cm_bufferpool, 0, bsize * bcount);
	if ((cm_frames = (cm_frame_t *)
				calloc(bcount, sizeof(cm_frame_t))) == NULL) 
	{
		panic("Could not allocate frame pool with %u frames\n", bcount);
		return -ENOMEM;
	}
	if ((cm_handles = (cm_handle_t *)
				calloc(bcount, sizeof(cm_handle_t))) == NULL) 
	{
		panic("Could not allocate handle pool with %u frames\n", bcount);
		return -ENOMEM;
	}
	for (i = 0; i < bcount; i++) 
	{
		cm_handles[i] = (cm_handle_t) 
			calloc(1, handle_size);
		if (NULL == cm_handles[i]) {
			panic("Could not allocate handle[%d]\n", i);
			return -ENOMEM;
		}
		pthread_mutex_init(&cm_frames[i].cm_lock, NULL);
		cm_frames[i].cm_magic = CM_MAGIC;
		cm_frames[i].cm_id = i;
		lock_page(&cm_frames[i]);
		cm_frames[i].cm_block = NULL_BLOCK(i);
		cm_frames[i].cm_buffer = (cm_buffer_t)&(((char *) cm_bufferpool)[i*bsize]);
		cm_frames[i].cm_ref =
			cm_frames[i].cm_fix = cm_frames[i].cm_error = 0;
		/* NULL private field */
		cm_frames[i].cm_private = NULL;
		/* All frames are clean to begin with */
		ClearPageDirty(&cm_frames[i]);
		/* All frames begin on the FREE list */
		SetPageFree(&cm_frames[i]);
		/* All frames begin by NOT being a part of any FILE list */
		ClearPageFile(&cm_frames[i]);
		/* All frames are INVALID initially */
		SetPageInvalid(&cm_frames[i]);
		/* All Frames are also not uptodate */
		ClearPageUptodate(&cm_frames[i]);
		/* no valid regions on the page */
		cm_frames[i].cm_valid_count = 0;
		cm_frames[i].cm_valid_start = NULL;
		cm_frames[i].cm_valid_size = NULL;
		qlist_add_tail(&cm_frames[i].cm_hash, &cm_free);
		unlock_page(&cm_frames[i]);
	}
	dprintf("Buffers initialized [%u frames each of size %u bytes]\n", bcount, bsize);
	return 0;
}

static void cmgr_buffer_finalize(void)
{
	int i;

	for (i = 0; i < BCOUNT; i++) 
	{
		free(cm_handles[i]);
		if (cm_frames[i].cm_valid_start)
		{
			free(cm_frames[i].cm_valid_start);
			cm_frames[i].cm_valid_start = NULL;
		}
		if (cm_frames[i].cm_valid_size) 
		{
			free(cm_frames[i].cm_valid_size);
			cm_frames[i].cm_valid_size = NULL;
		}
		cm_frames[i].cm_valid_count = 0;
	}
	free(cm_handles);
	free(cm_frames);
	free(cm_bufferpool);
	return;
}

static int cmgr_harvester_init(void)
{
	int ret;

	pthread_mutex_init(&global_options.mutex, NULL);
	pthread_cond_init(&global_options.avail, NULL);
	pthread_cond_init(&global_options.needed, NULL);
	if ((ret = pthread_create(&global_options.harvester, NULL, 
					cm_harvester, NULL)) != 0) 
	{
		panic("Could not create harvester thread: %s\n", strerror(ret));
		return -ret;
	}
	dprintf("Harvester thread initialized\n");
	return 0;
}

static void cmgr_harvester_finalize(void)
{
	if (global_options.harvester) 
	{
		int ret = 0;

		dprintf("About to send a signal to harvester\n");
		if ((ret = pthread_kill(global_options.harvester, SIGUSR2))
				!= 0) {
			dprintf("Could not send SIGUSR2 to harvester thread: %s\n", strerror(ret));
		}
		dprintf("Waiting for the harvester thread to exit\n");
		/* wait for it to die */
		if ((ret = pthread_join(global_options.harvester, NULL)) != 0) {
			dprintf("Could not join & wait for harvester's completion: %s\n", strerror(ret));
		}
		global_options.harvester = 0;
	}
	return;
}

int CMGR_init(cmgr_options_t *options)
{
	int ret = 0;

	if (cmgr_initialized == 0) 
	{
		int bsize, bcount, block_table_size, file_table_size, handle_size;

		if (!options 
			|| !options->compare_file
			|| !options->file_hash
			|| !options->readpage_begin
			|| !options->writepage_begin
			|| !options->readpage_complete
			|| !options->writepage_complete) 
		{
			panic("Invalid parameters. Cannot initialize cache!\n");
			return -EFAULT;
		}
		/* try to create a key */
		pthread_once(&once, key_create);

		bsize = options->co_bsize > 0 
		    ? options->co_bsize : CM_BSIZE;
		bcount = options->co_bcount > 0
		    ? options->co_bcount : CM_BCOUNT;
		block_table_size = options->co_block_table_size > 0
		    ? options->co_block_table_size : CM_TABLE_SIZE;
		file_table_size = options->co_file_table_size > 0
		    ? options->co_file_table_size : CM_TABLE_SIZE;
		handle_size = options->co_handle_size > 0
		    ? options->co_handle_size : CM_HANDLE_SIZE;
		/* Make sure that bsize is non-ve */
		if (bsize <= 0)
		{
			panic("Cache block size [%u] should be greater"
				"than 0!\n", bsize);
			return -EINVAL;
		}
		setup_output(options);
		LOG_BSIZE = LOG_2(bsize);
		/* make sure that all the remaining parameters are sane enough to set things in motion */
		if (bcount <= 0 
			|| block_table_size <= 0 
			|| file_table_size <= 0
			|| handle_size <= 0)
		{
			panic("Invalid cache options!\n");
			return -EINVAL;
		}
		/* Initialize the block hash tables. */

		/* NOTE: The block compare and hash routines are optional and need not be supplied */
		if ((ret = CMGRINT_block_init(options, block_table_size)) < 0)
		{
			CMGR_finalize();
			return ret;
		}
		/* Initialize the file hash tables */
		if ((ret = CMGRINT_file_init(options, file_table_size)) < 0) 
		{
			CMGR_finalize();
			return ret;
		}
		/* Initialize the buffer's */
		if ((ret = cmgr_buffer_init(options)) < 0) 
		{
			CMGR_finalize();
			return ret;
		}
		/* setup some convenience variables/macros */	
		BSIZE  = bsize;
		BCOUNT = bcount;
		BTSIZE = block_table_size;
		BFTSIZE = file_table_size;
		HANDLESIZE = handle_size;

		/* Initialize all our function pointer callbacks */
		global_options.options.compare_block = options->compare_block;
		global_options.options.block_hash = options->block_hash;
		global_options.options.compare_file = options->compare_file;
		global_options.options.file_hash = options->file_hash;
		global_options.options.readpage_begin = options->readpage_begin;
		global_options.options.writepage_begin = options->writepage_begin;
		global_options.options.readpage_complete = options->readpage_complete;
		global_options.options.writepage_complete = options->writepage_complete;

		FIXES = UNFIXES= 0;
		HITS = MISSES= 0;
		FETCHES = FLUSHES = INVALIDATES = 0;
		HARVESTS = SCANS= 0;
		global_options.low_water = CM_LOW_WATER * bcount + 1;
		global_options.high_water = CM_HIGH_WATER * bcount + 1;
		global_options.num_free_frames = bcount;
		global_options.batch_ratio = CM_BATCH_RATIO * bcount + 1;

		/* Create the harvester thread */
		if ((ret = cmgr_harvester_init()) < 0) 
		{
			CMGR_finalize();
			return ret;
		}
		dprintf("Initialized the cache-manager subsystem\n");
		cmgr_initialized = 1;
		return 0;
	}
	return -EAGAIN;
}

void CMGR_finalize(void)
{
	if (cmgr_initialized == 0) 
	{
		return;
	}
	/* First writeback all dirty buffers */
	CMGR_block_wb_all();
	/* terminate the harvester thread */
	cmgr_harvester_finalize();
	/* cleanup the file hash tables */
	CMGRINT_file_finalize();
	/* cleanup the block hash tables */
	CMGRINT_block_finalize();
	/* cleanup the buffers */
	cmgr_buffer_finalize();

	cmgr_initialized = 0;
	dprintf("Finalized the Cache Manager sub-system\n");
#if 0
	if (getenv("CMGR_STATS") != NULL) 
	{
		printf( "Cache Manager Configuration\n");
		printf( "Block Count = %u, Block Size = %u\n",
				BCOUNT, BSIZE);
		printf( "Cache Manager Hits: %Lu, Misses: %Lu\n",
				HITS, MISSES);
		printf( "Cache Manager Fetches: %Lu, Writebacks: %Lu\n",
				FETCHES, FLUSHES);
		printf( "Cache Manager Invalidates: %Lu\n", INVALIDATES);
		printf( "Cache Manager Fixes: %Lu, Unfixes: %Lu\n",
				FIXES, UNFIXES);
		printf( "Cache Manager nHarvests: %Lu, nScans: %Lu\n",
				HARVESTS, SCANS);
	}
	cleanup_output();
#endif
	return;
}

int CMGR_get_stats(cmgr_stats_t *stats, int reset)
{   
	if (!stats) 
	{
		return -EFAULT;
	}
	memcpy(stats, &global_options.stats, sizeof(cmgr_stats_t));
	if (reset) 
	{
	    memset(&global_options.stats, 0, sizeof(cmgr_stats_t));
	}
	return 0;
}

void CMGRINT_do_sanity_checks(cm_frame_t *frame)
{
	int i;

	/* make sure that we have all the parameters in a sane-state */
	Assert(frame && frame->cm_block.cb_handle);
	Assert(frame->cm_magic == CM_MAGIC);
	if (Page_Dirty(frame))
	{
		Assert(frame->cm_valid_count > 0);
#if 1
		for (i = 0; i < frame->cm_valid_count; i++)
		{
			Assert(frame->cm_valid_start[i] >= 0 && frame->cm_valid_start[i] < BSIZE);
			Assert(frame->cm_valid_size[i] >= 0 && frame->cm_valid_size[i] <= BSIZE);
			Assert(frame->cm_valid_start[i] + frame->cm_valid_size[i] <= BSIZE);
		}
#endif
	}
	return;
}

/*
 * For the given logical block, locate it in the cache (if possible)
 * and try to obtain an exclusive lock on it,
 * and return a pointer to the cache frame object.
 * If we don't find it in the cache, we "dont" issue 
 * a fetch for it. 
 */
static cm_frame_t* block_get(cm_block_t *block, void *private, int *error, int account_miss)
{
	cm_frame_t* handle = NULL;

	/* get a handle to the cache object and obtain a lock on it in write mode */
	if ((handle = CMGRINT_block_get(block, 
			private, error, account_miss)) == NULL) 
	{
		/* error is set internally */
		return NULL;
	}
	FIXES++;
	/* handle is locked */
	return handle;
}

/* 
 *  Note that this routine is called with the lock
 *  on the handle in the appropriate mode.
 */
static void block_put(cm_frame_t *handle)
{
	/* handle is unlocked in the put routine */
	CMGRINT_block_put(handle);
	UNFIXES++;
	return;
}

/*
 * Quick and hacky fix to mark all blocks of your cache
 * as not being uptodate.
 * Prone to race conditions... but we don't really care
 * since this function is called only on extraneous conditions
 * like network failures etc
 */
void	CMGR_invalidate(void)
{
	int i;
	for (i = 0; i < BCOUNT; i++) 
	{
		/* All frames are clean */
		ClearPageDirty(&cm_frames[i]);
		/* All Frames are also not uptodate */
		ClearPageUptodate(&cm_frames[i]);
	}
	return;
}

int CMGR_simple_init(cmgr_options_t *options)
{
	int ret = 0;

	if (cmgr_initialized == 0) 
	{
		int bsize, bcount, block_table_size, file_table_size, handle_size;

		if (!options 
			|| !options->compare_file
			|| !options->file_hash
			|| !options->readpage_begin
			|| !options->writepage_begin
			|| !options->readpage_complete
			|| !options->writepage_complete) 
		{
			panic("Invalid parameters. Cannot initialize cache!\n");
			return -EFAULT;
		}
		/* try to create a key */
		pthread_once(&once, key_create);

		bsize = options->co_bsize > 0 
		    ? options->co_bsize : CM_BSIZE;
		bcount = options->co_bcount > 0
		    ? options->co_bcount : CM_BCOUNT;
		block_table_size = options->co_block_table_size > 0
		    ? options->co_block_table_size : CM_TABLE_SIZE;
		file_table_size = options->co_file_table_size > 0
		    ? options->co_file_table_size : CM_TABLE_SIZE;
		handle_size = options->co_handle_size > 0
		    ? options->co_handle_size : CM_HANDLE_SIZE;
		/* Make sure that bsize is non-ve */
		if (bsize <= 0)
		{
			panic("Cache block size [%u] should be greater"
				"than 0!\n", bsize);
			return -EINVAL;
		}
		setup_output(options);
		LOG_BSIZE = LOG_2(bsize);
		/* make sure that all the remaining parameters are sane enough to set things in motion */
		if (bcount <= 0 
			|| block_table_size <= 0 
			|| file_table_size <= 0
			|| handle_size <= 0)
		{
			panic("Invalid cache options!\n");
			return -EINVAL;
		}
		/* Initialize the file hash tables */
		if ((ret = CMGRINT_file_init(options, file_table_size)) < 0) 
		{
			CMGR_simple_finalize();
			return ret;
		}
		/* setup some convenience variables/macros */	
		BSIZE  = bsize;
		BCOUNT = bcount;
		BTSIZE = block_table_size;
		BFTSIZE = file_table_size;
		HANDLESIZE = handle_size;

		/* Initialize all our function pointer callbacks */
		global_options.options.compare_block = options->compare_block;
		global_options.options.block_hash = options->block_hash;
		global_options.options.compare_file = options->compare_file;
		global_options.options.file_hash = options->file_hash;
		global_options.options.readpage_begin = options->readpage_begin;
		global_options.options.writepage_begin = options->writepage_begin;
		global_options.options.readpage_complete = options->readpage_complete;
		global_options.options.writepage_complete = options->writepage_complete;

		FIXES = UNFIXES= 0;
		HITS = MISSES= 0;
		FETCHES = FLUSHES = INVALIDATES = 0;
		HARVESTS = SCANS= 0;
		global_options.low_water = CM_LOW_WATER * bcount + 1;
		global_options.high_water = CM_HIGH_WATER * bcount + 1;
		global_options.num_free_frames = bcount;
		global_options.batch_ratio = CM_BATCH_RATIO * bcount + 1;

		dprintf("Initialized the cache-manager subsystem\n");
		cmgr_initialized = 1;
		return 0;
	}
	return -EAGAIN;
}

void CMGR_simple_finalize(void)
{
	if (cmgr_initialized == 0) 
	{
		return;
	}
	/* cleanup the file hash tables */
	CMGRINT_file_finalize();

	cmgr_initialized = 0;
	dprintf("Finalized the Cache Manager sub-system\n");
#if 0
	if (getenv("CMGR_STATS") != NULL) 
	{
		printf( "Cache Manager Configuration\n");
		printf( "Block Count = %u, Block Size = %u\n",
				BCOUNT, BSIZE);
		printf( "Cache Manager Hits: %Lu, Misses: %Lu\n",
				HITS, MISSES);
		printf( "Cache Manager Fetches: %Lu, Writebacks: %Lu\n",
				FETCHES, FLUSHES);
		printf( "Cache Manager Invalidates: %Lu\n", INVALIDATES);
		printf( "Cache Manager Fixes: %Lu, Unfixes: %Lu\n",
				FIXES, UNFIXES);
		printf( "Cache Manager nHarvests: %Lu, nScans: %Lu\n",
				HARVESTS, SCANS);
	}
	cleanup_output();
#endif
	return;
}

void    CMGR_simple_invalidate(void)
{
	CMGRINT_simple_invalidate();
	return;
}

int64_t CMGR_simple_get(char *buffer, cm_handle_t p,
	int64_t begin_chunk, int64_t nchunks, int64_t prefetch_index)
{
	cm_file_t *filp = NULL;
	int64_t i;
	char *ptr = (char *) buffer;
	int total_not_uptodate = 0, nfetch = 0;
	int non_contig = 0, flag_uptodate = 0, flag_notuptodate = 0, get_only_missing = 0;
	int64_t comp_size = 0, start_miss_index = -1;
	
	int *comp = NULL;
	int64_t *file_offsets = NULL;
	int32_t *file_sizes = NULL, flag = 0;
	cm_buffer_t *buffers = NULL;
	int64_t handle = 0;


	/* add this file to the file hash table */
	filp = CMGRINT_file_get(p);
	if (filp == NULL)
	{
		return -ESRCH;
	}
	/* Resize the space allocated for the chunks */
	if (CMGRINT_file_resize(filp, (begin_chunk + nchunks)) < 0)
	{
		CMGRINT_file_put(filp);
		return -ENOMEM;
	}
	if (begin_chunk < 0 || nchunks <= 0 || begin_chunk + nchunks >= filp->cf_hashes.cm_nhashes)
	{
		panic("(simple_get) Invalid <%Ld:%Ld>, %Ld\n", begin_chunk, nchunks, filp->cf_hashes.cm_nhashes);
		CMGRINT_file_put(filp);
		return -EINVAL;
	}
	comp_size = 0;
	total_not_uptodate = 0;
	for (i = 0; i < nchunks; i++)
	{
	    int account_miss = 1;
	    if (prefetch_index >= 0 && (i + begin_chunk) >= prefetch_index)
	    {
		    account_miss = 0;
	    }
	    /* if it is not valid/uptodate */
	    if (filp->cf_hashes.cm_phashes[begin_chunk + i].h_valid == 0)
	    {
		    if (account_miss)
			MISSES++;
		    if (start_miss_index < 0)
			start_miss_index = begin_chunk + i;
		    total_not_uptodate++;
		    if (flag_uptodate == 1)
			non_contig++;
		    flag_notuptodate = 1;
	    }
	    else /* it is valid */
	    {
		    if (account_miss)
			HITS++;
		    if (flag_notuptodate == 1)
			non_contig++;
		    comp_size += BSIZE;
		    flag_uptodate = 1;
		    /* copy the hash to the user buffers! */
		    memcpy(ptr + i * BSIZE, filp->cf_hashes.cm_phashes[begin_chunk + i].h_hash, BSIZE);
	    }
	}
	/* Lets assume that everything hit */
	if (total_not_uptodate == 0)
	{
		CMGRINT_file_put(filp);
		/* nothing needs to be done */
		return comp_size;
	}
	if (start_miss_index < 0)
	{
		panic("Invalid start_miss_index: %Ld\n", start_miss_index);
		CMGRINT_file_put(filp);
		/* nothing needs to be done */
		return -EINVAL;
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
		nfetch = nchunks;
		get_only_missing = 0;
		start_miss_index = begin_chunk;
		comp_size = 0;
	}
	if (nfetch <= 0)
	{
		panic("Invalid nfetch: %d\n", nfetch);
		CMGRINT_file_put(filp);
		/* nothing needs to be done */
		return -EINVAL;
	}
	if (get_only_missing == 1)
	{
		dprintf("Only missing %d page frames need to be fetched starting from %Ld\n", nfetch, start_miss_index);
	}
	else if (get_only_missing == 0)
	{
		dprintf("All %d page frames need to be fetched starting from %Ld\n", nfetch, start_miss_index);
	}
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
		CMGRINT_file_put(filp);
		return -ENOMEM;
	}
	for (i = 0; i < nfetch; i++)
	{
		file_sizes[i] = BSIZE;
		file_offsets[i] = (start_miss_index + i) * BSIZE;
		buffers[i] = (cm_buffer_t) filp->cf_hashes.cm_phashes[start_miss_index + i].h_hash;
	}
	FETCHES++;
	/* initiate the fetch */
	handle = global_options.options.readpage_begin(p, nfetch, buffers, file_sizes, file_offsets);
	if (handle < 0)
	{
		free(buffers);
		free(file_offsets);
		free(file_sizes);
		CMGRINT_file_put(filp);
		return handle;
	}
	else
	{
		/* wait for completion */
		comp = global_options.options.readpage_complete(handle);
		Assert(comp != NULL);
	}
	flag = 0;
	for (i = 0; i < nfetch; i++)
	{   
		/* mark as valid */
		if (comp[i] >= 0)
		{
			if (flag == 0 && comp[i] > 0)
			{
			    filp->cf_hashes.cm_phashes[start_miss_index + i].h_valid = 1;
			    /* copy the hash to the user buffers! */
			    memcpy(ptr + (start_miss_index - begin_chunk + i) * BSIZE, 
				    filp->cf_hashes.cm_phashes[start_miss_index + i].h_hash, BSIZE);
			}
			comp_size += comp[i];
		}
		else {
		    flag = 1;
		    comp_size = comp[i];
		}
	}
	free(comp);
	free(buffers);
	free(file_offsets);
	free(file_sizes);
	CMGRINT_file_put(filp);
	return comp_size;
}

int64_t CMGR_simple_put(char *buffer, cm_handle_t p,
	int64_t begin_chunk, int64_t nchunks)
{
	cm_file_t *filp = NULL;
	int64_t i;
	char *ptr = (char *) buffer;

	/* add this file to the file hash table */
	filp = CMGRINT_file_get(p);
	if (filp == NULL)
	{
		return -ESRCH;
	}
	if (CMGRINT_file_resize(filp, (begin_chunk + nchunks)) < 0)
	{
		CMGRINT_file_put(filp);
		return -ENOMEM;
	}
	if (begin_chunk < 0 || nchunks <= 0 || begin_chunk + nchunks >= filp->cf_hashes.cm_nhashes)
	{
		panic("(Simple put) Invalid <%Ld:%Ld>, %Ld\n", begin_chunk, nchunks, filp->cf_hashes.cm_nhashes);
		CMGRINT_file_put(filp);
		return -EINVAL;
	}
	/* now we know for sure that there is space allocated */
	for (i = 0; i < nchunks; i++)
	{
		/* mark as valid */
		filp->cf_hashes.cm_phashes[begin_chunk + i].h_valid = 1;
		/* Put the hashes into the hcache */
		memcpy(filp->cf_hashes.cm_phashes[begin_chunk + i].h_hash, ptr + i * BSIZE, BSIZE);
#ifdef VERBOSE_DEBUG
		{
		    char str[256];
		    hash2str(filp->cf_hashes.cm_phashes[begin_chunk + i].h_hash, BSIZE, str);
		    dprintf("BSIZE: %d cmgr_simple_put: copied chunk: %Ld, %s\n", BSIZE, begin_chunk + i, str);
		    hash2str(ptr + i * BSIZE, BSIZE, str);
		    dprintf("BSIZE: %d cmgr_simple_put: given chunk: %Ld, %s\n", BSIZE, begin_chunk + i, str);
		}
#endif
	}
	CMGRINT_file_put(filp);
	return (nchunks * BSIZE);
}

int64_t CMGR_get_region(char *buffer, cm_handle_t p,
	int64_t begin_byte, int64_t count, int64_t prefetch_index, cmgr_synch_options_t *options)
{
	cm_block_t blocks;
	char *ptr = buffer;
	cm_page_t begin_page, end_page, i, total_pages;
	loff_t where[2];
	cm_frame_t **page_frames = NULL;
	size_t bsize;
	int log_bsize = 0;
	int64_t error = 0;
	int64_t total_count = 0;
	cm_pos_t  *valid_start = NULL;
	cm_size_t *valid_size = NULL;

	bsize = BSIZE;
	log_bsize = LOG_BSIZE;
	/* break it up into multiple cache-block sized chunks */
	if (log_bsize < 0) {
	    begin_page = begin_byte / bsize;
	    end_page = (begin_byte + count - 1) / bsize;
	}
	else {
	    begin_page = (begin_byte >> log_bsize);
	    end_page = (begin_byte + count - 1) >> log_bsize;
	}
	total_pages = (end_page - begin_page + 1);

	if (total_pages > BCOUNT)
	{
		panic("Cannot satisfy read request of size %Lu bytes/%Lu "
			"pages.\n", count, total_pages);
		panic("Try increasing BCOUNT [%u] and/or BSIZE [%u]\n", BCOUNT, BSIZE);
		errno = EINVAL;
		return -1;
	}
	if (log_bsize < 0) {
	    where[0] = begin_byte - (begin_page * bsize);
	    where[1] = (begin_byte + count) % bsize;
	}
	else {
	    /* where[0] indicates the offset in the first page from which the user is requesting data */
	    where[0] = begin_byte - (begin_page << log_bsize);
	    /* where[1] indicates the count in the last page upto which the user is requesting data */
	    where[1] = (begin_byte + count) & (bsize - 1);
	}
	if (where[1] == 0) 
	{
		where[1] = bsize;
	}
	page_frames = (cm_frame_t **)calloc(total_pages, sizeof(cm_frame_t *));
	if (page_frames == NULL) 
	{
		panic("Could not allocate memory\n");
		errno = ENOMEM;
		return -1;
	}
	valid_start = (cm_pos_t *) calloc(total_pages, sizeof(cm_pos_t));
	if (valid_start == NULL)
	{
		panic("Could not allocate memory\n");
		free(page_frames);
		errno = ENOMEM;
		return -1;
	}
	valid_size = (cm_size_t *) calloc(total_pages, sizeof(cm_size_t));
	if (valid_size == NULL)
	{
		panic("Could not allocate memory\n");
		free(page_frames);
		free(valid_start);
		errno = ENOMEM;
		return -1;
	}
	dprintf("READ Stg 1: begin_page = %Lu, end_page = %Lu\n",
		begin_page, end_page);

	blocks.cb_handle = p;
	for (i = 0; i < total_pages; i++) 
	{
		int my_error, account_miss = 1;

		blocks.cb_page = i + begin_page;
		/* Dont account prefetched blocks as part of the MISS counter */
		if (prefetch_index >= 0 && blocks.cb_page >= prefetch_index)
		{
		    account_miss = 0;
		}
		/* beginning page */
		if (i == 0) 
		{ 
			if (total_pages != 1) 
			{ 
				/* user may not need the entire first page */
				valid_start[i] = where[0];
				valid_size[i] = bsize - where[0]; 
			}
			else 
			{
				/* user wants only a portion of the first page */
				valid_start[i] = where[0];
				valid_size[i] = where[1] - where[0];
			}
		}
		/* last page */
		else if (i == total_pages - 1) 
		{
			valid_start[i] = 0;
			valid_size[i] = where[1];
		}
		/* all other pages */
		else 
		{ 
			valid_start[i] = 0;
			valid_size[i] = bsize;
		}
		page_frames[i] = block_get(&blocks, &i /* unused */, (int *) &my_error, account_miss);
		if (page_frames[i] == NULL) 
		{
			cm_page_t j;

			/* cleanup whatever frames have been locked */
			for (j = 0; j < i; j++) 
			{
				block_put(page_frames[j]);
			}
			free(page_frames);
			free(valid_start);
			free(valid_size);
			errno = -my_error;
			return -1;
		}
	}
	dprintf("READ Stg 2: About to issue fetch for total_pages = %Lu\n", total_pages);
	if ((error = __CMGRINT_fetch_sync(total_pages, page_frames,
			valid_start, valid_size)) < 0)
	{
		for (i = 0; i < total_pages; i++)
		{
			block_put(page_frames[i]);
		}
		free(page_frames);
		free(valid_start);
		free(valid_size);
		errno = -error;
		return -1;
	}
#if 0
	{
	    struct file *filp = NULL;
	    int64_t *file_offsets = NULL;
	    int32_t *file_sizes = NULL, flag = 0;
	    cm_buffer_t *buffers = NULL;
	    int64_t handle = 0;
	    int64_t comp_size = 0;
	    int *comp = NULL, count = 0, total_not_uptodate = 0, flag_uptodate = 0;
	    int flag_notuptodate = 0, get_only_missing = 0, non_contig = 0, nfetch = 0;

	    nfetch = 0;
	    filp = CMGRINT_file_get(p);
	    if (filp)
	    {
		    struct qlist_head *entry = NULL;
		    /* start from wherever we left off */
		    for (entry = filp->cf_head; entry != &filp->cf_list;)
		    {
			    cm_frame_t *fr = NULL;
			    fr = qlist_entry(entry, cm_frame_t, cm_file);
			    lock_page(fr);
			    if (fr->cm_block.cb_page >= begin_page && fr->cm_block.cb_page <= end_page)
			    {
				    page_frames[fr->cm_block.cb_page - begin_page] = fr;
				    /* beginning page */
				    if (fr->cm_block.cb_page - begin_page == 0)
				    {
					    if (total_pages != 1) 
					    { 
						    /* user may not need the entire first page */
						    valid_start[0] = where[0];
						    valid_size[0] = bsize - where[0]; 
					    }
					    else 
					    {
						    /* user wants only a portion of the first page */
						    valid_start[0] = where[0];
						    valid_size[0] = where[1] - where[0];
					    }
				    }
				    /* last page */
				    else if (fr->cm_block.cb_page - end_page == 0)
				    {
					    valid_start[total_pages - 1] = 0;
					    valid_size[total_pages - 1] = where[1];
				    }
				    /* all other pages */
				    else 
				    { 
					    valid_start[fr->cm_block.cb_page - begin_page] = 0;
					    valid_size[fr->cm_block.cb_page - begin_page] = bsize;
				    }
				    if (!Page_Uptodate(fr))
				    {
					    valid_size[fr->cm_block.cb_page - begin_page] = 0;
					    total_not_uptodate++;
					    if (flag_uptodate == 1)
						non_contig++;
					    flag_notuptodate = 1;
				    }
				    else
				    {
					    if (flag_notuptodate == 1)
						non_contig++;
					    flag_uptodate = 1;
				    }
			    }
			    else
			    {
				    unlock_page(fr);
			    }
			    entry = entry->next;
		    }
		    if (entry != &filp->cf_list)
		    {
			/* remember where we left off */
			filp->cf_head = entry;
		    }
		    else
		    {
			/* set it to the first element of the list */
			filp->cf_head = filp->cf_list.next;
		    }
	    }
	    CMGRINT_file_put(filp);
	    /* one or more page frames need to be fetched */
	    if (total_not_uptodate != 0)
	    {
	    }
	}
#endif
	/* now walk through the pages and copy out the relevant data to user addresses */
	dprintf("READ Stg 3: About to copy relevant data to user address\n");
	for (i = 0; i < total_pages; i++) 
	{
		if (page_frames[i] != NULL)
		{
			dprintf("READ Stg 3: page %Lu valid_start: %Lu valid_size %u\n", 
				i + begin_page, valid_start[i], valid_size[i]);
			memcpy(ptr, (char *)page_frames[i]->cm_buffer + valid_start[i], valid_size[i]);
#ifdef VERBOSE_DEBUG
			print_hash((char *)page_frames[i]->cm_buffer + valid_start[i], valid_size[i]);
#endif
			total_count += valid_size[i];
			ptr += valid_size[i];
			/* put the page and mark it as a candidate for eviction essentially */
			block_put(page_frames[i]);
		}
		else
		{
			panic();
		}
	}
	dprintf("READ Stg 4, total bytes copied = %Lu\n", (int64_t) total_count);
	free(page_frames);
	free(valid_start);
	free(valid_size);
	return total_count;
}

int64_t CMGR_put_region(char *buffer, cm_handle_t p, 
	int64_t begin_byte, int64_t count, cmgr_synch_options_t *options)
{
	cm_block_t blocks;
	cm_page_t begin_page, end_page, i, total_pages;
	loff_t where[2];
	cm_frame_t **page_frames = NULL;
	size_t bsize = 0;
	int log_bsize = 0, error = 0;
	char *ptr = buffer;
	int64_t total_count = 0;

	bsize = BSIZE;
	log_bsize = LOG_BSIZE;
	/* Split the region into fixed size chunks */
	if (log_bsize < 0) {
	    begin_page = begin_byte / bsize;
	    end_page = (begin_byte + count - 1) / bsize;
	}
	else {
	    begin_page = (begin_byte >> log_bsize);
	    end_page = (begin_byte + count - 1) >> log_bsize;
	}
	/* Total number of pages that will be involved in this transaction */
	total_pages = (end_page - begin_page + 1);
	if (total_pages > BCOUNT)
	{
		panic("Cannot satisfy write request of size %Lu"
			"bytes/%Lu pages.\n", count, total_pages);
		panic("Try increasing BCOUNT and/or BSIZE\n");
		errno = EINVAL;
		return -1;
	}
	if (log_bsize < 0) {
	    where[0] = begin_byte - (begin_page * bsize);
	    where[1] = (begin_byte + count) % bsize;
	}
	else {
	    /* where[0] indicates the offset in the first page from where the user wishes to write data */
	    where[0] = begin_byte - (begin_page << log_bsize);
	    /* where[1] indicates the count in the last page upto which the user wishes to write data */
	    where[1] = (begin_byte + count) & (bsize - 1);
	}
	if (where[1] == 0) 
	{
		where[1] = bsize;
	}
	page_frames = (cm_frame_t **)calloc(total_pages, sizeof(cm_frame_t *));
	if (page_frames == NULL) 
	{
		panic("Could not allocate memory\n");
		errno = ENOMEM;
		return -1;
	}
	/* Try and allocate some buffers and fetch their contents from disk as well if need be */
	dprintf("WRITE Stg 1: begin_page = %Lu, end_page = %Lu\n",
		begin_page, end_page);

	blocks.cb_handle = p;
	for (i = 0; i < total_pages; i++) 
	{
		int my_error;
		blocks.cb_page = i + begin_page;
		/* Try to allocate a page for this write! All such blocks do not get accounted as part of a miss */
		page_frames[i] = block_get(&blocks, &i /* unused */, (int *) &my_error, 0);
		/* if allocation failed */
		if (page_frames[i] == NULL) 
		{
			cm_page_t j;

			/* put any frames that we may have fixed */
			for (j = 0; j < i; j++) 
			{
				block_put(page_frames[j]);
			}
			free(page_frames);
			errno = -my_error;
			return -1;
		}
	}
	dprintf("WRITE Stg 2: About to write for total_pages =  %Lu\n", total_pages);
	/* now walk through the pages and copy out the relevant data from user addresses */
	for (i = 0;i < total_pages; i++) 
	{
		cm_size_t valid_size;
		cm_pos_t valid_start;

		/* beginning page */
		if (i == 0) 
		{
			valid_start = where[0];
			if (total_pages == 1) 
			{
				valid_size = where[1] - where[0];
			}
			else
			{
				valid_size = bsize - where[0];
			}
		}
		else if (i == total_pages - 1) 
		{
			valid_start = 0;
			valid_size = where[1];
		}
		else 
		{
			valid_start = 0;
			valid_size = bsize;
		}
		memcpy(page_frames[i]->cm_buffer + valid_start, ptr, valid_size);
#ifdef VERBOSE_DEBUG
		print_hash(ptr, valid_size);
		print_hash(page_frames[i]->cm_buffer + valid_start, valid_size);
#endif
		total_count += valid_size;
		ptr += valid_size;
		dprintf("WRITE Stg 2: page %Lu valid_start: %Lu valid_size %u\n", 
			i + begin_page, valid_start, valid_size);
		/* mark the valid regions in the page */
		if ((error = CMGRINT_fixup_valid_regions(
				page_frames[i], valid_start, valid_size)) < 0)
		{
			cm_page_t j, fromwhere;

			/* If the user had requested synchronous writeout, we would have
			 * do a block_put on all the pages. else we need to do it only
			 * for those for which we have not yet begun
			 */
			if (!((options->cs_evict == 0)
				&& (options->cs_opt.keep.wb == 1))) {
			    fromwhere = i;
			}
			else {
			    fromwhere = 0;
			}
			/* put any frames that we may have fixed */
			for (j = fromwhere; j < total_pages; j++) 
			{
				block_put(page_frames[j]);
			}
			free(page_frames);
			errno = error;
			return -1;
		}
		/* mark the page as uptodate also */
		SetPageUptodate(page_frames[i]);
		/* mark the pages as dirty */
		SetPageDirty(page_frames[i]);
		/* 
		 * if user requested synchronous write out,
		 * do not unlock the pages.
		 */
		if (!((options->cs_evict == 0)
			&& (options->cs_opt.keep.wb == 1))) {
		    block_put(page_frames[i]);
		}
	}
	/* 
	 * If the user requested synchronous writeout do so now
	 */
	if (options->cs_evict == 0 && 
		options->cs_opt.keep.wb == 1) {
	    total_count = __CMGRINT_wb_sync(total_pages, page_frames);
	    /* unlock all pages */
	    for (i = 0; i < total_pages; i++) {
		block_put(page_frames[i]);
	    }
	}
	free(page_frames);
	dprintf("WRITE Stg 3 total_count = %Lu\n", (int64_t) total_count);

#ifdef MMAP_SUPPORT
	/* In order to support coherent mmap(), we have to mprotect those addresses */
	if ((error = cmgr_invalidate_mappings(p,
			begin_page, total_pages)) < 0)
	{
		panic("Could not invalidate mmapp'ed address range"
			"[%d]!\n", error);
	}
#endif
	return total_count;
}

static inline void gl_lock(void)
{
	int ret;

	lock_printf("GL lock\n"); 
	if ((ret = pthread_mutex_trylock(&global_options.mutex)) != 0) {
		lock_printf("GL lock about to BLOCK!\n");
		pthread_mutex_lock(&global_options.mutex);
	}
}

static inline void gl_unlock(void)
{
	lock_printf("GL unlock\n");
	pthread_mutex_unlock(&global_options.mutex);
}

void CMGRINT_mark_page_free(cm_frame_t *fr)
{
	/* Acquire the global mutex lock */
	gl_lock();
	fr->cm_private = NULL;
	/* mark this page as free indeed */
	fr->cm_ref = 0;
	fr->cm_fix = 0;
	fr->cm_error = 0;
	/* clean page */
	ClearPageDirty(fr);
	/* invalid page */
	SetPageInvalid(fr);
	/* not uptodate */
	ClearPageUptodate(fr);
	/* reset the handle and block identification */
	fr->cm_block.cb_page = -1;
	memset(fr->cm_block.cb_handle, 0, HANDLESIZE);
	/* Fill the page with zeroes */
	memset(fr->cm_buffer, 0, BSIZE);
	/* add it to the free list */
	qlist_add_tail(&fr->cm_hash, &cm_free);
	/* mark it as free also */
	SetPageFree(fr);
	/* Reset the valid zone counts, start and sizes */
	free(fr->cm_valid_start);
	free(fr->cm_valid_size);
	fr->cm_valid_count = 0;
	fr->cm_valid_start = NULL;
	fr->cm_valid_size = NULL;

	global_options.num_free_frames++;
	/* indicate the availability of free frames */
	pthread_cond_broadcast(&global_options.avail);
	/* unlock the global mutex lock */
	gl_unlock();
	return;
}

/* Wait for a page frame to appear on the free list */
cm_frame_t* CMGRINT_wait_for_free(void)
{
	cm_frame_t *p = NULL;

	/* obtain the global mutex lock */
	gl_lock();
	/*
	 * FIXME: Are we under memory pressure? Signal the harvester.
	 * We need to make this a bit more intelligent,
	 * by adaptively increasing or decreasing the threshold
	 * at which we decide to wake up the harvester thread
	 * depending upon the allocation times in the recent past.
	 */
	if (!qlist_empty(&cm_free) 
		&& global_options.num_free_frames < global_options.low_water) 
	{
		/* signal that we need more free frames */
		lock_printf("Nudge harvester thread\n");
		pthread_cond_broadcast(&global_options.needed);
	}
	while (qlist_empty(&cm_free)) {
		lock_printf("Poking harvester thread and Waiting for a free frame [%u < %u]\n",
				global_options.num_free_frames, global_options.low_water);
		/* signal that we need more free frames */
		pthread_cond_broadcast(&global_options.needed);
		/* be prepared to wait for some time */
		pthread_cond_wait(&global_options.avail, &global_options.mutex);
		lock_printf("Obtained a free frame [%u]\n", global_options.num_free_frames);
	}
	/* Aha! We do have some free frames on the list now */
	p = qlist_entry(cm_free.next, cm_frame_t, cm_hash);
	qlist_del(cm_free.next);
	global_options.num_free_frames--;
	gl_unlock();
	/*
	 * Make sure that this frame is really on the free list
	 * and does not belong to any file list 
	 */
	Assert(Page_Free(p) && !Page_File(p));
	return p;
}


static void handler(int sig, siginfo_t *info, void *unused)
{
	void *handle = NULL;

	handle = pthread_getspecific(key);
	if (handle) {
		free(handle);
		pthread_setspecific(key, NULL);
		dprintf("Harvester thread exiting!\n");
		pthread_exit(NULL);
	}
	return;
}

static void *cm_harvester(void *unused)
{
	static int victim = 0;
	int num_freed_per_cycle = 0, num_written_per_cycle = 0, num_fixed = 0;
	cm_frame_t *fr;
	cm_block_t  old_block;
	sigset_t set;

	/* try to create a key */
	pthread_once(&once, key_create);

	sigfillset(&set);
	sigdelset(&set, SIGKILL);
	sigdelset(&set, SIGUSR2);

	pthread_sigmask(SIG_SETMASK, &set, NULL);

	old_block.cb_handle = 
		(cm_handle_t)calloc(1, HANDLESIZE);
	if (!old_block.cb_handle) 
	{
		panic("Harvester could not allocate handle!\n");
		exit(1);
	}
	pthread_setspecific(key, old_block.cb_handle);
	dprintf("Harvester created %ld. [Low: %d, High: %d, Total: %d]\n",
		pthread_self(), global_options.low_water, global_options.high_water, BCOUNT);
	gl_lock();

	while (1) 
	{
		if (global_options.num_free_frames 
				>= global_options.high_water) 
		{
			lock_printf("Harvester going to idle! [%u >= %u]\n",
					global_options.num_free_frames, global_options.high_water);
			pthread_cond_wait(&global_options.needed, &global_options.mutex);
		}
		else 
		{
			/*
			 * unlock the global_options mutex and yield for a while
			 * and hope that someone can get in and remove
			 * off frames from the free list.
			 */
			gl_unlock();
			fr = &cm_frames[victim];
			/* Try to obtain a WRITE lock on the page */
			if (trylock_page(fr) == 0) 
			{
				/* dont mess with such pages */
				if (fr->cm_fix > 0) 
				{
					dprintf("Skipping fixed page %u -> %Lu\n", fr->cm_id, fr->cm_block.cb_page);
					SCANS++;
					unlock_page(fr);
				}
				/* Page was already freed! */
				else if (Page_Free(fr)) 
				{
					num_fixed = 0;
					dprintf("Skipping freed page %u\n", fr->cm_id);
					SCANS++;
					unlock_page(fr);
				}
				/* page is not invalidated and young enough according to CLOCK */
				else if (!Page_Invalid(fr) 
					&& fr->cm_ref - CM_GCLOCK_AGE > 0) 
				{
					num_fixed = 0;
					fr->cm_ref -= CM_GCLOCK_AGE;
					dprintf("Skipping not-yet-old page %u -> %Lu [%u]\n",
						fr->cm_id, fr->cm_block.cb_page, fr->cm_ref);
					SCANS++;
					/* if the page is dirty, start trickling it out */
					if (Page_Dirty(fr)) 
					{
						int err;

						/* try and write-it-back */
						dprintf("Trickle WB of %d\n", fr->cm_id);
						err = __CMGRINT_wb_sync(1, &fr);
						num_written_per_cycle++;
					}
					unlock_page(fr);
				}
				else /* Potential candidate */
				{
					/* save the old block on this page */
					int failed_wb = 0;

					num_fixed = 0;
					fr->cm_ref = 0;
					memcpy(old_block.cb_handle, fr->cm_block.cb_handle, HANDLESIZE);
					old_block.cb_page = fr->cm_block.cb_page;
					/*
					 * We need to drop the page lock because of the lock-ordering
					 * constraints to prevent deadlocks!
					 * BLOCK Hash-chain lock > page locks .
					 * This is taken care of in cmgr_block_del() function.
					 */
					if (CMGRINT_block_del(fr, &old_block, 0) < 0) 
					{
						dprintf("Skipping potential candidate page %u -> %Lu\n",
							fr->cm_id, fr->cm_block.cb_page);
						SCANS++;
						/* This victim could not be freed! */
						goto advance_clock;
					}
					/* 
					 * Ahah! we were successful in deleting it from the block hash table 
					 * fr is still locked in write mode though! 
					 */
					if (Page_Dirty(fr)) 
					{
						dprintf("Delayed WB of %d\n", fr->cm_id);
						/* try and write-it-back */
						failed_wb = __CMGRINT_wb_sync(1, &fr);
					}
					/* 
				 	 * remove it from the file list as well, propogate any 
					 * errors in case of failed writebacks! Internally
					 * the function drops the lock on fr. Do this
					 * only if the page has not been invalidated!
					 */
					if (!Page_Invalid(fr)) 
					{
						Assert(Page_File(fr));
						dprintf("Harvester trying to delete %u -> %Lu from file list\n",
							fr->cm_id, fr->cm_block.cb_page);
						CMGRINT_file_del(fr, failed_wb < 0 ? failed_wb : 0);
					}
					else 
					{ 
						/* page has been invalidated and hence should not be part of any file list */
						Assert(!Page_File(fr));
						unlock_page(fr);
					}
					/*
					 * At this point frame is clean, and has been
					 * removed from both the block and the file
					 * hash tables. Hence it is unreachable. Therefore
					 * we dont need to re-acquire lock on fr
					 */
					CMGRINT_mark_page_free(fr);
					HARVESTS++;
					dprintf("Successfully freed page %u\n", fr->cm_id);
					num_freed_per_cycle++;

					/* lets yield if we have freed a certain number of pages */
					if (num_freed_per_cycle
						+ num_written_per_cycle >= global_options.batch_ratio)
					{
						num_freed_per_cycle = 0;
						num_written_per_cycle = 0;
						pthread_yield();
					}
				}
			}
			else
			{
				/* 
				 * clearly if we could not get a write lock,
				 * then the page was locked and hence in use.
				 * So we don't touch!
				 */
				num_fixed++;
				if (num_fixed >= BCOUNT)
				{
					panic("Harvester thread detected all frames to be FIXED/LOCKED! Exiting!\n");
					panic("Try increasing BCOUNT/BSIZE to avoid such situations.\n");
					exit(1);
				}
			}
advance_clock:
			/* Re-acquire the mutex lock and continue harvesting */
			gl_lock();
			victim = (victim + 1) % BCOUNT;
		} /* end else */
	} /* end while(1) */
	/* unreached */
	return NULL;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */
