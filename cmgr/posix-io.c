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
#include <dlfcn.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <aio.h>
#include "cmgr.h"
#include "quicklist.h"
#include "mquickhash.h"
#include "posix-io.h"

/* Eventually we would like to use O_DIRECT for getting data from disks */
#ifndef O_DIRECT
#define O_DIRECT	0400000	/* direct disk access hint - currently ignored */
#endif

#ifdef MMAP_SUPPORT
/* sigsegv handler prototypes */
static void sigsegv_handler(int, siginfo_t *, void *);
#endif

enum {OPEN32 = 1, OPEN64 = 2};

/* handle cache code */

static struct mqhash_table *handle_table;

struct Handle {
	int64_t ino;
	int fd;
	int flag32or64;
	int open_flags; /* what was the flags with which this file is opened? */
	struct qlist_head hash;
};

static int compare_handle(void *key, struct mqhash_head *entry)
{
	struct Handle *h2 = NULL;
	int64_t *h1 = (int64_t *)key;
	
	h2 = qlist_entry(entry, struct Handle, hash);
	if (*h1 == h2->ino) {
		return 1;
	}
	return 0;
}

static int handle_hash(void *key, int table_size)
{
	int64_t *h1 = (int64_t *) key;
	int hash_value;

	hash_value = hash1(*h1);
	return hash_value % table_size;
}

int handle_cache_init(void)
{
	if ((handle_table = mqhash_init(compare_handle, handle_hash, 101)) 
			== NULL) {
		return -ENOMEM;
	}
	return 0;
}

void handle_cache_finalize(void)
{
	mqhash_finalize(handle_table);
}

int handle2fd(int64_t handle, int *flag32or64, int *open_flags)
{
	struct mqhash_head *entry = NULL;

	if ((entry = mqhash_search(handle_table, &handle)) != NULL) 
	{
		struct Handle *h = qlist_entry(entry, struct Handle, hash);
		Assert(h->ino == handle);
		*flag32or64 = h->flag32or64;
		*open_flags = h->open_flags;
		return h->fd;
	}
	*flag32or64 = -1;
	*open_flags = -1;
	return -1;
}

/* 
 * the reason the flags are pointers is so that we can
 * convey the current values in case the caller is interested.
 */
void add2handle(int64_t handle, int fd, int *flag32or64, int *open_flags)
{
	struct mqhash_head *entry = NULL;
	struct Handle *h;
	int hindex;

	if ((entry = mqhash_search(handle_table, &handle)) != NULL) 
	{
		h = qlist_entry(entry, struct Handle, hash);
		Assert(h->ino == handle);
		if (h->fd != fd) 
		{
			dprintf("re-adding fd(%d) for handle(%Lu)\n", fd, handle);
		}
		h->fd = fd;
		/* We dont update flag32or64 or open_flags in case we hit in the cache */
		*flag32or64 = h->flag32or64;
		*open_flags = h->open_flags;
		return;
	}
	h = (struct Handle *)calloc(1, sizeof(struct Handle));
	Assert(h);
	h->ino = handle;
	h->fd = fd;
	h->flag32or64 = *flag32or64;
	h->open_flags = *open_flags;

	hindex = handle_table->hash(&handle, handle_table->table_size);
	mqhash_wrlock(&handle_table->lock[hindex]);
	h->hash.next = h->hash.prev = NULL;
	mqhash_add(handle_table, &handle, &h->hash);
	mqhash_unlock(&handle_table->lock[hindex]);
	dprintf("added fd(%d) for handle(%Lu)\n", fd, handle);
	return;
}

void delhandle(int64_t handle)
{
	struct qlist_head *entry;
	struct Handle *h;

	entry = mqhash_search_and_remove(handle_table, &handle);
	if (entry) 
	{
		dprintf("deleting handle(%Lu)\n", handle);
		h = qlist_entry(entry, struct Handle, hash);
		free(h);
	}
	return;
}

/* file hash/comparison routines */

static int compare_file(void *key, void *entry_key)
{
	cm_handle_t f1 = (cm_handle_t) key;
	cm_handle_t f2 = (cm_handle_t) entry_key;

	if (((struct handle *)f1)->ino == ((struct handle *)f2)->ino) 
	{
		return 1;
	}
	return 0;
}

static int file_hash(void *key)
{
	cm_handle_t f1 = (cm_handle_t ) key;
	int hash_value;

	hash_value = abs(hash1(((struct handle *)f1)->ino));
	return hash_value;
}

#define IO_READ  0
#define IO_WRITE 1

