/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#ifndef __DYN_FDSET_H__
#define __DYN_FDSET_H__

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

/* struct dyn_fdset:
 * Replacement for fd_set that supports a variably-sized region for
 * storing the bits that determine what fds are selected.
 * 
 * Fields:
 * magic - set to DFD_MAGIC on allocation, 0 on free, to help with
 *   debugging
 * alloc_size - allocated size, in bytes
 * set - pointer to data region storing bits
 *
 * Semantics:
 * - if we haven't allocated space for a FD, then it is not set.
 * - if user sets a bit we haven't allocated space for, we allocate
 *    space and then set it.
 *
 * Usage:
 * - a dyn_fdset must be initialized via dfd_init() before use.
 * - a dyn_fdset must be freed with dfd_finalize() if it is no longer
 *   needed, or memory will be lost.
 * - a dyn_fdset must be cleaned up via dfd_finalize() if it is going to
 *   be re-initialized.  Note, however, that re-initialization is
 *   usually unnecessary; DFD_ZERO() should do the trick in most cases.
 */
struct dyn_fdset {
	int magic;
	int32_t alloc_size;
	void *set;
};

typedef struct dyn_fdset dyn_fdset;

/* initialization/cleanup functions */
int dfd_init(dyn_fdset *dset, int startsz);
int dfd_finalize(dyn_fdset *dset);

/* the four traditional fd_set operations */
int dfd_set(int fd, dyn_fdset *dset);
int dfd_clr(int fd, dyn_fdset *dset);
int dfd_isset(int fd, dyn_fdset *dset);
int dfd_zero(dyn_fdset *dset);

/* copy function */
int dfd_copy(dyn_fdset *dest, dyn_fdset *src);

/* we need a select that will operate on these guys too */
int dfd_select(int n, dyn_fdset *readfds, dyn_fdset *writefds, dyn_fdset
*exceptfds, struct timeval *timeout);

/* debugging functions */
int dfd_dump(dyn_fdset *dset);

/* some defines to make things look more like the old fd_set calls */
#define DFD_SET(__a,__b) dfd_set((__a),(__b))
#define DFD_CLR(__a,__b) dfd_clr((__a),(__b))
#define DFD_ISSET(__a,__b) dfd_isset((__a),(__b))
#define DFD_ZERO(__a) dfd_zero((__a))

#ifdef FD_SETSIZE
#define DFD_MIN_ALLOCSZ ((FD_SETSIZE < 256) ? 256 : FD_SETSIZE)
#else
#define DFD_MIN_ALLOCSZ 256
#endif

enum {
	DFD_MAGIC = 0x4321dcba
};

#endif
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
