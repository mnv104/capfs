#ifndef _CAPFS_LINUX_H
#define _CAPFS_LINUX_H

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

/* capfs_linux.h
 *
 * This file includes defines of common parameters, debugging print
 * macros, and some configuration values.
 *
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/param.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/wait.h>		
#include <linux/fs.h>
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
extern struct backing_dev_info capfs_backing_dev_info;

/* operations shared over more than one file */
int capfs_fsync(struct file *file, struct dentry *dentry, int datasync);

int capfs_release(struct inode *i, struct file *f);
int capfs_permission(struct inode *inode, int mask);
int capfs_inode_getattr(struct dentry *);
int capfs_meta_to_inode(struct capfs_meta *mbuf, struct inode *ibuf);
int capfs_rename(struct inode *old_inode, struct dentry *old_dentry, 
	struct inode *new_inode, struct dentry *new_dentry);
int capfs_open(struct inode *i, struct file *f);

int capfs_file_create(struct inode *dir, struct dentry *entry, int mode, struct nameidata *nd);

/* from inode.c */
int iattr_to_capfs_meta(struct iattr *attr, struct capfs_meta *meta);

/* from dir.c */
int capfs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *kst);
int capfs_setattr(struct dentry *dentry, struct iattr *attr);

/* from capfs-cache.c */
void capfs_inode_cache_initialize(void);
void capfs_inode_cache_finalize(void);
void capfs_inode_initialize(struct capfs_inode *inode);
void capfs_inode_finalize(struct capfs_inode *inode);

/* from inode.c */
struct inode* capfs_get_and_fill_inode(struct super_block *sb, 
		unsigned long inode_number,
		int mode, dev_t dev);
void capfs_fill_inode(struct inode *inode,
		int mode, dev_t dev);

/* global variables */
extern int capfs_debug;
extern kmem_cache_t *capfs_inode_cache;
extern struct dentry_operations capfs_dentry_operations;

enum {
	CAPFS_SUPER_MAGIC = 0x0872 /* must match one in capfs_config.h */
};

/* simple functions to get to CAPFS-specific info in superblocks and
 * inodes
 */

static inline struct capfs_sb_info *CAPFS_SB(struct super_block *sb)
{
	return (struct capfs_sb_info *)sb->s_fs_info;
}

static inline struct capfs_inode *CAPFS_I(struct inode *inode)
{
	return container_of(inode, struct capfs_inode, vfs_inode);
}

static inline int tcp_sbp(struct super_block *sb)
{
	struct capfs_sb_info *sbinfo = NULL;

	if (sb && (sbinfo = CAPFS_SB(sb))) {
		return sbinfo->tcp;
	}
	PERROR("tcp_sbp found NULL pointer in sb or pointer to capfs_sb\n");
	return -1;
}

static inline int tcp_inop(struct inode *inode)
{
	struct capfs_inode *pinode = NULL;

	if (inode && ((pinode = CAPFS_I(inode)) != NULL) && pinode->super) {
		return pinode->super->tcp;
	}
	PERROR("tcp_inop found NULL inode or capfs_inode pointer\n");
	return -1;
}

static inline int cons_sbp(struct super_block *sb)
{
	struct capfs_sb_info *sbinfo = NULL;

	if (sb && (sbinfo = CAPFS_SB(sb))) {
		return sbinfo->i_cons;
	}
	PERROR("cons_sbp found NULL pointer in sb or pointer to cons_sb\n");
	return -1;
}

static inline int cons_inop(struct inode *inode)
{
	struct capfs_inode *pinode = NULL;

	if (inode && ((pinode = CAPFS_I(inode)) != NULL) && pinode->super) {
		return pinode->super->i_cons;
	}
	PERROR("cons_inop found NULL inode or capfs_inode pointer\n");
	return -1;
}

#define capfs_update_inode_time(inode)\
	do { inode->i_mtime = inode->i_ctime = CURRENT_TIME;} while(0)

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
