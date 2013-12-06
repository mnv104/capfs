/*
 * capfs_bufmap.c copyright (c) 1999 Clemson University, all rights reserved.
 *
 * Written by Rob Ross and Phil Carns.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Contact:  Rob Ross    rbross@parl.clemson.edu
 *           Phil Carns  pcarns@parl.clemson.edu
*/

/* Functions to handle mapping user buffers into kiovecs and to clean up
 * afterwards.
 *
 * These are the externally available functions implemented here:
 * capfs_map_userbuf() - maps a region in the current process's vma
 *    into a kiovec.
 * capfs_unmap_userbuf() - cleans up after a mapping
 * capfs_copy_to_userbuf() - copies data from a buffer in the current
 *    process's vma space into a userbuf
 * 
 */
#define __NO_VERSION__
#include "capfs_kernel_config.h"

#include <linux/module.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/iobuf.h>
#include "capfs_bufmap.h"

#include <asm/pgtable.h>

#undef CAPFS_BUFMAP_DEBUG

/* only compile this if we have kiovec fns */
static void dump_kiobufinfo(struct kiobuf *iobuf);

/* userbuf structure; nobody else needs to know how it's implemented */
struct userbuf {
	int ct;
	struct kiobuf *vec;
};

/* capfs_map_userbuf(rw, buf, size, kiovp)
 *
 * Takes a description of a buffer in the current process's address
 * space and creates a kiovec structure describing the region.  This
 * also brings the pages into memory and locks them in preparation for
 * I/O.
 *
 * PARAMETERS:
 * rw = CAPFS_BUF_READ or CAPFS_BUF_WRITE
 * buf = virtual address of current process's buffer
 * size = size of buffer
 * vecp = address of pointer in which to store the address of the kiovec
 *
 * NOTES:
 * This function always creates a kiovec with a single kiobuf in it.
 *
 * When finished with this userbuf, one should call:
 *   capfs_unmap_userbuf(iobuf);
 * This will free the resources.
 *
 * Returns 0 on success, -errno on failure.  The address of the new
 * kiovec is returned in the pointer whose address is passed in vecp.
 */
