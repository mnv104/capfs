#ifndef _CAPFS_LINUX_H
#define _CAPFS_LINUX_H

/*
 * copyright (c) 1999 Rob Ross and Phil Carns, all rights reserved.
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

/* capfs_linux.h
 *
 * This file includes defines of common parameters, debugging print
 * macros, and some configuration values.
 *
 */
#include <linux/kernel.h>
#include <linux/posix_types.h>
#include <linux/types.h>
#include <linux/param.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/wait.h>		
#include <linux/fs.h>


/* 2.4 */
#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/init.h>

#include "ll_capfs.h"

extern struct file_system_type capfs_fs_type;

/* operations */
extern struct inode_operations capfs_dir_inode_operations;
extern struct inode_operations capfs_file_inode_operations;
extern struct inode_operations capfs_symlink_inode_operations;

extern struct file_operations capfs_dir_operations;
extern struct file_operations capfs_file_operations;

extern struct address_space_operations capfs_file_aops;

/* operations shared over more than one file */
int capfs_fsync(struct file *file, struct dentry *dentry, int datasync);

int capfs_release(struct inode *i, struct file *f);
int capfs_permission(struct inode *inode, int mask);
int capfs_revalidate_inode(struct dentry *);
int capfs_meta_to_inode(struct capfs_meta *mbuf, struct inode *ibuf);
int capfs_rename(struct inode *old_inode, struct dentry *old_dentry, 
	struct inode *new_inode, struct dentry *new_dentry);
int capfs_open(struct inode *i, struct file *f);

int capfs_file_create(struct inode *dir, struct dentry *entry, int mode);

/* from inode.c */
int iattr_to_capfs_meta(struct iattr *attr, struct capfs_meta *meta);
int capfs_notify_change(struct dentry *entry, struct iattr *iattr);

/* global variables */
extern int capfs_debug;

#ifndef CAPFS_SUPER_MAGIC
enum {
	CAPFS_SUPER_MAGIC = 0x0872 /* must match one in capfs_config.h */
};
#endif

/* simple functions to get to CAPFS-specific info in superblocks and
 * inodes
 */

static inline struct capfs_super *capfs_sbp(struct super_block *sb)
{
	return sb->u.generic_sbp;
}

static inline struct capfs_inode *capfs_inop(struct inode *ino)
{
	return ino->u.generic_ip;
}

static inline int tcp_sbp(struct super_block *sb)
{
	if (sb && sb->u.generic_sbp) {
		return ((struct capfs_super *)(sb->u.generic_sbp))->tcp;
	}
	PERROR("tcp_sbp found NULL pointer in sb or pointer to capfs_sb\n");
	return -1;
}

static inline int tcp_inop(struct inode *inode)
{
	struct capfs_inode *pinode = NULL;

	if (inode && ((pinode = inode->u.generic_ip) != NULL)
			&& pinode->super) {
		return pinode->super->tcp;
	}
	PERROR("tcp_inop found NULL inode or capfs_inode pointer\n");
	return -1;
}

static inline int cons_sbp(struct super_block *sb)
{
	if (sb && sb->u.generic_sbp) {
		return ((struct capfs_super *)(sb->u.generic_sbp))->i_cons;
	}
	PERROR("cons_sbp found NULL pointer in sb or pointer to cons_sb\n");
	return -1;
}

static inline int cons_inop(struct inode *inode)
{
	struct capfs_inode *pinode = NULL;

	if (inode && ((pinode = inode->u.generic_ip) != NULL)
			&& pinode->super) {
		return pinode->super->i_cons;
	}
	PERROR("cons_inop found NULL inode or capfs_inode pointer\n");
	return -1;
}

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
