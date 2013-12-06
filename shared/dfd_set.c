/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <dfd_set.h>
#include <string.h>
#include <log.h>

/* struct dyn_fdset:
 * Replacement for fd_set that supports a variably-sized region for
 * storing the bits that determine what fds are selected.
 * 
 * Fields:
 * alloc_size - allocated size, in bytes
 * set - pointer to data region storing bits
 *
 * struct dyn_fdset {
 *    int magic;
 * 	int32_t alloc_size;
 * 	void *set;
 * };
 */

static int __dfd_offset(int fd);
static int __dfd_grow(dyn_fdset *dset, int fd);

/* dfd_init(*dset, startsz)
 *
 * Returns 0 on success, -errno on failure (invalid set, failure in grow).
 */
int dfd_init(dyn_fdset *dset, int startsz)
{
	if (dset->magic == DFD_MAGIC) {
		/* already initialized? */
		return -EINVAL;
	}

	dset->magic = DFD_MAGIC;
	dset->set = NULL;
	dset->alloc_size = 0; /* THIS IS PROBABLY UNNECESSARY */

	/* __dfd_grow() maintains the alloc_size field after initialization */
	if (__dfd_grow(dset, startsz) < 0) {
		dset->magic = 0;
		return -ENOMEM;
	}

	return 0;
}

/* dfd_finalize(*dset)
 *
 * Cleans up dset and frees any memory.
 *
 * Returns -errno on invalid set, 0 otherwise.
 */
int dfd_finalize(dyn_fdset *dset)
{
	if (dset->magic != DFD_MAGIC) return -EINVAL;

	if (dset->set != NULL) {
		free(dset->set);
	}
	dset->magic = 0;
	dset->set = NULL;
	dset->alloc_size = 0;

	return 0;
}

/* dfd_set(fd, *dset)
 *
 * Sets the bit corresponding to FD fd in the set dset.
 *
 * Returns -errno on failure, 0 otherwise.
 */
int dfd_set(int fd, dyn_fdset *dset)
{
	if (dset->magic != DFD_MAGIC || fd < 0) return -EINVAL;

	if (__dfd_offset(fd) >= dset->alloc_size) {
		/* see dfd_issset() for notes on this test.
		 *
		 * need to grow here.
		 */
		if (__dfd_grow(dset, fd) < 0) return -ENOMEM;
		/* fall through and set bit */
	}

	FD_SET(fd, (fd_set *)dset->set);
	return 0;
}

/* dfd_clr(fd, *dset)
 *
 * Clears the bit corresponding to FD fd from the set dset.
 *
 * Returns -errno on invalid parameter, 0 otherwise.
 */
int dfd_clr(int fd, dyn_fdset *dset)
{
	if (dset->magic != DFD_MAGIC || fd < 0) return -EINVAL;

	if (__dfd_offset(fd) >= dset->alloc_size) {
		/* see dfd_issset() for notes on this test.  */
		return 0;
	}

	FD_CLR(fd, (fd_set *)dset->set);
	return 0;
}

/* dfd_isset(fd, *dset)
 *
 * Checks if bit is set in dset for FD fd.
 *
 * Returns:
 * -errno on invalid parameter.
 * 0 if bit is not set.
 * 1 if bit is set.
 *
 * Notes:
 * - for compatibility with regular old FD_ISSET() please test for bit
 *   set with != 0 after looking for the -errno.
 */
int dfd_isset(int fd, dyn_fdset *dset)
{
	if (dset->magic != DFD_MAGIC || fd < 0) return -EINVAL;

	if (__dfd_offset(fd) >= dset->alloc_size) {
		/* remember semantics: if we don't have space for the bit, it is
		 * not set.
		 *
		 * need a >= here because offsets go 0...(size-1)
		 */
		return 0;
	}

	return (FD_ISSET(fd, (fd_set *)dset->set) == 0) ? 0 : 1;
}

/* dfd_zero(*dset)
 *
 * Note: can't just use FD_ZERO(); FD_ZERO() assumes the fd_set is of
 *  the default size...
 *
 * Returns -errno on failure, 0 on success.
 */
int dfd_zero(dyn_fdset *dset)
{
	if (dset->magic != DFD_MAGIC) return -EINVAL;

	memset(dset->set, 0, dset->alloc_size);
	return 0;
}

/* dfd_copy(*dest, *src)
 *
 * Copies the FD information in src into dest, resizing dest if
 * necessary.
 *
 * Returns -errno on failure, 0 on success.
 */
int dfd_copy(dyn_fdset *dest, dyn_fdset *src)
{
	/* both MUST be initialized before copy can occur */
	if (src->magic != DFD_MAGIC || dest->magic != DFD_MAGIC)
		return -EINVAL;

	/* destination needs to have enough space to hold the source set */
	if (dest->alloc_size < src->alloc_size) {
		/* quick and dirty estimation of FD using alloc_size */
		if (__dfd_grow(dest, src->alloc_size * 8) < 0) return -ENOMEM;

		if (dest->alloc_size < src->alloc_size) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_SHARED, "something unexpected occurred");
			return -EINVAL;
		}
	}

	/* now copy the data */
	memcpy(dest->set, src->set, src->alloc_size);
	return 0;
}