int capfs_map_userbuf(int rw, char *buf, size_t size, void **vecp)
{
	int err;
	struct kiobuf *vec;
	struct userbuf *ubuf;
#ifdef HAVE_ALLOC_KIOVEC_SZ
	int vsz = KIO_MAX_SECTORS;
#endif

	/* ensure user has proper access to the region */
	if (rw == CAPFS_BUF_WRITE) err = verify_area(VERIFY_WRITE, buf, size);
	else err = verify_area(VERIFY_READ, buf, size);
	if (err) {
		printk("capfs_map_userbuf: verify_area failed\n");
		return err;
	}

	if ((ubuf = (struct userbuf *) kmalloc(sizeof(struct userbuf),
	GFP_KERNEL)) == NULL) {
		printk("capfs_map_userbuf: kmalloc failed\n");
		return -ENOMEM;
	}

#ifdef HAVE_ALLOC_KIOVEC_SZ
	if ((err = alloc_kiovec_sz(1, &vec, &vsz)) != 0) {
#else
	if ((err = alloc_kiovec(1, &vec)) != 0) {
#endif
		printk("capfs_map_userbuf: alloc_kiovec failed\n");
		kfree(ubuf);
		return err;
	}

	/* map_user_kiobuf() can handle non-page-aligned addresses and odd
	 * sizes
	 */
	if ((err = map_user_kiobuf((rw == CAPFS_BUF_WRITE) ? 0 : 1, vec,
	(unsigned long) buf, size)))
	{
		printk("capfs_map_userbuf: map_user_kiobuf failed\n");
		kfree(ubuf);
#ifdef HAVE_ALLOC_KIOVEC_SZ
		free_kiovec_sz(1, &vec, &vsz);
#else
		free_kiovec(1, &vec);
#endif
		return err;
	}

	ubuf->ct = 1;
	ubuf->vec = vec;

	*vecp = (void *) ubuf;
#ifdef CAPFS_BUFMAP_DEBUG
	printk("capfs_map_userbuf passing back %lx\n", (unsigned long) ubuf);
#endif

	dump_kiobufinfo(vec);

	return 0;
}

/* capfs_unmap_userbuf()
 *
 * Handles freeing of resources associated with a previously allocated
 * kiovec.
 */
void capfs_unmap_userbuf(void *vp)
{
	int i;
#ifdef HAVE_ALLOC_KIOVEC_SZ
	int vsz = KIO_MAX_SECTORS;
#endif
	struct kiobuf *vec;
	struct userbuf *ubuf = (struct userbuf *) vp;

#ifdef CAPFS_BUFMAP_DEBUG
	printk("capfs_unmap_userbuf freeing %lx\n", (unsigned long) ubuf);
#endif
	for (i=0, vec=ubuf->vec; i < ubuf->ct; i++) {
#ifdef CAPFS_BUFMAP_DEBUG
		printk("calling unmap_kiobuf for %lx\n", (unsigned long) vec);
#endif
		unmap_kiobuf(vec);
		vec++;
	}
	
#ifdef CAPFS_BUFMAP_DEBUG
	printk("calling free_kiovec for %lx\n", (unsigned long) &(ubuf->vec));
#endif
#ifdef HAVE_ALLOC_KIOVEC_SZ
	free_kiovec_sz(ubuf->ct, &(ubuf->vec), &vsz);
#else
	free_kiovec(ubuf->ct, &(ubuf->vec));
#endif
	kfree(ubuf);
	return;
}

/* capfs_copy_to_userbuf()
 *
 * Copies data from a buffer in the current process vma space into one
 * of our userbufs.
 *
 * NOTE:
 * This has to be done in a bunch of steps, because the pages aren't
 * necessarily contiguous in the userbuf.  Probably there is a better
 * way to get this done, but I don't know what it is right now.  Maybe
 * someone will read this comment, know a better solution, and email me
 * <smile>?  In any case, it's not a hack per se, just a bit
 * inefficient.
 *
 * REMINDER:
 * copy_from_user(kernel_to, user_from, size)
 * copy_to_user(user_to, kernel_from, size)
 *
 * Returns 0 on success, whatever error value copy_from_user() spit out
 * on failure.
 */
unsigned long capfs_copy_to_userbuf(void *vp, const void *user_from,
unsigned long size)
{
	int i, j;
	unsigned long err, pgsz, buflen;
	struct kiobuf *vec;
	char *from, *to;
	struct userbuf *buf = (struct userbuf *) vp;

	from = (char *) user_from;
	vec = buf->vec;

	for (i=0; i < buf->ct; i++) {
		buflen = vec->length;

		/* do first page separately to handle offset */
		to = (char *) page_address(vec->maplist[0]) + vec->offset;
		pgsz = (PAGE_SIZE-vec->offset > size) ? size : (PAGE_SIZE-vec->offset);
		if (pgsz > buflen) pgsz = buflen;

#ifdef CAPFS_BUFMAP_DEBUG
		printk("pc_to_ub copying %ld bytes from %lx in user space.\n",
		pgsz, (unsigned long) from);
#endif
		if ((err = copy_from_user(to, from, pgsz)) != 0) return err;
		from += pgsz;
		size -= pgsz;
		buflen -= pgsz;

		/* do remaining pages */
		for (j=1; j < vec->nr_pages && buflen > 0 && size > 0; j++) {
			to = (char *) page_address(vec->maplist[j]);
			pgsz = (PAGE_SIZE > size) ? size : PAGE_SIZE;
			if (pgsz > buflen) pgsz = buflen;

#ifdef CAPFS_BUFMAP_DEBUG
			printk("pc_to_ub copying %ld bytes from %lx in user space.\n",
			pgsz, (unsigned long) from);
#endif
			if ((err = copy_from_user(to, from, pgsz)) != 0) return err;
			from += pgsz;
			size -= pgsz;
			buflen -= pgsz;
		}
		vec++;
	}
	
	return 0;
}

/* capfs_copy_from_userbuf()
 *
 * Returns 0 on success, whatever error value copy_from_user() spit out
 * on failure.
 */
unsigned long capfs_copy_from_userbuf(const void *user_to, void *vp,
unsigned long size)
{
	int i, j;
	unsigned long err, pgsz, buflen;
	struct kiobuf *vec;
	char *from, *to;
	struct userbuf *buf = (struct userbuf *) vp;

	to = (char *) user_to;
	vec = buf->vec;

	for (i=0; i < buf->ct; i++) {
		buflen = vec->length;

		/* do first page separately to handle offset */
		from = (char *) page_address(vec->maplist[0]) + vec->offset;
		pgsz = (PAGE_SIZE-vec->offset > size) ? size : (PAGE_SIZE-vec->offset);
		if (pgsz > buflen) pgsz = buflen;

#ifdef CAPFS_BUFMAP_DEBUG
		printk("pc_to_ub copying %ld bytes to %lx in user space.\n",
		pgsz, (unsigned long) from);
#endif
		if ((err = copy_to_user(to, from, pgsz)) != 0) return err;
		to += pgsz;
		size -= pgsz;
		buflen -= pgsz;

		/* do remaining pages */
		for (j=1; j < vec->nr_pages && buflen > 0 && size > 0; j++) {
			from = (char *) page_address(vec->maplist[j]);
			pgsz = (PAGE_SIZE > size) ? size : PAGE_SIZE;
			if (pgsz > buflen) pgsz = buflen;

#ifdef CAPFS_BUFMAP_DEBUG
			printk("pc_to_ub copying %ld bytes from %lx in user space.\n",
			pgsz, (unsigned long) from);
#endif
			if ((err = copy_to_user(to, from, pgsz)) != 0) return err;
			to += pgsz;
			size -= pgsz;
			buflen -= pgsz;
		}
		vec++;
	}
	
	return 0;
}

static void dump_kiobufinfo(struct kiobuf *iobuf)
{
#ifdef CAPFS_BUFMAP_DEBUG
	int i;

	printk("dump_kiobufinfo called for %lx:\n", (unsigned long) iobuf);

	printk(
	"   nr_pages = %d, array_len = %d, offset = %d, length = %d, locked = %d\n",
	iobuf->nr_pages, iobuf->array_len, iobuf->offset, iobuf->length,
	iobuf->locked);

	printk("   pagelist: ");
	for (i=0; i < iobuf->nr_pages; i++)
		printk("%lx ", (unsigned long) iobuf->pagelist[i]);

	printk("\n   maplist:\n");
	for (i=0; i < iobuf->nr_pages; i++) {
		if (iobuf->maplist[i] == NULL)
			printk("     <null>\n");
		else {
			printk("     %lx: offset = %lx, count = %d, flags = %lx\n",
			(unsigned long) iobuf->maplist[i], iobuf->maplist[i]->offset,
			iobuf->maplist[i]->count.counter, iobuf->maplist[i]->flags);
		}
	}
#endif
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