static int getfd4handle(int64_t handle, int mode)
{
		int fd;
		struct stat64 sbuf64;
		struct stat sbuf;
		static char *flags2str(int);
		int flag32or64 = -1, open_flags = -1;

		fd = handle2fd(handle, &flag32or64, &open_flags);
		if (fd < 0) 
		{
			dprintf("Invalid fd (-1) for handle %Ld\n", handle);
		}
		else 
		{
			dprintf("Obtained fd (%u) for handle %Ld opened in %s mode with flags %s\n",
					fd, handle, 
					flag32or64 == OPEN64 ? "64-bit" : "32 bit", flags2str(open_flags));
		}
		if (flag32or64 == OPEN64) 
		{
			if (fstat64(fd, &sbuf64) < 0) 
			{
				dprintf("fstat64 error: %s\n", strerror(errno));
				return -errno;
			}
			Assert(sbuf64.st_ino == handle);
		}
		else
		{
			if (fstat(fd, &sbuf) < 0) 
			{
				dprintf("fstat error: %s\n", strerror(errno));
				return -errno;
			}
			Assert(sbuf.st_ino == handle);
		}
		/* Make sure that we are not reading from a writeonly file or viceversa */
		if (mode == IO_READ)
		{
			Assert((open_flags & 0x3) != O_WRONLY);
		}
		else
		{
			Assert((open_flags & 0x3) != O_RDONLY);
		}
		return fd;
}

struct user_ptr 
{
	sem_t				sem;
	struct aiocb **aiocb_array;
	int				nframes;
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
		if (ptr->aiocb_array)
		{
			int i;
			for (i = 0; i < ptr->nframes; i++)
			{
				free(ptr->aiocb_array[i]);
			}
			free(ptr->aiocb_array);
		}
		free(ptr);
	}
}

static struct user_ptr* alloc_user_ptr(int nframes)
{
		struct user_ptr *ptr = NULL;
		int i;

