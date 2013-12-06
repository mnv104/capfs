/*
 * copyright (c) 1999 Rob Ross and Phil Carns, all rights reserved.
 *
 * Written by Rob Ross and Phil Carns, funded by Scyld Computing.
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

#define __NO_VERSION__

#include "capfs_kernel_config.h"
#include "capfs_linux.h"
#include "ll_capfs.h"
#include "capfs_proc.h"

#include <linux/locks.h>
#include <linux/smp_lock.h>

/* dir inode-ops */
static int capfs_readlink(struct dentry*, char*, int);
static int capfs_followlink(struct dentry*, struct nameidata *);

struct inode_operations capfs_symlink_inode_operations = {
	readlink: capfs_readlink, /* read link */
	follow_link: capfs_followlink, /* follow link */
	revalidate: capfs_revalidate_inode, /* revalidate inode */
};

static int capfs_symlink_filler(struct inode *inode, struct page *page)
{
	void *buffer = kmap(page);
  	int error;
	/* Make sure that it is NUL terminated */
  	error = ll_capfs_readlink(capfs_inop(inode), buffer, PAGE_CACHE_SIZE);
	if (error < 0)
   	goto error;
  	SetPageUptodate(page);
  	kunmap(page);
  	UnlockPage(page);
  	return 0;

error:
  	SetPageError(page);
  	kunmap(page);
  	UnlockPage(page);
  	return -EIO;
}

static char *capfs_getlink(struct inode *inode, struct page **ppage)
{
	struct page *page;
  	u32 *p;

  	/* Caller revalidated the directory inode already. */
  	page = read_cache_page(&inode->i_data, 0, (filler_t *)capfs_symlink_filler, inode);
	if (IS_ERR(page))
  	 	goto read_failed;
 	if (!Page_Uptodate(page))
    	goto getlink_read_error;
  	*ppage = page;
  	p = kmap(page);
  	return (char*)p;
  
getlink_read_error:
	page_cache_release(page);
  	return ERR_PTR(-EIO);
read_failed:
  	return (char*)page;
}

static int capfs_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	struct inode *inode = dentry->d_inode;
 	struct page *page = NULL;
  	int res;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.readlink++;       
	res = vfs_readlink(dentry, buffer, buflen, capfs_getlink(inode,&page));
  	if (page) {
    	kunmap(page);
    	page_cache_release(page);
  	}
  	return res;
}

static int capfs_followlink(struct dentry *dentry, struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	struct page *page = NULL;
  	int res;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.readlink++;       
	res = vfs_follow_link(nd, capfs_getlink(inode, &page));
  	if (page) {
   	kunmap(page);
	  	page_cache_release(page);
  	}
  	return res;
}
