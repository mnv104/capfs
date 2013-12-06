#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <sys/syscall.h>
#include <linux/unistd.h>
#include "cmgr.h"
#include "quicklist.h"
#include "mquickhash.h"
#include "dcache.h"

enum {D_READ = 0, D_WRITE = 1};

enum {USE_SHA1 = 0, BSIZE = 16384};

/* bsize is the size of the cached objects, bcount is the number of objects that can be cached, rest are hash-table sizes */
static int bsize = BSIZE, bcount = 0, btsize = 0, bftsize = 0;

/* file hash/comparison routines */

static int compare_file(void *key, void *entry_key)
{
	cm_handle_t f1 = (cm_handle_t) key;
	cm_handle_t f2 = (cm_handle_t) entry_key;

	if (strcmp(((struct handle *)f1)->hash, ((struct handle *)f2)->hash) == 0) 
	{
		return 1;
	}
	return 0;
}

static int file_hash(void *key)
{
	cm_handle_t f1 = (cm_handle_t ) key;
	int hash_value = 0, i;
	char *hash = ((struct handle *)f1)->hash;

	for (i = 0; i < strlen(hash); i++) {
		hash_value += abs(hash1(hash[i]));
	}
	return hash_value;
}

struct user_ptr 
{
	cm_handle_t    p;
	int 				mode;
	int				nframes;
	char 			  **buffers;
	size_t		  *sizes;
	int64_t		  *offsets;
	int			  *completed;
};

/*
* NOTE that we dont free the ->completed integer pointer
* since that is returned to the cache manager
*/
static void dealloc_user_ptr(struct user_ptr *ptr)
{
	if (ptr)
	{
		free(ptr);
	}
}

static struct user_ptr* alloc_user_ptr(cm_handle_t p, int mode, int nframes,
		char **buffers, size_t *sizes, int64_t *offsets)
{
		struct user_ptr *ptr = NULL;

		ptr = (struct user_ptr *) calloc(1, sizeof(struct user_ptr));
		if (ptr)
		{
			ptr->p = p;
			ptr->mode = mode;
			ptr->nframes = nframes;
			ptr->buffers = buffers;
			ptr->sizes = sizes;
			ptr->offsets = offsets;
			/* We try to allocate this at the last, so 
			 * that if we had to call dealloc_user_ptr()
			 * we still would not have to free this!
			 */
			ptr->completed = (int *) 
				calloc(nframes, sizeof(int));
			if (!ptr->completed)
			{
				goto error_exit;
			}
		}
		return ptr;
error_exit:
		dealloc_user_ptr(ptr);
		return NULL;
}

static inline char* convert(unsigned char* hash)
{
	char *fname = (char *) malloc(256);
	int i, count = 0;

	for (i = 0; i < 5; i++) {
		count += sprintf(fname + count, "%u", *(int *)(hash + i));
	}
	return fname;
}


/*
 * FIXME:
 * Partho, this will be eventually the RPC/Socket
 * interface to get a specified set of block(s) from the
 * server. For now, I am making this do a local 
 * fetch
 */
static void cas_get(struct user_ptr *uptr)
{
	char *filename, *hash = ((struct handle *)uptr->p)->hash;
	int fd;

	filename = convert(hash);
	fd = open(filename, O_RDONLY);
	if (fd >= 0) {
		int i;

		for (i = 0; i < uptr->nframes; i++) {
			uptr->completed[i] = 
				pread(fd, uptr->buffers[i], uptr->sizes[i], uptr->offsets[i]);
		}
		close(fd);
	}
	else {
		int i;

		for (i = 0; i < uptr->nframes; i++) {
			uptr->completed[i] = -errno;
		}
	}
	return;
}


/*
 * FIXME:
 * Partho, this will be eventually the RPC/Socket
 * Interface for the analogous put operation to the
 * data server. For now, I am making this a local disk write
 */
