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

#include <linux/locks.h>
#include <asm/uaccess.h>
#include <linux/seq_file.h>

#include "capfs_linux.h"
#include "capfs_mount.h"
#include "ll_capfs.h"

/* VFS super_block ops */
static struct inode *capfs_super_iget(struct super_block *sb, char *name);
static struct super_block *capfs_read_super(struct super_block *, void *, int);
static void capfs_read_inode(struct inode *);
static void capfs_clear_inode(struct inode *);
static void capfs_put_super(struct super_block *);
static int capfs_statfs(struct super_block *sb, struct statfs *buf);
static int capfs_show_options(struct seq_file *m, struct vfsmount *mnt);

struct super_operations capfs_super_operations =
{
	read_inode:    capfs_read_inode,
	/* moved notify_change to inode_operations setattr in 2.4 */
	clear_inode:   capfs_clear_inode,
	put_super:     capfs_put_super,
	statfs:        capfs_statfs,
	show_options:  capfs_show_options,
};

/* iattr_to_capfs_meta()
 *
 * Doesn't work on size field right now.
 */
int iattr_to_capfs_meta(struct iattr *attr, struct capfs_meta *meta)
{
	PENTRY;
	PDEBUG(D_INODE, "iattr_to_capfs_meta: valid = %x, flags = %x\n",
	attr->ia_valid, attr->ia_attr_flags);

	memset(meta, 0, sizeof(*meta));
	meta->mode = attr->ia_mode;
	meta->uid = attr->ia_uid;
	meta->gid = attr->ia_gid;
	meta->atime = attr->ia_atime;
	meta->mtime = attr->ia_mtime;
	meta->ctime = attr->ia_ctime;
	meta->valid = 0;

	/* map valid fields in attr to ones in meta */
	if (attr->ia_valid & ATTR_MODE) meta->valid |= V_MODE;
	if (attr->ia_valid & ATTR_UID) meta->valid |= V_UID;
	if (attr->ia_valid & ATTR_GID) meta->valid |= V_GID;

	/* TODO: MAKE SURE THIS WORKS OUT OK! 
	 * Might need to pull the non-valid values in the iattr out from the
	 * inode?  That would be a bit troublesome...
	 */
	if (attr->ia_valid & (ATTR_ATIME | ATTR_MTIME))
		meta->valid |= V_TIMES;
	if (attr->ia_valid & ATTR_CTIME)
		meta->valid |= V_CTIME;

	PEXIT;
	return 0;
}

/* capfs_notify_change()
 *
 * Calls ll_capfs_setmeta() to set CAPFS attributes, then calls
 * inode_setattr() to set the attributes in the local cache.
 *
 * NOTE: we don't ever request a size change here; we leave that to
 * truncate().
 */
int capfs_notify_change(struct dentry *entry, struct iattr *iattr)
{
	int error = 0;
	struct capfs_inode *pinode;
	struct capfs_meta meta;
	unsigned int saved_valid;
	PENTRY;

	saved_valid = iattr->ia_valid;
	iattr->ia_valid &= ~ATTR_SIZE;

	if (entry->d_inode == NULL) {
		PDEBUG(D_INODE, "capfs_notify_change called for %s (null inode)\n",
		entry->d_name.name);
		PEXIT;
		return -EINVAL;
	}
	pinode = capfs_inop(entry->d_inode);

	PDEBUG(D_INODE, "capfs_notify_change called for %s\n", pinode->name);

	/* fill in our metadata structure, call ll_capfs_setmeta() */
	iattr_to_capfs_meta(iattr, &meta);
	meta.handle = pinode->handle;
	if ((error = ll_capfs_setmeta(pinode, &meta, current->fsuid,
		current->fsgid)) != 0) {
		PERROR("capfs_notify_change failed\n");
		PEXIT;
		return error;
	}

	iattr->ia_valid = saved_valid;
	inode_setattr(entry->d_inode, iattr);

	PEXIT;
	return error;
}

