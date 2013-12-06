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
#include "hcache.h"
#include "sha.h"

enum {H_READ = 0, H_WRITE = 1, CHUNK_SIZE = 16384};

enum {USE_SHA1 = 0};

/* bsize is the size of the hashes, bcount is the number of hashes that can be cached, rest are hash-table sizes */
static int bsize = 0, bcount = 0, btsize = 0, bftsize = 0;
/* Units of computing hash. */
static int chunk_size = CHUNK_SIZE;

static void crypt_init(void)
{
	sha1_init();
	return;
}

static void crypt_finalize(void)
{
	sha1_finalize();
	return;
}

/* file hash/comparison routines */

static int compare_file(void *key, void *entry_key)
{
	cm_handle_t f1 = (cm_handle_t) key;
	cm_handle_t f2 = (cm_handle_t) entry_key;

	if (strcmp(((struct handle *)f1)->name, ((struct handle *)f2)->name) == 0) 
	{
		return 1;
	}
	return 0;
}

static int file_hash(void *key)
{
	cm_handle_t f1 = (cm_handle_t ) key;
	int hash_value = 0, i;
	char *name = ((struct handle *)f1)->name;

	for (i = 0; i < strlen(name); i++) {
		hash_value += abs(hash1(name[i]));
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

static void
compute_hashes(struct user_ptr *uptr)
{
	struct stat statbuf;
	int fd, i, start_chunk;
	char *filename = ((struct handle *)uptr->p)->name;
	void *file_addr;
	size_t size = 0;

	if (stat(filename, &statbuf) < 0) {
		fprintf(stderr, "No such file: %s!\n", filename);
		for (i = 0; i < uptr->nframes; i++) {
			uptr->completed[i] = -errno;
		}
		return;
	}
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "%s could not be opened! %s\n", filename, strerror(errno));
		for (i = 0; i < uptr->nframes; i++) {
			uptr->completed[i] = -errno;
		}
		return;
	}
	if ((file_addr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		for (i = 0; i < uptr->nframes; i++) {
			uptr->completed[i] = -errno;
		}
		close(fd);
		return;
	}
	start_chunk = (uptr->offsets[0] / uptr->sizes[0]);
	/* what is the offset into the real file? */
	size = start_chunk * chunk_size;
	for (i = start_chunk; i < (start_chunk + uptr->nframes); i++) {
		size_t input_length = 0;
		size_t hash_lengths = 0;

		if (size + chunk_size <= statbuf.st_size) {
			input_length = chunk_size;
		}
		else {
			if (size < statbuf.st_size) {
				input_length = statbuf.st_size - size;
			}
			else {
				uptr->completed[i - start_chunk] = 0;
				size += chunk_size;
				continue;
			}
		}
		if (sha1((char *)file_addr + i * chunk_size, input_length,
						(unsigned char **)&uptr->buffers[i-start_chunk], &hash_lengths) < 0) {
			fprintf(stderr, "Could not compute hash!\n");
			uptr->completed[i - start_chunk] = -errno;
			break;
		}
		else {
			uptr->completed[i - start_chunk] = hash_lengths;
		}
		size += chunk_size;
	}
	munmap(file_addr, statbuf.st_size);
	close(fd);
	return;
}

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
static long hash_buffered_read_begin(cm_handle_t p, 
		int number, cm_buffer_t *buffers, size_t *sizes, int64_t *offsets)
{
		int ret;
		struct user_ptr *uptr = NULL;

		/* Actually the bulk of the work is done only at the time of the complete routine */
		if ((uptr = post_io(p, number, (char **) buffers,
						sizes, offsets, H_READ, &ret)) == NULL)
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
static int* hash_buffered_read_complete(long _uptr)
{
		struct user_ptr *uptr = NULL;
		int *completed;

		uptr = (struct user_ptr *) _uptr;
		completed = uptr->completed;
		/*
		 * Compute the hashes for the file
		 * here. Note, at some point
		 * this would become an RPC call, 
		 * right now, I just compute it locally
		 */
		compute_hashes(uptr);
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
static long hash_buffered_write_begin(cm_handle_t p, 
		int number, cm_buffer_t *buffers, size_t *sizes, int64_t *offsets)
{
		int ret;
		struct user_ptr *uptr = NULL;

		if ((uptr = post_io(p, number, (char **) buffers,
						sizes, offsets, H_WRITE, &ret)) == NULL)
		{
			panic("hash_buffered_write: could not post write %d\n", ret);
			return ret;
		}
		return (long) uptr;
}

/*
 * We don't have to writeback the hashes from the cache.
 */
static int* hash_buffered_write_complete(long _uptr)
{
		struct user_ptr *uptr = NULL;
		int *completed = NULL, i;

		uptr = (struct user_ptr *) _uptr;
		completed = (uptr->completed);
		for (i = 0; i < uptr->nframes; i++) {
			completed[i] = uptr->sizes[i];
		}
		/* Deallocate the user pointer */
		dealloc_user_ptr(uptr);
		return completed;
}

/****** A slower, hcache code that is less memory hogging *******/

static void hcache_init_complex(struct hcache_options *opt)
{
	cmgr_options_t options;
	int ret;
	int csize, handle_size = sizeof(struct handle), crypto = USE_SHA1;
	char *envp, *str, *output_fname = NULL;
	hread_begin hr_begin = NULL;
	hread_complete hr_complete = NULL;
	hwrite_begin hw_begin = NULL;
	hwrite_complete hw_complete = NULL;

	/* setenv("CMGR_DEBUG", "true", 1); */
	if (opt) {
		hr_begin = opt->hr_begin;
		hr_complete = opt->hr_complete;
		hw_begin = opt->hw_begin;
		hw_complete = opt->hw_complete;
	}
	crypt_init();
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
	
	bsize = EVP_MD_size(EVP_sha1());
	/* see if some environment variables have been set */
	if ((envp = getenv("CMGR_CRYPTO")) != NULL) 
	{
		if (strncmp(envp, "sha1", 4) == 0) {
			crypto = USE_SHA1;
		}
		else {
			dprintf("Unhandled crypto! Defaulting to SHA-1\n");
		}
	}
	/* units of computing hashes */
	if ((envp = getenv("CMGR_CHUNK_SIZE")) != NULL)
	{
		csize = strtol(envp, &str, 10);
		if (*str == '\0') {
			chunk_size = csize;
		}
	}
	/* sizes of the hashes themselves that are cached */
	if ((envp = getenv("CMGR_BSIZE")) != NULL) 
	{
		bsize = strtol(envp, &str, 10);
		if (*str != '\0') {
			bsize = EVP_MD_size(EVP_sha1()); 
		}
		else {
			/* we just need it to be a multiple of sha1_size */
			assert(bsize == EVP_MD_size(EVP_sha1()));
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
	/* Not providing a block comparison/hash routine */
	options.compare_block = NULL;
	options.block_hash = NULL;
	/* file comparison and hashing routine need to be provided */
	options.compare_file = compare_file;
	options.file_hash = file_hash;
	/*
	 * readpage_*() and writepage_*() routines need to be 
	 * supplied to buffer manager 
	 */
	options.readpage_begin = (hr_begin == NULL) ? hash_buffered_read_begin : hr_begin;
	options.readpage_complete = (hr_complete == NULL) ? hash_buffered_read_complete : hr_complete;
	options.writepage_begin = (hw_begin == NULL) ? hash_buffered_write_begin : hw_begin;
	options.writepage_complete = (hw_complete == NULL) ? hash_buffered_write_complete : hw_complete;

	/* initialize the cache manager stuff */
	if ((ret = CMGR_init(&options)) < 0) 
	{
		panic("Cache Manager could not be initialized: %s\n", strerror(-ret));
		exit(1);
	}
	dprintf("Cache Manager initialized\n");
	return;
}

static void hcache_finalize_complex(void)
{
	/* free up all memory */
	crypt_finalize();
	/* finalize the cache manager stuff */
	CMGR_finalize();
	dprintf("Finalized the Hash cache manager\n");
}

/*
 * prefetch_index is the index beyond which we are just doing a hcache read-ahead.
 * This is needed for proper hcache stats accounting...
 */
static int64_t hcache_get_complex(char *fname, int64_t begin_chunk, int64_t nchunks, int64_t prefetch_index, void *buf)
{
	/* read through the buffer manager */
	char *ptr = buf;
	struct handle h;
	loff_t begin_byte;
	ssize_t min_count;
	int64_t total_count;
	cmgr_synch_options_t options;

	if (fname == NULL || buf == NULL)
	{
		panic("Could not get region of the file. Invalid parameters\n");
		return -EINVAL;
	}
	begin_byte = begin_chunk * bsize;
	min_count = bsize * nchunks;
	
	memset(&options, 0, sizeof(options));
	strncpy(h.name, fname, MAXNAMELEN);

	/* sets errno internally */
	if ((total_count = CMGR_get_region(ptr, &h, 
					begin_byte, min_count, prefetch_index, &options)) < 0)
	{
		panic("Could not get region of the file through the cache\n");
		return -1;
	}
	dprintf("begin_byte = %lld, min_count = %d, total_count = %lld\n",
			begin_byte, min_count, total_count);
	return total_count;
}

static int64_t hcache_put_complex(char *fname, int64_t begin_chunk, int64_t nchunks, const void *buf)
{
	char *ptr = (char *)buf;
	int64_t total_count = 0;
	ssize_t count = 0;
	struct handle h;
	loff_t begin_byte;
	cmgr_synch_options_t options;

	if (fname == NULL || buf == NULL)
	{
		panic("Could not put region of the file. Invalid parameters\n");
		return -EINVAL;
	}
	memset(&options, 0, sizeof(options));
	begin_byte = begin_chunk * bsize;
	count = nchunks * bsize;
	strncpy(h.name, fname, MAXNAMELEN);
	
	/* Sets errno internally */
	if ((total_count = CMGR_put_region(ptr, &h,
					begin_byte, count, &options)) < 0)
	{
		panic("Could not put region of the file through the cache!\n");
		return -1;
	}
	return total_count;
}

/* function to clear the hash cache for a particular file */
static int hcache_clear_complex(char *filename)
{
	struct handle h;
	cmgr_synch_options_t options;

	if (filename == NULL)
	{
		panic("Could not clear hcache. Invalid parameters\n");
		return -EINVAL;
	}
	strncpy(h.name, filename, MAXNAMELEN);
	options.cs_evict = 1;
	/* Mark for eviction all hashes that belong to this file. This routine should block. */
	CMGR_synch_region(&h, 0, -1, &options, 1);
	return 0;
}

/* function to clear a specified range of the hash cache of a particular file */
static int hcache_clear_range_complex(char *filename, int64_t begin_chunk, int nchunks)
{
	struct handle h;
	cmgr_synch_options_t options;
	loff_t begin_byte;
	ssize_t count;

	if (filename == NULL || nchunks <= 0) {
		panic("Could not clear hcache range. Invalid parameters\n");
		return -EINVAL;
	}
	begin_byte = begin_chunk * bsize;
	count = nchunks * bsize;
	strncpy(h.name, filename, MAXNAMELEN);
	options.cs_evict = 0;
	/* mark as being invalid */
	options.cs_opt.keep.wb = 0;
	options.cs_opt.keep.synch = CM_INVALIDATE_SYNCH;
	/*
	 * Mark for eviction all hashes that belong to this file 
	 * that lie in the specified range.  
	 * IMPORTANT: The last parameter has to be set to 0,
	 * so that we don't race with hcache_get(), and deadlock
	 * the meta-data server.
	 */
	CMGR_synch_region(&h, begin_byte, count, &options, 0);
	return 0;
}

static void hcache_invalidate_complex(void)
{
	CMGR_invalidate();
	return;
}

/******** Faster and hopefully a simpler hcache code ********/

static void hcache_init_simple(struct hcache_options *opt)
{
	cmgr_options_t options;
	int ret;
	int csize, handle_size = sizeof(struct handle), crypto = USE_SHA1;
	char *envp, *str, *output_fname = NULL;
	hread_begin hr_begin = NULL;
	hread_complete hr_complete = NULL;
	hwrite_begin hw_begin = NULL;
	hwrite_complete hw_complete = NULL;

	setenv("CMGR_DEBUG", "true", 1); 

	if (opt) {
		hr_begin = opt->hr_begin;
		hr_complete = opt->hr_complete;
		hw_begin = opt->hw_begin;
		hw_complete = opt->hw_complete;
	}
	crypt_init();
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
	
	bsize = EVP_MD_size(EVP_sha1());
	/* see if some environment variables have been set */
	if ((envp = getenv("CMGR_CRYPTO")) != NULL) 
	{
		if (strncmp(envp, "sha1", 4) == 0) {
			crypto = USE_SHA1;
		}
		else {
			dprintf("Unhandled crypto! Defaulting to SHA-1\n");
		}
	}
	/* units of computing hashes */
	if ((envp = getenv("CMGR_CHUNK_SIZE")) != NULL)
	{
		csize = strtol(envp, &str, 10);
		if (*str == '\0') {
			chunk_size = csize;
		}
	}
	/* sizes of the hashes themselves that are cached */
	if ((envp = getenv("CMGR_BSIZE")) != NULL) 
	{
		bsize = strtol(envp, &str, 10);
		if (*str != '\0') {
			bsize = EVP_MD_size(EVP_sha1()); 
		}
		else {
			/* we just need it to be a multiple of sha1_size */
			assert(bsize == EVP_MD_size(EVP_sha1()));
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
	/* Not providing a block comparison/hash routine */
	options.compare_block = NULL;
	options.block_hash = NULL;
	/* file comparison and hashing routine need to be provided */
	options.compare_file = compare_file;
	options.file_hash = file_hash;
	/*
	 * readpage_*() and writepage_*() routines need to be 
	 * supplied to buffer manager 
	 */
	options.readpage_begin = (hr_begin == NULL) ? hash_buffered_read_begin : hr_begin;
	options.readpage_complete = (hr_complete == NULL) ? hash_buffered_read_complete : hr_complete;
	options.writepage_begin = (hw_begin == NULL) ? hash_buffered_write_begin : hw_begin;
	options.writepage_complete = (hw_complete == NULL) ? hash_buffered_write_complete : hw_complete;

	/* initialize the cache manager stuff */
	if ((ret = CMGR_simple_init(&options)) < 0) 
	{
		panic("Simple Cache Manager could not be initialized: %s\n", strerror(-ret));
		exit(1);
	}
	dprintf("Simple Hash cache manager initialized\n");
	return;
}

static void hcache_finalize_simple(void)
{
	/* free up all memory */
	crypt_finalize();
	/* finalize the cache manager stuff */
	CMGR_simple_finalize();
	dprintf("Finalized the Hash cache manager\n");
}

static int64_t hcache_get_simple(char *fname, int64_t begin_chunk, int64_t nchunks, int64_t prefetch_index, void *buf)
{
	char *ptr = (char *) buf;
	struct handle h;
	int64_t total_count = 0;

	if (fname == NULL || buf == NULL)
	{
		panic("Could not get region of the file. Invalid parameters\n");
		return -EINVAL;
	}
	strncpy(h.name, fname, MAXNAMELEN);
	dprintf("hcache_get_simple: name = %s\n", h.name);
	/* sets errno internally */
	if ((total_count = CMGR_simple_get(ptr, &h, 
					begin_chunk, nchunks, prefetch_index)) < 0)
	{
		panic("Failed simple_get of the file through the hcache\n");
		return -1;
	}
	dprintf("begin_chunk = %lld, nchunks = %lld, total_count = %lld\n",
			begin_chunk, nchunks, total_count);
	return total_count;
}


static int64_t hcache_put_simple(char *fname, int64_t begin_chunk, int64_t nchunks, const void *buf)
{
	char *ptr = (char *)buf;
	struct handle h;
	int64_t total_count = 0;

	if (fname == NULL || buf == NULL)
	{
		panic("Could not put region of the file. Invalid parameters\n");
		return -EINVAL;
	}
	strncpy(h.name, fname, MAXNAMELEN);
	dprintf("hcache_put_simple: name = %s\n", h.name);
	/* Sets errno internally */
	if ((total_count = CMGR_simple_put(ptr, &h,
					begin_chunk, nchunks)) < 0)
	{
		panic("Failed simple_put of the file through the cache!\n");
		return -1;
	}
	dprintf("begin_chunk = %lld, nchunks = %lld, total_count = %lld\n",
			begin_chunk, nchunks, total_count);
	return total_count;
}

static int hcache_clear_simple(char *fname)
{
	struct handle h;
	cmgr_synch_options_t options;

	if (fname == NULL)
	{
		panic("Could not clear region of the file. Invalid parameters\n");
		return -EINVAL;
	}
	strncpy(h.name, fname, MAXNAMELEN);
	options.cs_evict = 1;
	/* Mark for eviction all hashes that belong to this file. This routine should block. */
	return CMGR_simple_synch_region(&h, 0, -1, &options, 1);
}

static int hcache_clear_range_simple(char *filename, int64_t begin_chunk, int nchunks)
{
	struct handle h;
	cmgr_synch_options_t options;

	if (filename == NULL || nchunks <= 0 || begin_chunk < 0) {
		panic("Could not clear hcache range. Invalid parameters\n");
		return -EINVAL;
	}
	strncpy(h.name, filename, MAXNAMELEN);
	options.cs_evict = 0;
	/* mark as being invalid */
	options.cs_opt.keep.wb = 0;
	options.cs_opt.keep.synch = CM_INVALIDATE_SYNCH;
	/*
	 * Mark for eviction all hashes that belong to this file 
	 * that lie in the specified range.  
	 * IMPORTANT: The last parameter has to be set to 0,
	 * so that we don't race with hcache_get(), and deadlock
	 * the meta-data server.
	 * Is this still true?
	 */
	return CMGR_simple_synch_region(&h, begin_chunk, nchunks, &options, 0);
}

static void hcache_invalidate_simple(void)
{
	CMGR_simple_invalidate();
	return;
}

/*************** EXPORTED HCACHE API *****************/

static int organization = CAPFS_HCACHE_SIMPLE;

void hcache_init(struct hcache_options *opt)
{
	if (opt)
	{
		organization = opt->organization;
	}
	/* use the simple hcache api!  */
	if (CAPFS_HCACHE_SIMPLE == organization)
	{
		hcache_init_simple(opt);
	}
	else
	{
		hcache_init_complex(opt);
	}
	return;
}

void hcache_finalize(void)
{
	/* use the simple hcache api!  */
	if (CAPFS_HCACHE_SIMPLE == organization)
	{
		hcache_finalize_simple();
	}
	else
	{
		hcache_finalize_complex();
	}
	return;
}

int64_t hcache_get(char *fname, int64_t begin_chunk, int64_t nchunks, int64_t prefetch_index, void *buf)
{
	/* use the simple hcache api!  */
	if (CAPFS_HCACHE_SIMPLE == organization)
	{
		return hcache_get_simple(fname, begin_chunk, nchunks, prefetch_index, buf);
	}
	else
	{
		return hcache_get_complex(fname, begin_chunk, nchunks, prefetch_index, buf);
	}
}

int64_t hcache_put(char *fname, int64_t begin_chunk, int64_t nchunks, const void *buf)
{
	/* use the simple hcache api!  */
	if (CAPFS_HCACHE_SIMPLE == organization)
	{
		return hcache_put_simple(fname, begin_chunk, nchunks, buf);
	}
	else
	{
		return hcache_put_complex(fname, begin_chunk, nchunks, buf);
	}
}

int hcache_clear(char *fname)
{
	/* use the simple hcache api!  */
	if (CAPFS_HCACHE_SIMPLE == organization)
	{
		return hcache_clear_simple(fname);
	}
	else
	{
		return hcache_clear_complex(fname);
	}
}

int hcache_clear_range(char *filename, int64_t begin_chunk, int nchunks)
{
	/* use the simple hcache api!  */
	if (CAPFS_HCACHE_SIMPLE == organization)
	{
		return hcache_clear_range_simple(filename, begin_chunk, nchunks);
	}
	else
	{
		return hcache_clear_range_complex(filename, begin_chunk, nchunks);
	}
}

void hcache_invalidate(void)
{
	/* use the simple hcache api!  */
	if (CAPFS_HCACHE_SIMPLE == organization)
	{
		hcache_invalidate_simple();
	}
	else
	{
		hcache_invalidate_complex();
	}
	return;
}

void hcache_get_stats(cmgr_stats_t *stats, int reset)
{
	CMGR_get_stats(stats, reset);
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