static void cas_put(struct user_ptr *uptr)
{
	char *hash = ((struct handle *)uptr->p)->hash;
	int fd;
	char *filename = NULL;

	filename = convert(hash);

	fd = open(filename, O_CREAT | O_RDWR, 0700);
	if (fd >= 0) {
		int i;

		for (i = 0; i < uptr->nframes; i++) {
			uptr->completed[i] = 
				pwrite(fd, uptr->buffers[i], uptr->sizes[i], uptr->offsets[i]);
		}
		close(fd);
	}
	else {
		int i;

		for (i = 0; i < uptr->nframes; i++) {
			uptr->completed[i] = -errno;
		}
	}
	return;
}

/*
 * Ideally, we would like to have server-side support for asynch. operations.
 * But that is something to play with in the future. Right, now even though
 * I have nice separation of operation initiation and completion, 
 * we are still operating synchronously! grr..
 */
static struct user_ptr* 
post_io(cm_handle_t p, int nframes, char **buffers,
		size_t *sizes, int64_t *offsets, int mode, int *error)
{
		struct user_ptr *uptr = NULL;

		/* try to allocate the user ptr to keep track of the state */
		uptr = alloc_user_ptr(p, mode, nframes, buffers, sizes, offsets);
		if (!uptr)
		{
			*error = -ENOMEM;
			return NULL;
		}
		return uptr;
}

/*
 * readpage_begin() routine must return an opaque handle
 * on success and negative error code on failure.
 * This routine is invoked only on a cache miss.
 * Use this to just setup information for computing hashes if need be
 */ 
static long cas_buffered_read_begin(cm_handle_t p, 
		int number, cm_buffer_t *buffers, size_t *sizes, int64_t *offsets)
{
		int ret;
		struct user_ptr *uptr = NULL;

		/* Actually the bulk of the work is done only at the time of the complete routine */
		if ((uptr = post_io(p, number, (char **) buffers,
						sizes, offsets, D_READ, &ret)) == NULL)
		{
			panic("hash_buffered_read: could not post read %d\n", ret);
			return ret;
		}
		return (long) uptr;
}

/*
 * Complete the I/O operation that was posted asynchronously
 * earlier. We return a pointer to an array of integers
 * that indicate the error codes in case of failed I/O
 * or amount of I/O completed.
 * Callers responsibility to free it.
 */
static int* cas_buffered_read_complete(long _uptr)
{
		struct user_ptr *uptr = NULL;
		int *completed = NULL;

		uptr = (struct user_ptr *) _uptr;
		completed = (uptr->completed);
		/*
		 * Read the data based on the hashes from the file
		 * here. Note, at some point
		 * this would become an RPC call, 
		 * right now, I just do it locally for now.
		 */
		cas_get(uptr);
		/* Deallocate the user pointer */
		dealloc_user_ptr(uptr);
		return completed;
}

/*
 * writepage_begin() routine must return an opaque handle
 * on success and negative error code on failure.
 * This routine is invoked only on a cache miss.
 * There is no need to WB crypto-hashes!!
 * So these will be simple no-ops.
 */ 
static long cas_buffered_write_begin(cm_handle_t p, 
		int number, cm_buffer_t *buffers, size_t *sizes, int64_t *offsets)
{
		int ret;
		struct user_ptr *uptr = NULL;

		if ((uptr = post_io(p, number, (char **) buffers,
						sizes, offsets, D_WRITE, &ret)) == NULL)
		{
			panic("cas_buffered_write: could not post write %d\n", ret);
			return ret;
		}
		return (long) uptr;
}

/*
 * We don't have to writeback the data from the cache. We always
 * write through the cache.
 */
static int* cas_buffered_write_complete(long _uptr)
{
		struct user_ptr *uptr = NULL;
		int *completed = NULL;

		uptr = (struct user_ptr *) _uptr;
		completed = (uptr->completed);
		/*
		 * Write the data based on the hashes from the file
		 * here. Note, at some point
		 * this would become an RPC call, 
		 * right now, I just do it locally for now.
		 */
		cas_put(uptr);
		/* Deallocate the user pointer */
		dealloc_user_ptr(uptr);
		return completed;
}