static struct super_block * capfs_read_super(struct super_block *sb, 
					    void *data, int silent)
{
	struct capfs_super *capfs_sbp = NULL;
	struct inode *root;
	struct capfs_mount *mnt = (struct capfs_mount *)data;
	PENTRY;

	PDEBUG(D_SUPER, "capfs_read_super called.\n");

	MOD_INC_USE_COUNT;

	if (data == NULL) {
		if (!silent) PERROR("capfs_read_super: no data parameter!\n");
		goto capfs_read_super_abort;
	}

	if ((capfs_sbp = (struct capfs_super *) kmalloc(sizeof(struct
		capfs_super), GFP_KERNEL)) == NULL)
	{
		if (!silent) PERROR("capfs_read_super: kmalloc failed!\n");
		goto capfs_read_super_abort;
	}

	/* data has already been copied in... */
	capfs_sbp->flags = mnt->flags;
	capfs_sbp->port = mnt->port;
	capfs_sbp->tcp  = mnt->tcp;
	/* the vfs code does not know if "cons" is really valid or not. only client knows */
	capfs_sbp->i_cons = -1; /* will be filled out by the client */
	strncpy(capfs_sbp->cons, mnt->cons, CAPFS_CONSLEN);
	strncpy(capfs_sbp->mgr, mnt->mgr, CAPFS_MGRLEN);
	strncpy(capfs_sbp->dir, mnt->dir, CAPFS_DIRLEN);

	/* modify the superblock */

	sb->s_maxbytes = ~0ULL;	/* Whoohoo, unlimited file size! */
	/* Note: CAPFS must be configured with LFS support in order to 
	 * access large files.  We cannot check this here.
	 */
	sb->s_magic = CAPFS_SUPER_MAGIC;
	sb->s_blocksize = 4096;
	sb->s_blocksize_bits = 12;
	sb->s_op = &capfs_super_operations;
	sb->u.generic_sbp = capfs_sbp;

	/* set up root inode info */
	root = capfs_super_iget(sb, capfs_sbp->dir);
	if (root == NULL) goto capfs_read_super_abort;

	sb->s_root = d_alloc_root(root);

	PDEBUG(D_SUPER, "super addr = %lx, mode = %o, name = %s\n", (long) sb,
	root->i_mode, capfs_inop(root)->name);
	PEXIT;
	return sb;

capfs_read_super_abort:
	PERROR("capfs_read_super aborting!\n");
	sb->s_dev = 0;
	if (capfs_sbp != NULL) kfree(capfs_sbp);
	MOD_DEC_USE_COUNT;
	PEXIT;
	return NULL;
}

/* capfs_put_super()
 *
 * Frees memory we allocated to store superblock information.
 *
 * Everything else seems to be taken care of in super.c:kill_super().
 */
static void capfs_put_super(struct super_block *sb)
{
	PENTRY;
	PDEBUG(D_SUPER, "capfs_put_super called.\n");

	kfree(sb->u.generic_sbp);

	MOD_DEC_USE_COUNT;
	PEXIT;
	return;
}

/* capfs_read_inode()
 *
 * This function is called by iget().  However, we don't have the
 * information we need (ie. file name) from the parameters here, so
 * instead all the work that this function would normally do is done in
 * dir.c:capfs_lookup().  The CAPFS lookup function (in v2 anyway) returns
 * metadata, so this also happens to save us a call to the manager.
 */
static void capfs_read_inode(struct inode *inode)
{
	PENTRY;
	PDEBUG(D_INODE, "capfs_read_inode() called (does nothing).\n");
	PEXIT;
	return;
}

/* capfs_clear_inode()
 *
 * Free up privately allocated memory associated with the inode.
 */
static void capfs_clear_inode(struct inode *inode)
{
   struct capfs_inode *pinode;

	PENTRY;
	pinode = capfs_inop(inode);
   kfree(pinode);
   inode->u.generic_ip = NULL;
	PEXIT;
	return;
}

/* FIXME: This will be overkill to get the correct POSIX semantics for file-system size */
static int capfs_statfs(struct super_block *sb, struct statfs *buf)
{
	int error = 0;
	struct capfs_statfs pbuf;

	PDEBUG(D_SUPER, "capfs_statfs called.\n");


	if ((error = ll_capfs_statfs(capfs_sbp(sb), &pbuf)) != 0) {
		PERROR("capfs_statfs failed\n");
		PEXIT;
		return error;
	}

#if 0
	sbuf.f_fsid = (fsid_t) 0;
#endif

	/* in 2.4 we write straight into the buffer handed to us;
	 * this gets to the user at a higher level
	 */
	buf->f_type = CAPFS_SUPER_MAGIC;
	buf->f_bsize = pbuf.bsize;
	buf->f_blocks = pbuf.blocks;
	buf->f_bfree = pbuf.bfree;
	buf->f_bavail = pbuf.bavail;
	buf->f_files = pbuf.files;
	buf->f_ffree = pbuf.ffree;
	buf->f_namelen = pbuf.namelen;

	PEXIT;
	return error;
}

/* init_capfs: used by filesystems.c to register capfs */

struct file_system_type capfs_fs_type = {
   "capfs", 0, capfs_read_super, NULL
};

int init_capfs_fs(void)
{
	int ret = -1;
	PENTRY;
	ret = register_filesystem(&capfs_fs_type);
	PEXIT;
	return(ret);
}


/* capfs_super_iget()
 *
 * Special routine for getting the inode for the superblock of a CAPFS
 * file system.
 *
 * Returns NULL on failure, pointer to inode structure on success.
 */
