/*
 * copyright (c) 2005 Murali Vilayannur, all rights reserved.
 *
 * Written by Murali Vilayannur
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
 * Contact:  Murali Vilayannur vilayann@cse.psu.edu
 */

#include "capfs_kernel_config.h"
#include "capfs_linux.h"
#include "ll_capfs.h"
#include "capfs_proc.h"

struct capfs_symlink {
	struct page* page;
	char   body[0];
};

static int capfs_symlink_filler(struct inode *inode, struct page *page)
{
	const unsigned int pgbase = offsetof(struct capfs_symlink, body);
	const unsigned int pglen = PAGE_SIZE - pgbase;
	void *buffer = kmap(page);
  	int error;

	/* Make sure that it is NUL terminated */
  	error = ll_capfs_readlink(CAPFS_I(inode), buffer, pgbase, pglen);
	if (error < 0)
   	goto error;
  	SetPageUptodate(page);
  	kunmap(page);
  	unlock_page(page);
  	return 0;

error:
  	SetPageError(page);
  	kunmap(page);
  	unlock_page(page);
  	return -EIO;
}

static int capfs_followlink(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	struct page *page = NULL;
	struct capfs_symlink *p;
	void *err;
  	int res;

	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.readlink++;
	res = capfs_inode_getattr(dentry);
	if (res < 0) {
		err = ERR_PTR(res);
		goto read_failed;
	}
	page = read_cache_page(&inode->i_data, 0,
			(filler_t *) capfs_symlink_filler, inode);
	if (IS_ERR(page)) {
		err = page;
		goto read_failed;
	}
	if (!PageUptodate(page)) {
		err = ERR_PTR(-EIO);
		goto getlink_read_error;
	}
	p = kmap(page);
	p->page = page;
	nd_set_link(nd, p->body);
  	return 0;
getlink_read_error:
	page_cache_release(page);
read_failed:
	nd_set_link(nd, err);
	return 0;
}

static void capfs_put_link(struct dentry *dentry, struct nameidata *nd)
{
	char *s = nd_get_link(nd);
	if (!IS_ERR(s)) {
		struct capfs_symlink *p;
		struct page *page;

		p = container_of(s, struct capfs_symlink, body[0]);
		page = p->page;
		kunmap(page);
		page_cache_release(page);
	}
}

struct inode_operations capfs_symlink_inode_operations = {
	.readlink = generic_readlink, /* read link */
	.follow_link = capfs_followlink, /* follow link */
	.put_link = capfs_put_link,
	.getattr = capfs_getattr,
	.setattr = capfs_setattr,
};