void dcache_init(void)
{
	cmgr_options_t options;
	int ret;
	int handle_size = sizeof(struct handle);
	char *envp, *str, *output_fname = NULL;

	output_fname = getenv("CMGR_OUTPUT");
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
	dprintf("Initializing the Hash Cache Manager!\n");
	
	/* sizes of the data that are being cached */
	if ((envp = getenv("CMGR_BSIZE")) != NULL) 
	{
		bsize = strtol(envp, &str, 10);
		if (*str != '\0') {
			bsize = 0;
		}
	}
	if ((envp = getenv("CMGR_BCOUNT")) != NULL) 
	{
		bcount = strtol(envp, &str, 10);
		if (*str != '\0')
			bcount = 0; /* use defaults provided by buffer manager */
	}
	if ((envp = getenv("CMGR_BTSIZE")) != NULL) 
	{
		btsize = strtol(envp, &str, 10);
		if (*str != '\0')
			btsize = 0; /* use defaults provided by buffer manager */
	}
	if ((envp = getenv("CMGR_BFTSIZE")) != NULL) 
	{
		bftsize = strtol(envp, &str, 10);
		if (*str != '\0')
			bftsize = 0; /* use defaults provided by buffer manager */
	}
	/* tuneable buffer manager parameters */
	options.co_output_fname = output_fname;
	options.co_bsize = bsize;
	options.co_bcount = bcount;
	options.co_block_table_size = btsize;
	options.co_file_table_size = bftsize;
	options.co_handle_size = handle_size;
	/* not providing a block comparison/hash routine.  */
	options.compare_block = NULL;
	options.block_hash = NULL;
	/* file comparison and hashing routine need to be provided */
	options.compare_file = compare_file;
	options.file_hash = file_hash;
	/*
	 * readpage_*() and writepage_*() routines need to be 
	 * supplied to buffer manager 
	 */
	options.readpage_begin = cas_buffered_read_begin;
	options.readpage_complete = cas_buffered_read_complete;
	options.writepage_begin = cas_buffered_write_begin;
	options.writepage_complete = cas_buffered_write_complete;

	/* initialize the cache manager stuff */
	if ((ret = CMGR_init(&options)) < 0) 
	{
		panic("Cache Manager could not be initialized: %s\n", strerror(-ret));
		exit(1);
	}
	dprintf("Cache Manager initialized\n");
	return;
}

void dcache_finalize(void)
{
	/* finalize the cache manager stuff */
	CMGR_finalize();
	dprintf("Finalized the Hash cache manager\n");
}

int dcache_get(char *hash, void *buf, size_t size)
{
	/* read through the buffer manager */
	char *ptr = buf;
	struct handle h;
	loff_t begin_byte;
	int total_count;
	cmgr_synch_options_t options;

	begin_byte = 0;

	memset(&options, 0, sizeof(options));
	strncpy(h.hash, hash, EVP_SHA1_SIZE);

	/* sets errno internally */
	if ((total_count = CMGR_get_region(ptr, &h, 
					begin_byte, size, -1, &options)) < 0)
	{
		panic("Could not get region of the file through the cache\n");
		return -1;
	}
	return total_count;
}

int dcache_put(char *hash, const void *buf, size_t size)
{
	char *ptr = (char *)buf;
	int total_count = 0;
	ssize_t count = 0;
	struct handle h;
	loff_t begin_byte;
	cmgr_synch_options_t options;

	/* Currently we use the options parameter to signal synchronous writeout */
	memset(&options, 0, sizeof(options));
	options.cs_evict = 0;
	options.cs_opt.keep.wb = 1;
	begin_byte = 0;
	count = size;
	strncpy(h.hash, hash, EVP_SHA1_SIZE);
	
	/* Sets errno internally */
	if ((total_count = CMGR_put_region(ptr, &h,
					begin_byte, count, &options)) < 0)
	{
		panic("Could not put region of the file through the cache!\n");
		return -1;
	}
	return total_count;
}

void dcache_get_stats(cmgr_stats_t *stats, int reset)
{
	CMGR_get_stats(stats, reset);
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