		ptr = (struct user_ptr *) calloc(1, sizeof(struct user_ptr));
		if (ptr)
		{
			/* Initialize the semaphore LOCKED */
			sem_init(&ptr->sem, 0, 0);
			ptr->nframes = nframes;
			ptr->aiocb_array = (struct aiocb **) 
				calloc(nframes, sizeof(struct aiocb *));
			if (!ptr->aiocb_array)
			{
				goto error_exit;
			}
			for (i = 0; i < nframes; i++)
			{
				ptr->aiocb_array[i] = (struct aiocb *)
					calloc(1, sizeof(struct aiocb));
				if (!ptr->aiocb_array[i])
				{
					goto error_exit;
				}
			}
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

/*
 * Thread-based callback function 
 */
static void completion_func(sigval_t val)
{
		struct user_ptr *uptr = NULL;
		int i;

		uptr = (struct user_ptr *) val.sival_ptr;
		for (i = 0; i < uptr->nframes; i++)
		{
			int ret;

			ret = aio_error(uptr->aiocb_array[i]);
			/* We need to check if there was any error during the page I/O */
			if (ret != 0)
			{
				/* if op is in progress?, we should go BUG() */
				if (ret == EINPROGRESS)
				{
					fprintf(stderr, "Operation cannot be in progress in completion routine\n");
					exit(1);
				}
				/* I/O on error on some cache frame! */
				else
				{
					/* negative error code on failure */
					uptr->completed[i] = -ret;
				}
			}
			/* we did finish! */
			else
			{
				uptr->completed[i] = aio_return(uptr->aiocb_array[i]);
			}
		}
		/* wake up the caller if any! */
		sem_post(&uptr->sem);
		return;
}

static struct user_ptr* 
post_io(cm_handle_t p, int nframes, char **buffers,
		size_t *sizes, int64_t *offsets, int mode, int *error)
{
		struct user_ptr *uptr = NULL;
		int i, fd, ret;
		struct sigevent ev;

		/* try to allocate the user ptr to keep track of the state */
		uptr = alloc_user_ptr(nframes);
		if (!uptr)
		{
			*error = -ENOMEM;
			return NULL;
		}
		/* Deliver asynchronous notification of completion by a threaded-callback function */
		ev.sigev_notify = SIGEV_THREAD;
		ev.sigev_value.sival_ptr = uptr;
		ev.sigev_notify_function = completion_func;
		ev.sigev_notify_attributes = NULL;

		/* For the given handle, get the file descriptor */
		fd = getfd4handle(((struct handle *)p)->ino, mode);
		if (fd < 0)
		{
			*error = fd;
			/* Explicitly free the completed array */
			free(uptr->completed);
			dealloc_user_ptr(uptr);
			return NULL;
		}
		/* Prepare requests to be submitted to the AIO_* subsystem */
		for (i = 0; i < nframes; i++)
		{
			(uptr->aiocb_array[i])->aio_fildes = fd;
			(uptr->aiocb_array[i])->aio_lio_opcode = (mode == IO_READ) ? LIO_READ : LIO_WRITE;
			(uptr->aiocb_array[i])->aio_reqprio = 0;
			(uptr->aiocb_array[i])->aio_buf = (char *) buffers[i];
			(uptr->aiocb_array[i])->aio_nbytes = sizes[i];
			(uptr->aiocb_array[i])->aio_offset = offsets[i];
		}
		/* Submit the request */
		ret = lio_listio(LIO_NOWAIT, uptr->aiocb_array, nframes, &ev);
		if (ret != 0)
		{
			/* error in submission */
			*error = -errno;
			/* Explicitly free the completed array */
			free(uptr->completed);
			dealloc_user_ptr(uptr);
			return NULL;
		}
		return uptr;
}

/*
 * readpage_begin() routine must return an opaque handle
 * on success and negative error code on failure.
 * This routine is invoked only on a cache miss.
 * Issue an I/O using the aio_calls and a thread notification
 * function. Use the complete() callback function
 * to wait for completion of the posted I/O.
 */ 
static long posix_buffered_read_begin(cm_handle_t p, 
		int number, cm_buffer_t *buffers, size_t *sizes, int64_t *offsets)
{
		int ret;
		struct user_ptr *uptr = NULL;

		/* whoohoo! try to post a read from the disk file */
		if ((uptr = post_io(p, number, (char **) buffers,
						sizes, offsets, IO_READ, &ret)) == NULL)
		{
			panic("posix_buffered_read: could not post read %d\n", ret);
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
static int* posix_buffered_read_complete(long _uptr)
{
		struct user_ptr *uptr = NULL;
		int *completed = NULL;

		uptr = (struct user_ptr *) _uptr;
		sem_wait(&uptr->sem);
		completed = (uptr->completed);
		/* Deallocate the user pointer */
		dealloc_user_ptr(uptr);
		return completed;
}

/*
 * writepage_begin() routine must return an opaque handle
 * on success and negative error code on failure.
 * This routine is invoked only on a cache miss.
 * Issue an I/O using the aio_calls and a thread notification
 * function. Use the complete function to wait on I/O completion
 * and/or notification.
 */ 
static long posix_buffered_write_begin(cm_handle_t p, 
		int number, cm_buffer_t *buffers, size_t *sizes, int64_t *offsets)
{
		int ret;
		struct user_ptr *uptr = NULL;

		/* whoohoo! try to post a write to the disk file */
		if ((uptr = post_io(p, number, (char **) buffers,
						sizes, offsets, IO_WRITE, &ret)) == NULL)
		{
			panic("posix_buffered_write: could not post write %d\n", ret);
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
static int* posix_buffered_write_complete(long _uptr)
{
		struct user_ptr *uptr = NULL;
		int *completed = NULL;

		uptr = (struct user_ptr *) _uptr;
		sem_wait(&uptr->sem);
		completed = (uptr->completed);
		/* Deallocate the user pointer */
		dealloc_user_ptr(uptr);
		return completed;
}

static int (*real_open)(const char*fname,int flags,mode_t mode);
static int (*real_creat64)(const char*fname,mode_t mode);
static int (*real_creat)(const char*fname,mode_t mode);
static int (*real_open64)(const char*fname,int flags,mode_t mode);
static int (*real_close)(int fd);
static int (*real_fsync)(int fd);
static int (*real_fdatasync)(int fd);
static int (*real_ftruncate)(int fd,off_t length);
static int (*real_ftruncate64)(int fd,off_t length);
static int (*real_unlink)(const char* pathname);
static int (*real_truncate)(const char* pathname,int length);
static int (*real_truncate64)(const char* pathname,int length);
static ssize_t (*real_read)(int, void*, size_t);
static ssize_t (*real_write)(int, const void*, size_t);
#ifdef MMAP_SUPPORT
static void*  (*real_mmap)(void* ptr, size_t length, int prot, int flags, int fd,
					 off_t offset);
static void*  (*real_mmap64)(void* ptr, size_t length, int prot, int flags, int fd,
					 loff_t offset);
static void*  (*real_mremap)(void* oldptr, size_t oldsize, size_t newsize,
					 unsigned long flags);
static int    (*real_munmap)(void *ptr, size_t length);
static int	  (*real_msync)(const void *ptr, size_t size, int flags);
static ssize_t (*real_sendfile)(int out_fd, int in_fd, off_t *offset, size_t count);
static ssize_t (*real_sendfile64)(int out_fd, int in_fd, off64_t *offset, size_t count);
#endif

//static void (*real_exit)(int status);

static void *dl_handle = NULL;

static int initialize_dl_handle(void)
{
	dl_handle = dlopen("/lib/libc.so.6", RTLD_LAZY);
	if (dl_handle == NULL) {
		panic("Could not get a handle on libc:%s\n", dlerror());
		return -1;
	}
	real_open = dlsym(dl_handle, "open");
	if (!real_open) {
		panic("Could not obtain function pointer (open): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_open64 = dlsym(dl_handle, "open64");
	if (!real_open64) {
		panic("Could not obtain function pointer (open64): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_close = dlsym(dl_handle, "close");
	if (!real_close) {
		panic("Could not obtain function pointer (close): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_fsync = dlsym(dl_handle, "fsync");
	if (!real_fsync) {
		panic("Could not obtain function pointer (fsync): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_fdatasync = dlsym(dl_handle, "fdatasync");
	if (!real_fdatasync) {
		panic("Could not obtain function pointer (fdatasync): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_ftruncate = dlsym(dl_handle, "ftruncate");
	if (!real_ftruncate) {
		panic("Could not obtain function pointer (ftruncate): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_ftruncate64 = dlsym(dl_handle, "ftruncate64");
	if (!real_ftruncate64) {
		panic("Could not obtain function pointer (ftruncate64): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_unlink = dlsym(dl_handle, "unlink");
	if (!real_unlink) {
		panic("Could not obtain function pointer (unlink): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_truncate = dlsym(dl_handle, "truncate");
	if (!real_truncate) {
		panic("Could not obtain function pointer (truncate): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_truncate64 = dlsym(dl_handle, "truncate64");
	if (!real_truncate64) {
		panic("Could not obtain function pointer (truncate64): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_read = dlsym(dl_handle, "read");
	if (!real_read) {
		panic("Could not obtain function pointer (read): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_write = dlsym(dl_handle, "write");
	if (!real_write) {
		panic("Could not obtain function pointer (write): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_creat = dlsym(dl_handle, "creat");
	if (!real_creat) {
		panic("Could not obtain function pointer (creat): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_creat64 = dlsym(dl_handle, "creat64");
	if (!real_creat) {
		panic("Could not obtain function pointer (creat64): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	/*
	real_exit = dlsym(dl_handle, "exit");
	if (!real_exit) {
		panic("Could not obtain function pointer (exit): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	*/
#ifdef MMAP_SUPPORT
	real_mmap = dlsym(dl_handle, "mmap");
	if (!real_mmap) {
		panic("Could not obtain function pointer (mmap): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_mmap64 = dlsym(dl_handle, "mmap64");
	if (!real_mmap64) {
		panic("Could not obtain function pointer (mmap64): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_mremap = dlsym(dl_handle, "mremap");
	if (!real_mremap) {
		panic("Could not obtain function pointer (mremap): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_munmap = dlsym(dl_handle, "munmap");
	if (!real_munmap) {
		panic("Could not obtain function pointer (munmap): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_msync = dlsym(dl_handle, "msync");
	if (!real_msync) {
		panic("Could not obtain function pointer (msync): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_sendfile = dlsym(dl_handle, "sendfile");
	if (!real_sendfile) {
		panic("Could not obtain function pointer (sendfile): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
	real_sendfile64 = dlsym(dl_handle, "sendfile64");
	if (!real_sendfile64) {
		panic("Could not obtain function pointer (sendfile64): %s\n", dlerror());
		dlclose(dl_handle);
		return -1;
	}
#endif
	return 0;
}

static inline void finalize_dl_handle(void)
{
	dlclose(dl_handle);
	return;
}

void __attribute__ ((constructor)) posix_io_init(void);
void __attribute__ ((destructor)) posix_io_finalize(void);

void posix_io_init(void)
{
	cmgr_options_t options;
#ifdef MMAP_SUPPORT
	struct sigaction saction;
#endif
	int bsize = 0, bcount = 0, btsize = 0, bftsize = 0, ret;
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
	dprintf("Initializing the POSIX I/O interceptor library\n");

	if (initialize_dl_handle() < 0) 
	{
		exit(1);
	}
	dprintf("DL handle initialized\n");

	if ((ret = handle_cache_init()) < 0) 
	{
		panic("Handle cache could not be initialized: %s\n", strerror(-ret));
		exit(1);
	}
	dprintf("Handle Cache initialized\n");

	/* see if some environment variables have been set */
	if ((envp = getenv("CMGR_BSIZE")) != NULL) 
	{
		bsize = strtol(envp, &str, 10);
		if (*str != '\0')
			bsize = 0; /* use defaults provided by buffer manager */
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
	options.readpage_begin = posix_buffered_read_begin;
	options.readpage_complete = posix_buffered_read_complete;
	options.writepage_begin = posix_buffered_write_begin;
	options.writepage_complete = posix_buffered_write_complete;

	/* initialize the cache manager stuff */
	if ((ret = CMGR_init(&options)) < 0) 
	{
		panic("Cache Manager could not be initialized: %s\n", strerror(-ret));
		handle_cache_finalize();
		exit(1);
	}
	dprintf("Cache Manager initialized\n");
#ifdef MMAP_SUPPORT
	/* Finally, we setup a SIGSEGV handler for supporting mmap type operations */
	memset(&saction, 0, sizeof(saction));
	saction.sa_sigaction = sigsegv_handler;
	saction.sa_flags = SA_SIGINFO | SA_NOMASK; 
	if (sigaction(SIGSEGV, &saction, NULL) < 0)
	{
		panic("Could not install a SIGSEGV signal handler: %s\n", strerror(errno));
		CMGR_finalize();
		finalize_dl_handle();
		handle_cache_finalize();
		exit(1);
	}
#endif
	return;
}

void posix_io_finalize(void)
{
	/* finalize the cache manager stuff */
	CMGR_finalize();
	finalize_dl_handle();
	handle_cache_finalize();
	dprintf("Finalized the POSIX I/O cache manager\n");
}

/*
void exit(int) __attribute__((noreturn)); 

void exit(int status)
{
	posix_io_finalize();
	real_exit(status);
}
*/

#ifdef DEBUG
static char* flags2str(int flags)
{
	int mask = 0x3;

	if ((flags & mask) == O_WRONLY) {
		return "O_WRONLY";
	}
	else if ((flags & mask) == O_RDWR) {
		return "O_RDWR";
	}
	else if ((flags & mask) == O_RDONLY) {
		return "O_RDONLY";
	}
	else {
		dprintf("flags=%x\n", flags);
		return "O_DONTKNOW";
	}
}
#endif

int creat(const char *fname, mode_t mode) 
{
	int fd;

	fd = real_creat(fname, mode);
	if (fd >= 0) {
		struct stat sbuf;
		int flag32or64 = OPEN32, open_flags = O_WRONLY | O_CREAT | O_TRUNC;

		fstat(fd, &sbuf);
		dprintf("creat: fname:%s fd %d opened with flags (%x:%s)\n",
				fname, fd, open_flags, flags2str(open_flags));
		add2handle(sbuf.st_ino, fd, &flag32or64, &open_flags);
	}
	return fd;
}

int creat64(const char *fname, mode_t mode) 
{
	int fd;

	fd = real_creat64(fname, mode);
	if (fd >= 0) {
		struct stat64 sbuf;
		int flag32or64 = OPEN64, open_flags = O_WRONLY | O_CREAT | O_TRUNC | O_LARGEFILE;

		fstat64(fd, &sbuf);
		dprintf("creat64: fname:%s fd %d opened with flags (%x:%s)\n",
				fname, fd, open_flags, flags2str(open_flags));
		add2handle(sbuf.st_ino, fd, &flag32or64, &open_flags);
	}
	return fd;
}

int open(const char* fname, int flags, ...)
{
	int fd, mode = -1;

	if (flags & O_CREAT) {
		va_list ap;

		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
		dprintf("open called with mode %x\n", mode);
	}
	
	fd = real_open(fname, flags, mode);
	if (fd >= 0) {
		struct stat sbuf;
		int flag32or64 = OPEN32, open_flags = flags;

		fstat(fd, &sbuf);
		dprintf("open: fname:%s fd %d opened with flags (%x:%s)\n",
				fname, fd, flags, flags2str(open_flags));
		add2handle(sbuf.st_ino, fd, &flag32or64, &open_flags);
	}
	return fd;
}

int open64(const char* fname, int flags, ...)
{
	int fd, mode = -1;

	if (flags & O_CREAT) {
		va_list ap;

		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
		dprintf("open64 called with mode %x\n", mode);
	}

	fd = real_open64(fname, flags, mode);
	if (fd >= 0) {
		struct stat64 sbuf;
		int flag32or64 = OPEN64, open_flags = flags;

		fstat64(fd, &sbuf);
		dprintf("open64: fname: %s fd %d opened with flags (%x:%s)\n",
				fname, fd, flags, flags2str(open_flags));
		add2handle(sbuf.st_ino, fd, &flag32or64, &open_flags);
	}
	return fd;
}

int close(int fd)
{
	if (fd >= 0) 
	{
		struct stat sbuf;
		struct handle h;
		cmgr_synch_options_t options;

		fstat(fd, &sbuf);
		h.ino = sbuf.st_ino;
		options.cs_evict = 0;
		/* writeback the data */
		options.cs_opt.keep.wb  = 1;
		/* Right now, we don't wish to synchronize the cache */
		options.cs_opt.keep.synch = CM_DONT_SYNCH;
		/* Whole file */
		CMGR_synch_region(&h, 0, -1, &options);
		/* flush any dirty pages belonging to this file */
		dprintf("Closing fd %u [%Ld]\n", fd, h.ino);
		delhandle(sbuf.st_ino);
	}
	return real_close(fd);
}

static ssize_t cmgr_read(int fd, void *buf, size_t count)
{
	/* read through the buffer manager */
	struct stat sbuf;
	int flag32or64 = -1, open_flags = -1;

	/* bogus count value */
	if (count < 0) 
	{
		errno = EINVAL;
		return -1;
	}
	
	/* check if it is a bogus file descriptor? */
	if (fstat(fd, &sbuf) < 0) { 
		return -1;
	}

	/* 
	 * given that open() may not be the only way to get fd's, we add stuff to
	 * the handle cache at this point to set up the association between
	 * this fd & its inode number. We also retrieve the flags in case
	 * we may want to add more Assertions later on here
	 */
	add2handle(sbuf.st_ino, fd, &flag32or64, &open_flags);

	/*
	 * Let the read routine through the buffer
	 * cache only if we are sure that it is a regular
	 * file, not otherwise
	 */
	if (S_ISREG(sbuf.st_mode)) 
	{
		char *ptr = buf;
		size_t min_count = 0;
		ssize_t total_count = 0;
		struct handle h;
		loff_t begin_byte;
		cmgr_synch_options_t options;

		memset(&options, 0, sizeof(options));
		/* Enquire the current position of where the read() wishes to begin from */
		begin_byte = lseek(fd, 0, SEEK_CUR);
		/* Disallow reads() beyond current end-of-file */
		min_count = min(sbuf.st_size - begin_byte, count);
		/* Disallow reads beyond current end-of-file */
		if (min_count <= 0) 
		{
			return 0;
		}
		h.ino = sbuf.st_ino;

		/* sets errno internally */
		if ((total_count = CMGR_get_region(ptr, &h, 
						begin_byte, min_count, &options)) < 0)
		{
			panic("Could not get region of the file through the cache\n");
			return -1;
		}
		/* update the file pointer to the amount of bytes successfully read */
		lseek(fd, total_count, SEEK_CUR);
		return total_count;
	}
	else 
	{
		/* not a regular file */
		return real_read(fd, buf, count);
	}
}

ssize_t read(int fd, void *buf, size_t count)
{
	ssize_t ret;

	ret = cmgr_read(fd, buf, count);
	dprintf("read on %d returned %ld\n", fd, (long) ret);
	return ret;
}

static ssize_t cmgr_write(int fd, const void *buf, size_t count)
{
	/* write through the buffer manager */
	struct stat sbuf;
	int flag32or64 = -1, open_flags = -1;

	/* bogus count value */
	if (count < 0) 
	{
		errno = EINVAL;
		return -1;
	}

	/* bogus file descriptor? */
	if (fstat(fd, &sbuf) < 0) 
	{
		return -1;
	}
	/* 
	 * given that open() may not be the only way to get fd's, we add stuff to
	 * the handle cache at this point to set up the association between
	 * this fd & its inode number. We also wish to retrieve the flags in
	 * case we may want to add more Assertions later on here.
	 */
	add2handle(sbuf.st_ino, fd, &flag32or64, &open_flags);

	/*
	 * only allow writes through the cache only for 
	 * regular files.
	 */
	if (S_ISREG(sbuf.st_mode)) 
	{
		char *ptr = (char *)buf;
		ssize_t total_count = 0;
		struct handle h;
		loff_t begin_byte;
		cmgr_synch_options_t options;

		/* FIXME: Currently we dont use the options parameter */
		memset(&options, 0, sizeof(options));
		/* Enquire the current position of where the write() wishes to begin from */
		begin_byte = lseek(fd, 0, SEEK_CUR);
		h.ino = sbuf.st_ino;
		
		/* Sets errno internally */
		if ((total_count = CMGR_put_region(ptr, &h,
						begin_byte, count, &options)) < 0)
		{
			panic("Could not put region of the file through the cache!\n");
			return -1;
		}
		lseek(fd, total_count, SEEK_CUR);
		return total_count;
	}
	else 
	{
		/* non-regular file */
		return real_write(fd, buf, count);
	}
}

ssize_t write(int fd, const void *buf, size_t count)
{
	ssize_t ret;

	ret = cmgr_write(fd, buf, count);
	dprintf("write on %d returned %ld\n", fd, (long) ret);
	return ret;
}

int fsync(int fd)
{
	/* get the handle and flush all the pages of this file */
	struct stat sbuf;
	
	/* bogus fd value */
	if (fstat(fd, &sbuf) < 0) 
	{
		return -1;
	}
	do
	{
		struct handle h;
		cmgr_synch_options_t options;

		h.ino = sbuf.st_ino;
		options.cs_evict = 0;
		/* writeback the data */
		options.cs_opt.keep.wb  = 1;
		/* Right now, we don't wish to synchronize the cache */
		options.cs_opt.keep.synch = CM_DONT_SYNCH;
		/* Whole file */
		CMGR_synch_region(&h, 0, -1, &options);
		return real_fsync(fd);
	} while(0);
}

int fdatasync(int fd)
{
	/* get the handle and flush all the pages of this file */
	struct stat sbuf;
	
	/* bogus fd value */
	if (fstat(fd, &sbuf) < 0) 
	{
		return -1;
	}
	do 
	{
		struct handle h;
		cmgr_synch_options_t options;

		h.ino = sbuf.st_ino;
		options.cs_evict = 0;
		/* writeback the data */
		options.cs_opt.keep.wb  = 1;
		/* Right now, we don't wish to synchronize the cache */
		options.cs_opt.keep.synch = CM_DONT_SYNCH;
		/* Whole file */
		CMGR_synch_region(&h, 0, -1, &options);
		return real_fdatasync(fd);
	} while(0);
}

int ftruncate(int fd, off_t length)
{
	/* invalidate any pages in the cache as well */
	struct stat sbuf;

	if (fstat(fd, &sbuf) < 0) 
	{
		return -1;
	}
	if (length < sbuf.st_size) 
	{
		struct handle h;
		cmgr_synch_options_t options;

		h.ino = sbuf.st_ino;
		options.cs_evict = 1;
		/* Mark for eviction the specified blocks outside of the truncate */
		CMGR_synch_region(&h, 0, length, &options);
	}
	return real_ftruncate(fd, length);
}

int ftruncate64(int fd, loff_t length)
{
	/* invalidate any pages in the cache as well */
	struct stat64 sbuf;

	if (fstat64(fd, &sbuf) < 0) 
	{
		return -1;
	}
	if (length < sbuf.st_size) 
	{
		struct handle h;
		cmgr_synch_options_t options;

		h.ino = sbuf.st_ino;
		options.cs_evict = 1;
		/* Mark for eviction the specified blocks outside of the truncate */
		CMGR_synch_region(&h, 0, length, &options);
	}
	return real_ftruncate64(fd, length);
}

int unlink(const char *pathname)
{
	if (pathname) 
	{
		struct stat sbuf;
		struct handle h;
		cmgr_synch_options_t options;

		if (stat(pathname, &sbuf) < 0) 
		{
			return -1;
		}
		h.ino = sbuf.st_ino;
		options.cs_evict = 1;
		/* Mark for eviction all blocks that belong to this file */
		CMGR_synch_region(&h, 0, -1, &options);
		delhandle(sbuf.st_ino);
	}
	return real_unlink(pathname);
}

int truncate(const char* pathname, off_t length)
{
	struct stat sbuf;

	if (stat(pathname, &sbuf) < 0) 
	{
		return -1;
	}
	if (length < sbuf.st_size) 
	{
		struct handle h;
		cmgr_synch_options_t options;

		h.ino = sbuf.st_ino;
		options.cs_evict = 1;
		/* Mark for eviction the specified blocks outside of the truncate */
		CMGR_synch_region(&h, 0, length, &options);
	}
	return real_truncate(pathname, length);
}

int truncate64(const char* pathname, loff_t length)
{
	struct stat64 sbuf;

	if (stat64(pathname, &sbuf) < 0) 
	{
		return -1;
	}
	if (length < sbuf.st_size) 
	{
		struct handle h;
		cmgr_synch_options_t options;

		h.ino = sbuf.st_ino;
		options.cs_evict = 1;
		/* Mark for eviction the specified blocks outside of the truncate */
		CMGR_synch_region(&h, 0, length, &options);
	}
	return real_truncate64(pathname, length);
}

static inline char* print_prot(int prot)
{
	char *prot_array[8] = {
		"PROT_NONE", "PROT_READ",
		"PROT_WRITE", "PROT_READ|PROT_WRITE",
		"PROT_EXEC", "PROT_EXEC|PROT_READ",
		"PROT_EXEC|PROT_WRITE","PROT_EXEC|PROT_READ|PROT_WRITE",
	};
	prot &= 7;
	return prot_array[prot];
}

static inline char* print_flags(int flags)
{
	if (flags & MAP_SHARED)
		return "MAP_SHARED";
	if (flags & MAP_PRIVATE)
		return "MAP_PRIVATE";
	if (flags & MAP_FIXED)
		return "MAP_FIXED";
	if (flags & MAP_ANONYMOUS)
		return "MAP_ANONYMOUS";
	return "MAP_DONTKNOW";
}

#ifdef MMAP_SUPPORT
void* mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset)
{
	void *dummy_ptr = NULL;
	struct stat sbuf;

	dprintf("mmap -> length=%d, prot=%s, flags=%s, fd=%d, offset=%ld\n",
			length, print_prot(prot), print_flags(flags), fd, offset);
	if (!(flags & MAP_ANONYMOUS) && (fd < 0 || fstat(fd, &sbuf) < 0))
	{
		errno = EBADF;
		return MAP_FAILED;
	}
	if (!(flags & MAP_ANONYMOUS) 
			&& !(prot & PROT_EXEC) 
			&& !(prot & PROT_NONE)
			&& !(flags & MAP_FIXED)
			&& S_ISREG(sbuf.st_mode))
	{
		struct handle h;
		int ret;

		/* Do the real mmap, but make the permissions PROT_NONE */
		if ((dummy_ptr = real_mmap(NULL, length, PROT_NONE,
						 flags, fd, offset)) == MAP_FAILED) 
		{
			return MAP_FAILED;
		}
		h.ino = sbuf.st_ino;
		if ((ret=CMGR_add_mappings(&h, offset, length,
					dummy_ptr, prot, flags)) < 0)
		{
			errno = ret;
			panic("Could not register mmap address range!\n");
			real_munmap(dummy_ptr, length);
			return MAP_FAILED;
		}
	}
	else
	{
		/* Do the real mmap, but dont change the permission here */
		if ((dummy_ptr = real_mmap(NULL, length, prot,
						 flags, fd, offset)) == MAP_FAILED) 
		{
			return MAP_FAILED;
		}
	}
	return dummy_ptr;
}

void* mmap64(void *start, size_t length, int prot, int flags, int fd, loff_t offset)
{
	void *dummy_ptr = NULL;
	struct stat64 sbuf;
	
	dprintf("mmap64 length=%d, prot=%s, flags=%s, fd=%d, offset=%Ld\n",
			length, print_prot(prot), print_flags(flags), fd, offset);
	if (!(flags & MAP_ANONYMOUS) && (fd < 0 || fstat64(fd, &sbuf) < 0))
	{
		errno = EBADF;
		return MAP_FAILED;
	}
	if (!(flags & MAP_ANONYMOUS) 
			&& !(prot & PROT_EXEC) 
			&& !(prot & PROT_NONE)
			&& !(flags & MAP_FIXED)
			&& S_ISREG(sbuf.st_mode))
	{
		struct handle h;
		int ret;

		/* Do the real mmap64, but make the permissions PROT_NONE */
		if ((dummy_ptr = real_mmap64(NULL, length, PROT_NONE,
						 flags, fd, offset)) == MAP_FAILED) 
		{
			return MAP_FAILED;
		}
		h.ino = sbuf.st_ino;
		if ((ret = CMGR_add_mappings(&h, offset, length,
					dummy_ptr, prot, flags)) < 0)
		{
			errno = ret;
			panic("Could not register mmap address range!\n");
			real_munmap(dummy_ptr, length);
			return MAP_FAILED;
		}
	}
	else
	{
		/* Do the real mmap64, but don't change the permissions here */
		if ((dummy_ptr = real_mmap64(NULL, length, prot,
						 flags, fd, offset)) == MAP_FAILED) 
		{
			return MAP_FAILED;
		}
	}
	return dummy_ptr;
}

void* mremap(void *old_address, size_t old_size, size_t new_size, int flags)
{
	void *ptr;

	ptr = real_mremap(old_address, old_size, new_size, flags);
	/* Adjust the mappings internally. Don't know what to do on an error. */
	CMGR_remap_mappings(old_address, old_size, new_size, flags);
	return ptr;
}

int munmap(void *start, size_t length)
{
	CMGR_del_mappings(start, length);
	return real_munmap(start, length);
}

int msync(void *start, size_t length, int flags)
{
	/* flush the dirty file blocks, invalidate them */
	CMGR_sync_mappings((unsigned long) start, length, flags);
	/* and then flush the memory mapped pages to disk */
	return real_msync(start, length, flags);
}

ssize_t sendfile(int out_fd, int in_fd, off_t *offset, size_t count)
{
	struct handle h;
	struct stat sbuf;
	cmgr_synch_options_t options;

	if (fstat(in_fd, &sbuf) < 0)
	{
		return -1;
	}
	h.ino = sbuf.st_ino;
	options.cs_evict = 0;
	options.cs_opt.keep.wb = 1;
	options.cs_opt.keep.synch = CM_DONT_SYNCH;
	/* Flush the specified dirty file blocks. Don't invalidate them */
	CMGR_synch_region(&h, *offset, count, &options);
	/* and then call the real sendfile routine */
	return real_sendfile(out_fd, in_fd, offset, count);
}

ssize_t sendfile64(int out_fd, int in_fd, off64_t *offset, size_t count)
{
	struct handle h;
	struct stat64 sbuf;
	cmgr_synch_options_t options;

	if (fstat64(in_fd, &sbuf) < 0)
	{
		return -1;
	}
	h.ino = sbuf.st_ino;
	options.cs_evict = 0;
	options.cs_opt.keep.wb = 1;
	options.cs_opt.keep.synch = CM_DONT_SYNCH;
	/* Flush the specified dirty file blocks. No need to synchronize them */
	CMGR_synch_region(&h, *offset, count, &options);
	/* and then call the real sendfile64 routine */
	return real_sendfile64(out_fd, in_fd, offset, count);
}

static void sigsegv_handler(int sig_num, siginfo_t *info, void *ctx)
{
	unsigned long fault_address;

	if (sig_num != SIGSEGV || info->si_signo != SIGSEGV)
	{
		panic("Why on earth did we get this signal [%d %d]?\n", sig_num, info->si_signo);
		exit(1);
	}
	if (info->si_code == SEGV_MAPERR)
	{
		panic("Segmentation violation due to invalid address [%p]\n", info->si_addr);
		exit(1);
	}
	/* okay, so it was a SEGV_ACCERR, i.e an access violation error */
	fault_address = (unsigned long) info->si_addr;
	CMGR_fixup_mappings(fault_address);
	return;
}
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

