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
#include "capfs_kernel_config.h"
#include "capfs_linux.h"
#include "capfs_bufmap.h"


#undef CAPFS_BUFMAP_DEBUG

/* userbuf structure; nobody else needs to know how it's implemented */
struct userbuf {
	int ct; /* count of such pages */
	struct page **pages; /* mapped pages */
};

/* capfs_map_userbuf(rw, buf, size, opaqueptr)
 *
 * Takes a description of a buffer in the current process's address
 * space and also brings the pages into memory and locks them in preparation for
 * I/O.
 *
 * PARAMETERS:
 * rw = CAPFS_BUF_READ or CAPFS_BUF_WRITE
 * buf = virtual address of current process's buffer
 * size = size of buffer
 *
 * When finished with this userbuf, one should call:
 *   capfs_unmap_userbuf(opaqueptr);
 * This will free the resources.
 *
 * Returns 0 on success, -errno on failure.  
 */
int capfs_map_userbuf(int rw, char *buf, size_t size, void **opaqueptr)
{
	int i, err;
	struct userbuf *ubuf;

	/* ensure user has proper access to the region */
	if (rw == CAPFS_BUF_WRITE) 
	{
		err = verify_area(VERIFY_WRITE, buf, size);
	}
	else 
	{
		err = verify_area(VERIFY_READ, buf, size);
	}
	if (err) 
	{
		printk("capfs_map_userbuf: verify_area failed\n");
		return err;
	}
	if (PAGE_ALIGN((unsigned long) buf) != (unsigned long) buf) {
		printk("capfs_map_userbuf: mis-aligned user buffer pointer (front)\n");
		return -EINVAL;
	}
	if (PAGE_ALIGN(((unsigned long) buf + size)) != ((unsigned long) buf + size))
	{
		printk("capfs_map_userbuf: mis-aligned user buffer pointer (back)\n");
		return -EINVAL;
	}
	if (size < 0 || size % PAGE_SIZE != 0) 
	{
		/* redundant check? */
		printk("capfs_map_userbuf: size(%d) is not aligned to page size or is invalid\n", size);
		return -EINVAL;
	}

	if ((ubuf = (struct userbuf *) kmalloc(sizeof(struct userbuf), GFP_KERNEL)) == NULL) 
	{
		printk("capfs_map_userbuf: kmalloc failed\n");
		return -ENOMEM;
	}
	ubuf->ct = size / PAGE_SIZE;
	ubuf->pages = (struct page **) kmalloc(ubuf->ct * sizeof(struct page *), GFP_KERNEL);
	if (ubuf->pages == NULL) 
	{
		printk("capfs_map_userbuf: kmalloc failed\n");
		kfree(ubuf);
		return -ENOMEM;
	}
	/* map the pages */
	down_read(&current->mm->mmap_sem);

	err = get_user_pages(current, current->mm, (unsigned long) buf /* start */, 
			ubuf->ct /* len */, 1 /* write */, 0 /* dont force */,
			ubuf->pages /* array of physical pages */, NULL /* vmas */);

	up_read(&current->mm->mmap_sem);

	if (err < 0) 
	{
		printk("capfs_map_userbuf: get_user_pages failed %d\n", err);
		kfree(ubuf->pages);
		kfree(ubuf);
		return err;
	}
	/* check if we were able to map in the entire set of pages */
	if (err != ubuf->ct)
	{
		printk("capfs_map_userbuf: get_user_pages did not return(%d) what we gave(%d)\n",
				err, ubuf->ct);
		for (i = 0; i < err; i++) 
		{
			SetPageError(ubuf->pages[i]);
			page_cache_release(ubuf->pages[i]);
		}
		kfree(ubuf->pages);
		kfree(ubuf);
		return -ENOMEM;
	}
	for (i = 0; i < ubuf->ct; i++) 
	{
		flush_dcache_page(ubuf->pages[i]);
	}
	*opaqueptr = (void *) ubuf;
#ifdef CAPFS_BUFMAP_DEBUG
	printk("capfs_map_userbuf passing back %lx\n", (unsigned long) ubuf);
#endif
	return 0;
}

/* capfs_unmap_userbuf()
 *
 * Handles freeing of resources associated with a previously allocated
 * kiovec.
 */
void capfs_unmap_userbuf(void *opaqueptr)
{
	int i;
	struct userbuf *ubuf = (struct userbuf *) opaqueptr;

#ifdef CAPFS_BUFMAP_DEBUG
	printk("capfs_unmap_userbuf freeing %lx\n", (unsigned long) ubuf);
#endif
	for (i = 0; i < ubuf->ct; i++)
	{
		page_cache_release(ubuf->pages[i]);
	}
	kfree(ubuf->pages);
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
unsigned long capfs_copy_to_userbuf(void *opaque_ptr, 
		void __user * user_from, unsigned long size)
{
	int i;
	void *to_kaddr = NULL;
	int amt_copied = 0, amt_remaining = 0, ret = 0, cur_copy_size;
	struct userbuf *ubuf = (struct userbuf *) opaque_ptr;
	void __user *offset = user_from;

	if (ubuf == NULL) 
	{
		return -EINVAL;
	}
	i = 0;
	while (amt_copied < size)
	{
		amt_remaining = (size - amt_copied);
		cur_copy_size = (amt_remaining > PAGE_SIZE ? PAGE_SIZE : amt_remaining);
		to_kaddr = kmap(ubuf->pages[i]);
		ret = copy_from_user(to_kaddr, offset, cur_copy_size);
		kunmap(ubuf->pages[i]);
		if (ret)
		{
			PERROR("copy_from_user failed\n");
			return -EIO;
		}
		i++;
		offset     += cur_copy_size;
		amt_copied += cur_copy_size;
	}
	return 0;
}

/* capfs_copy_from_userbuf()
 *
 * Returns 0 on success, whatever error value copy_from_user() spit out
 * on failure.
 */
unsigned long capfs_copy_from_userbuf(void __user *user_to, 
		void *opaque_ptr, unsigned long size)
{
	int i;
	void *from_kaddr = NULL;
	int amt_copied = 0, amt_remaining = 0, ret = 0, cur_copy_size;
	struct userbuf *ubuf = (struct userbuf *) opaque_ptr;
	void __user *offset = user_to;

	if (ubuf == NULL) 
	{
		return -EINVAL;
	}
	i = 0;
	while (amt_copied < size)
	{
		amt_remaining = (size - amt_copied);
		cur_copy_size = (amt_remaining > PAGE_SIZE ? PAGE_SIZE : amt_remaining);
		from_kaddr = kmap(ubuf->pages[i]);
		ret = copy_to_user(offset, from_kaddr, cur_copy_size);
		kunmap(ubuf->pages[i]);
		if (ret)
		{
			PERROR("copy_to_user failed\n");
			return -EIO;
		}
		i++;
		offset     += cur_copy_size;
		amt_copied += cur_copy_size;
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