/* dfd_select(n, *readfds, *writefds, *exceptfds, *timeout)
 *
 * n is max FD + 1 as with regular select
 *
 * Checks readiness of file descriptors.  See man page for select() for
 * more info.
 *
 * Returns number of descriptors remaining in sets, which might be zero,
 * or -1 on error.
 *
 * IDEA: is there some optimization we can do to reduce the maximum FD
 * we look at based on the size of readfds and writefds?  We know if
 * there is not space for a bit that the bit is 0.
 */
int dfd_select(int n, dyn_fdset *readfds, dyn_fdset *writefds, dyn_fdset
*exceptfds, struct timeval *timeout)
{
	int ret;
	void *real_rfds = NULL, *real_wfds = NULL, *real_efds = NULL;

	/* select will assume that it can look through all valid sets up to
	 * FD n-1, so we need to grow any sets that aren't big enough already.
	 */
	if (readfds != NULL) {
		if (readfds->magic != DFD_MAGIC) {
			errno = EINVAL;
			return -1;
		}
		/* see dfd_isset() for why this test is >= */
		if (__dfd_offset(n-1) >= readfds->alloc_size) {
			if (__dfd_grow(readfds, n-1) < 0) {
				errno = ENOMEM;
				return -1;
			}
		}
		
		real_rfds = readfds->set;
	}
	if (writefds != NULL) {
		if (writefds->magic != DFD_MAGIC) {
			errno = EINVAL;
			return -1;
		}
		if (__dfd_offset(n-1) >= writefds->alloc_size) {
			if (__dfd_grow(writefds, n-1) < 0) {
				errno = ENOMEM;
				return -1;
			}
		}

		real_wfds = writefds->set;
	}
	if (exceptfds != NULL) {
		if (exceptfds->magic != DFD_MAGIC) {
			errno = EINVAL;
			return -1;
		}
		if (__dfd_offset(n-1) >= exceptfds->alloc_size) {
			if (__dfd_grow(exceptfds, n-1) < 0) {
				errno = ENOMEM;
				return -1;
			}
		}

		real_efds = exceptfds->set;
	}

	ret = select(n, real_rfds, real_wfds, real_efds, timeout);
	return ret;
}

/* __dfd_grow(*dset, fd)
 *
 * Allocates (or reallocates) memory to store bit set such that the 
 * set can store a bit for the given fd.
 *
 * The size is in terms of an FD because it simplifies the majority of
 * the uses in this code and also abstracts away the issue of data size
 * for the representation in case someone wants to use this call
 * externally (which I might disallow).
 *
 * We could combine the allocation cases, but I think it's easier to
 * understand the way it is.
 *
 * Returns -errno on failure, 0 on success.
 */
static int __dfd_grow(dyn_fdset *dset, int fd)
{
	int bytesz;

	/* always leave space for DFD_MIN_ALLOCSZ FDs */
	if (fd < DFD_MIN_ALLOCSZ) fd = DFD_MIN_ALLOCSZ;

	/* the FD_XXX calls work on full words AFAIK, so we need to make sure
	 * not to allocate some fraction of a word and screw things up.
	 *
	 * NFDBITS is the number of bits in a word.
	 *
	 * I hate using glibc code in slightly odd ways, but I hate
	 * re-implementing things even more...
	 *
	 * Do we need to worry about word-alignment too?  I don't know.
	 * Shouldn't have to, as fd_sets wouldn't necessarily be word aligned.
	 *
	 * We potentially overestimate here (or do we?) instead of bothering
	 * with a mod operation; it won't hurt to overestimate.
	 */
	bytesz = ((fd / NFDBITS) + 1) * (NFDBITS / 8);
	if (dset->set == NULL) {
		/* first use, simple case */
		if ((dset->set = malloc(bytesz)) == NULL) {
			/* mark as no longer valid */
			dset->magic = 0;
			return -ENOMEM;
		}
		memset(dset->set, 0, bytesz);
		dset->alloc_size = bytesz;
		/* drop through... */
	}
	else if (bytesz > dset->alloc_size) {
		/* need to realloc */
		void *oldptr = dset->set;

		if ((dset->set = realloc(dset->set, bytesz)) == NULL) {
			/* mark as no longer valid */
			dset->magic = 0;
			dset->alloc_size = 0;
			free(oldptr);
			return -ENOMEM;
		}
		/* zero out the new memory */
		memset(((char *)dset->set) + dset->alloc_size, 0,
		bytesz - dset->alloc_size);

		dset->alloc_size = bytesz;
		/* drop through... */
	}
	return 0;
}

/* __dfd_offset(fdnum)
 *
 * Returns the byte offset for the byte holding the bit referring to FD
 * fd in some dyn_fdset.
 *
 */
static int __dfd_offset(int fd)
{
	return (fd / 8);
}

int dfd_dump(dyn_fdset *dset)
{
	int i, empty=1;

	LOG(stderr, INFO_MSG, SUBSYS_SHARED, "magic is %s\n", 
			(dset->magic == DFD_MAGIC) ? "ok" : "bad");
	LOG(stderr, INFO_MSG, SUBSYS_SHARED, "alloc_size = %d bytes (max %d)\n",
			dset->alloc_size, dset->alloc_size*8);

	if (dset->magic != DFD_MAGIC) {
		LOG(stderr, INFO_MSG, SUBSYS_SHARED, "[skipping fd listing]\n\n");
		return 0;
	}

	for (i=0; __dfd_offset(i) < dset->alloc_size; i++) {
		if (DFD_ISSET(i, dset)) {
			LOG(stderr, INFO_MSG, SUBSYS_SHARED, "%d ", i);
			empty=0;
		}
	}

	return 0;
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