static struct inode *capfs_super_iget(struct super_block *sb, char *name)
{
	int mgrlen, dirlen, len;
	struct capfs_meta meta;
	struct capfs_inode *pinode;
	struct inode *inode;
	char *namebuf;

	PENTRY;

	/* big enough for <host>:port/<name> */
	mgrlen = strlen(capfs_sbp(sb)->mgr);
	dirlen = strlen(name);
	namebuf = (char *) kmalloc(mgrlen + dirlen + 8, GFP_KERNEL);
	if (namebuf == NULL) {
		PERROR("capfs_super_iget crapped out at kmalloc\n");
		return NULL;
	}
	
	/* put the full name together */
	sprintf(namebuf, "%s:%d%s", capfs_sbp(sb)->mgr,
	capfs_sbp(sb)->port, name);

	len = strlen(namebuf);

	PDEBUG(D_SUPER, "capfs_super_iget: %s\n", namebuf);

	/* last parameter == 1 is set only for mount time lookups */
	if (ll_capfs_lookup(capfs_sbp(sb), namebuf, len, &meta, 1) < 0) {
		PERROR("capfs_super_iget crapped out at ll_capfs_lookup\n");
		kfree(namebuf);
		PEXIT;
		return NULL;
	}

	if ((inode = iget(sb, meta.handle)) == NULL) {
		PERROR("capfs_super_iget crapped out at iget\n");
		kfree(namebuf);
		PEXIT;
		return NULL;
	}

	/* allocate and fill in the inode structure... */
	if ((pinode = (struct capfs_inode *) kmalloc(sizeof(struct capfs_inode)
	+ len + 1, GFP_KERNEL)) == NULL)
	{
		iput(inode);
		kfree(namebuf);
		PEXIT;
		return NULL;
	}
	
	init_capfs_rwsem(&pinode->lock);	
	pinode->handle = meta.handle;
	capfs_meta_to_inode(&meta, inode);
	pinode->name = (int8_t *)pinode + sizeof(struct capfs_inode);
	strcpy(pinode->name, namebuf);
	pinode->super = capfs_sbp(sb);
	inode->u.generic_ip = pinode;

	PDEBUG(D_SUPER, "capfs_super_iget: final name = %s\n",
	capfs_inop(inode)->name);

	if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &capfs_dir_inode_operations;
		inode->i_fop = &capfs_dir_operations;
	}
   else if (S_ISREG(inode->i_mode)) {
		inode->i_op = &capfs_file_inode_operations;
		inode->i_fop = &capfs_file_operations;
		inode->i_data.a_ops = &capfs_file_aops;
	}
	else if (S_ISLNK(inode->i_mode)) { /* I wonder if the root inode is going to be a symlink */
		inode->i_op = &capfs_symlink_inode_operations;
		inode->i_data.a_ops = &capfs_file_aops;
	}
   else {
		inode->i_op = NULL;
		inode->i_fop = NULL;
	}

#ifdef HAVE_MGR_LOOKUP
	/* if we have the mgr lookup, then we didn't get all the metadata
    * we wanted from the lookup operation (like we used to).  In that case we 
	 * need to call ll_capfs_getmeta() to get the remainder.
	 */
	if (ll_capfs_getmeta(capfs_inop(inode), &meta, NULL) < 0) {
		PERROR("capfs_super_iget crapped out at ll_capfs_getmeta\n");
		iput(inode);
		inode = NULL;
	}
	if(inode) {
		capfs_meta_to_inode(&meta, inode);
	}
#endif

	kfree(namebuf);
	PEXIT;
	return inode;
}


/* show the CAPFS mount options currently in-use */
static int  capfs_show_options(struct seq_file *m, struct vfsmount *mnt)
{
   static struct capfs_proc_info {
		int flag;
		char *str;
		char *nostr;
   } ppi_info[] = {
		{CAPFS_MOUNT_INTR,",intr ",""},
		{CAPFS_MOUNT_HCACHE,",hcache ",""},
		{CAPFS_MOUNT_DCACHE,",dcache ",""},
		{0, NULL, NULL}
   };
   struct capfs_proc_info *ppi_infop=NULL;
   struct capfs_super *sb = capfs_sbp(mnt->mnt_sb);
   if(!sb) {
		PERROR("Could not get CAPFS super-block!\n");
		return 0;
   }
   for (ppi_infop = ppi_info; ppi_infop->flag; ppi_infop++) {
		if (sb->flags & ppi_infop->flag) {
			seq_puts(m, ppi_infop->str);
		}
		else {
			seq_puts(m, ppi_infop->nostr);
		}
   }
   seq_puts(m," addr=");
   seq_escape(m, sb->mgr, " \t\n\\");
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
