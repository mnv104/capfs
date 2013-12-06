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
#include "capfs_kernel_config.h"
#include "capfs_linux.h"
#include "capfs_mount.h"
#include "ll_capfs.h"

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
	meta->atime = attr->ia_atime.tv_sec;
	meta->mtime = attr->ia_mtime.tv_sec;
	meta->ctime = attr->ia_ctime.tv_sec;
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

/* capfs_setattr()
 *
 * Calls ll_capfs_setmeta() to set CAPFS attributes, then calls
 * inode_setattr() to set the attributes in the local cache.
 *
 * NOTE: we don't ever request a size change here; we leave that to
 * truncate().
 */
int capfs_setattr(struct dentry *entry, struct iattr *iattr)
{
	int error = 0;
	struct inode *inode = entry->d_inode;
	struct capfs_inode *pinode;
	struct capfs_meta meta;
	unsigned int saved_valid;
	PENTRY;

	error = inode_change_ok(inode, iattr);
	if (error == 0) 
	{
		saved_valid = iattr->ia_valid;
		iattr->ia_valid &= ~ATTR_SIZE;

		if (entry->d_inode == NULL) {
			PDEBUG(D_INODE, "capfs_setattr called for %s (null inode)\n",
			entry->d_name.name);
			PEXIT;
			return -EINVAL;
		}
		pinode = CAPFS_I(entry->d_inode);

		PDEBUG(D_INODE, "capfs_setattr: %s [atime: %ld] [mtime: %ld] [ctime: %ld]\n", pinode->name, meta.atime, meta.mtime, meta.ctime);

		/* fill in our metadata structure, call ll_capfs_setmeta() */
		iattr_to_capfs_meta(iattr, &meta);
		meta.handle = pinode->handle;
		if ((error = ll_capfs_setmeta(pinode, &meta, current->fsuid,
			current->fsgid)) != 0) {
			PERROR("capfs_setattr failed\n");
			PEXIT;
			return error;
		}

		iattr->ia_valid = saved_valid;
		error = inode_setattr(entry->d_inode, iattr);
	}

	PEXIT;
	return error;
}

static struct inode *capfs_alloc_inode(struct super_block *sb)
{
	struct inode *new_inode = NULL;
	struct capfs_inode *new_capfs_inode = NULL;

	/*
	 * Use the slab allocator to allocate the capfs_inode
	 * structures. Use the constructor to initialize
	 * the structure.
	 */
	new_capfs_inode = kmem_cache_alloc(capfs_inode_cache, SLAB_KERNEL);
	if (new_capfs_inode) 
	{
		new_inode = &new_capfs_inode->vfs_inode;
	}
	return new_inode;
}

static void capfs_destroy_inode(struct inode *inode)
{
	struct capfs_inode *old_capfs_inode = CAPFS_I(inode);

	PDEBUG(D_INODE, "capfs_destroy_inode: destroying inode %d\n", (int)inode->i_ino);
	capfs_inode_finalize(old_capfs_inode);
	kmem_cache_free(capfs_inode_cache, old_capfs_inode);
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

/*
 * write back the access times etc..
 */
static int capfs_write_inode(struct inode* inode, int wait)
{
	struct capfs_inode *capfs_inode = CAPFS_I(inode);
	struct capfs_meta  meta;
	int error;

	PENTRY;
	if (capfs_inode == NULL)
	{
		return -EIO;
	}
	memset(&meta, 0, sizeof(struct capfs_meta));
	meta.handle = capfs_inode->handle;
	meta.mode = inode->i_mode;
	meta.atime = inode->i_atime.tv_sec;
	meta.mtime = inode->i_mtime.tv_sec;
	meta.ctime = inode->i_ctime.tv_sec;
	meta.valid |= (V_TIMES | V_CTIME);
	error = ll_capfs_setmeta(capfs_inode, &meta, current->fsuid,
					current->fsgid); 
	PDEBUG(D_INODE, "capfs_write_inode: %s [atime: %lu] [mtime: %lu] [ctime: %lu] -> %d\n",
			capfs_inode->name, meta.atime, meta.mtime, meta.ctime, error);
	PEXIT;
	return error;
}

/* called when the VFS removes this inode from the inode cache */
static void capfs_put_inode(struct inode *inode)
{
	PDEBUG(D_INODE, "capfs_put_inode: (%d | ct = %d | nlink = %d)\n",
			(int) inode->i_ino, (int) atomic_read(&inode->i_count),
			(int) inode->i_nlink);
	if (atomic_read(&inode->i_count) == 1) 
	{
		/* Kill aliased dentries associated with this inode */
		d_prune_aliases(inode);
	}
	return;
}

static int capfs_statfs(struct super_block *sb, struct kstatfs *buf)
{
	int error = 0;
	struct capfs_statfs pbuf;

	PDEBUG(D_SUPER, "capfs_statfs called.\n");


	if ((error = ll_capfs_statfs(CAPFS_SB(sb), &pbuf)) != 0) {
		PERROR("capfs_statfs failed\n");
		PEXIT;
		return error;
	}

#if 0
	sbuf.f_fsid = (fsid_t) 0;
#endif
	buf->f_type = sb->s_magic;
	buf->f_bsize = pbuf.bsize;
	buf->f_blocks = pbuf.blocks;
	buf->f_bfree = pbuf.bfree;
	buf->f_bavail = pbuf.bavail;
	buf->f_files = pbuf.files;
	buf->f_ffree = pbuf.ffree;
	buf->f_namelen = pbuf.namelen;
	do 
	{
		struct statfs tmp_statfs;
		buf->f_frsize = 1024;
		if ((sizeof(struct statfs) != sizeof(struct kstatfs)) &&
			 (sizeof(tmp_statfs.f_blocks) == 4))
		{
			/*
				in this case, we need to truncate the values here to
				be no bigger than the max 4 byte long value because
				the kernel will return an overflow if it's larger
				otherwise.  
			 */
			 buf->f_blocks &= 0x00000000FFFFFFFFULL;
			 buf->f_bfree &= 0x00000000FFFFFFFFULL;
			 buf->f_bavail &= 0x00000000FFFFFFFFULL;
			 buf->f_files &= 0x00000000FFFFFFFFULL;
			 buf->f_ffree &= 0x00000000FFFFFFFFULL;
			 
			 PDEBUG(D_INODE, "capfs_statfs (1) got %lu files total | %lu "
						 "files_avail\n", (unsigned long)buf->f_files,
						 (unsigned long)buf->f_ffree);
		}
		else
		{
			 PDEBUG(D_INODE, "capfs_statfs (2) got %lu files total | %lu "
						 "files_avail\n", (unsigned long)buf->f_files,
						 (unsigned long)buf->f_ffree);
		}
	} while (0);

	PEXIT;
	return error;
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
   struct capfs_sb_info *sb = NULL;

	sb = CAPFS_SB(mnt->mnt_sb);
   if(!sb) 
	{
		PERROR("Could not get CAPFS super-block!\n");
		return 0;
   }
   for (ppi_infop = ppi_info; ppi_infop->flag; ppi_infop++) 
	{
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

struct super_operations capfs_super_operations =
{
	.drop_inode =     generic_delete_inode,
	.alloc_inode =    capfs_alloc_inode,
	.destroy_inode =  capfs_destroy_inode,
	.read_inode =     capfs_read_inode,
	.write_inode =    capfs_write_inode,
	.put_inode  =     capfs_put_inode,
	.statfs =         capfs_statfs,
	.show_options =   capfs_show_options,
};

struct backing_dev_info capfs_backing_dev_info =
{
    .ra_pages = 1024,
    .memory_backed = 1 /* does not contribute to dirty memory */
};


void capfs_fill_inode(struct inode *inode,
		int mode, dev_t dev)
{
	if (inode)
	{
		inode->i_mapping->host = inode;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	  	inode->i_rdev = dev;
	 	inode->i_bdev = NULL;
	  	inode->i_cdev = NULL;
	  	inode->i_mapping->a_ops = &capfs_file_aops;
	  	inode->i_mapping->backing_dev_info = &capfs_backing_dev_info;
		PDEBUG(D_INODE, "capfs_fill_inode: inode %p allocated\n  "
				"(capfs_inode is %p | sb is %p)\n", inode, CAPFS_I(inode), inode->i_sb);
      if ((mode & S_IFMT) == S_IFREG)
		{
			inode->i_op = &capfs_file_inode_operations;
			inode->i_fop = &capfs_file_operations;
			inode->i_data.a_ops = &capfs_file_aops;
	  	}
      else if ((mode & S_IFMT) == S_IFLNK)
      {
			inode->i_op = &capfs_symlink_inode_operations;
			inode->i_fop = NULL;
			inode->i_data.a_ops = &capfs_file_aops;
		}
		else if ((mode & S_IFMT) == S_IFDIR) 
		{
			inode->i_op = &capfs_dir_inode_operations;
			inode->i_fop = &capfs_dir_operations;
			/* dir inodes start with i_nlink == 2 (for "." entry) */
			inode->i_nlink++;
		}
		else
		{
			inode->i_op = NULL;
			inode->i_fop = NULL;
		}
	}
	return;
}

struct inode* capfs_get_and_fill_inode(struct super_block *sb, 
		unsigned long inode_number,
		int mode, dev_t dev)
{
	struct inode *inode = NULL;
	struct capfs_inode *capfs_inode = NULL;

	PDEBUG(D_INODE, "capfs_get_and_fill_inode called with sb = %p,"
			"inode_number = %ld, mode = %d, MAJOR(dev) = %u, MINOR(dev) = %u\n",
			sb, inode_number, mode, MAJOR(dev), MINOR(dev));
	inode = iget(sb, inode_number);
	if (inode)
	{
		capfs_inode = CAPFS_I(inode);
		if (capfs_inode == NULL) 
		{
			iput(inode);
			PERROR("struct inode: PRIVATE DATA not allocated\n");
			return NULL;
		}
		capfs_fill_inode(inode, mode, dev);
	}
	return inode;
}

/*
 * Lookup the root inode of the CAPFS file system
 * and return an initialized inode with the appropriate
 * information
 */
static struct inode *capfs_super_iget(struct super_block *sb,
		char *name)
{
	int mgrlen, dirlen, len;
	struct capfs_meta meta;
	struct capfs_inode *pinode;
	struct inode *inode;
	char *namebuf;

	PENTRY;

	mgrlen = strlen(CAPFS_SB(sb)->mgr);
	dirlen = strlen(name);
	namebuf = (char *) kmalloc(mgrlen + dirlen + 8, GFP_KERNEL);
	if (namebuf == NULL) {
		PERROR("crapped out at kmalloc\n");
		return NULL;
	}
	/* piece the whole file name together */

	/* <host>:<port>/<name> */
	sprintf(namebuf, "%s:%d%s", CAPFS_SB(sb)->mgr, CAPFS_SB(sb)->port, name);
	len = strlen(namebuf);
	PDEBUG(D_SUPER, "%s\n", namebuf);

	/* last parameter == 1 is set only for mount time lookups */
	if (ll_capfs_lookup(CAPFS_SB(sb), namebuf, len, &meta, 1) < 0) {
		PERROR("crapped out at ll_capfs_lookup\n");
		kfree(namebuf);
		PEXIT;
		return NULL;
	}

	if ((inode = iget(sb, meta.handle)) == NULL) {
		PERROR("crapped out at iget\n");
		kfree(namebuf);
		PEXIT;
		return NULL;
	}
	pinode = CAPFS_I(inode);
	if (pinode == NULL) {
		PERROR("found NULL pinode pointer\n");
		kfree(namebuf);
		iput(inode);
		PEXIT;
		return NULL;
	}
	pinode->handle = meta.handle;
	capfs_meta_to_inode(&meta, inode);
	pinode->name = kmalloc(len + 1, GFP_KERNEL);
	if (pinode->name == NULL) {
		PERROR("crapped out at kmalloc!\n");
		kfree(namebuf);
		iput(inode);
		PEXIT;
		return NULL;
	}
	strcpy(pinode->name, namebuf);
	pinode->super = CAPFS_SB(sb);
	PDEBUG(D_SUPER, "final name = %s\n", CAPFS_I(inode)->name);
	/* Initialize the newly allocated inode with its function pointer etc etc */
	capfs_fill_inode(inode, (S_IFDIR | 0755), 0);
#ifdef HAVE_MGR_LOOKUP
	if (ll_capfs_getmeta(pinode, &meta, NULL) < 0) 
	{
		PERROR("crapped out at ll_capfs_getmeta\n");
		iput(inode);
		inode = NULL;
	}
	if(inode) 
	{
		capfs_meta_to_inode(&meta, inode);
	}
#endif
	kfree(namebuf);
	PEXIT;
	return inode;
}

static struct export_operations capfs_export_ops = { };

/*
 * Initialize the fields in the superblock
 * and return
 */
static int capfs_fill_sb(struct super_block *sb,
		void *data, int silent)
{
	struct capfs_sb_info *capfs_sbp = NULL;
	struct capfs_mount *mnt = (struct capfs_mount *) data;
	struct inode *root;
	struct dentry *root_dentry = NULL;

	PENTRY;

	PDEBUG(D_SUPER, "capfs_fill_sb called\n");

	if (data == NULL) {
		if (!silent) 
			PERROR("capfs_fill_sb: no data parameter\n");
		return -EINVAL;
	}
	sb->s_fs_info = NULL;
	/* allocate and initialize our own private sb_info structure */
	capfs_sbp = (struct capfs_sb_info *) 
		kmalloc(sizeof(struct capfs_sb_info), GFP_KERNEL);
	if (capfs_sbp == NULL) 
	{
		if (!silent) 
			PERROR("capfs_fill_sb: kmalloc failed\n");
		return -ENOMEM;
	}
	memset(capfs_sbp, 0, sizeof(struct capfs_sb_info));
	capfs_sbp->flags = mnt->flags;
	capfs_sbp->port  = mnt->port;
	capfs_sbp->tcp   = mnt->tcp;
	/*
	 * vfs code does not know nor care about consistency policies.
	 * Only client code cares about the validity of this parameter
	 */
	capfs_sbp->i_cons = -1; /* will be filled out by the client */
	
	PDEBUG(D_INODE, "capfs_fill_sb got <cons>: %s, <mgr>: %s, <dir>: %s <flags>: %x\n",
			mnt->cons, mnt->mgr, mnt->dir, mnt->flags);
	strncpy(capfs_sbp->cons, mnt->cons, CAPFS_CONSLEN);
	strncpy(capfs_sbp->mgr, mnt->mgr, CAPFS_MGRLEN);
	strncpy(capfs_sbp->dir, mnt->dir, CAPFS_DIRLEN);
	capfs_sbp->sb  = sb;

	/* modify the superblock to reflect a few things */
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_magic    = CAPFS_SUPER_MAGIC;
	sb->s_blocksize_bits = 12;
	sb->s_blocksize = 4096;
	sb->s_op       = &capfs_super_operations;
	sb->s_fs_info = capfs_sbp;

	/* setup root inode information here */
	root = capfs_super_iget(sb, capfs_sbp->dir);
	if (root == NULL) 
	{
		if (!silent) 
			PERROR("capfs_super_iget aborted!\n");
		/* Cleanup is done inside the kill_sb callback routine */
		return -ENOMEM;
	}
	/* allocates a dentry for this inode */
	root_dentry = d_alloc_root(root);
	if (root_dentry == NULL) 
	{
		iput(root);
		return -ENOMEM;
	}
	root_dentry->d_op = &capfs_dentry_operations;
	sb->s_export_op   = &capfs_export_ops;
	sb->s_root = root_dentry;

	PDEBUG(D_SUPER, "super addr = %lx, mode = %o, name = %s\n", (long) sb, root->i_mode, CAPFS_I(root)->name);
	PEXIT;
	return 0;
}

/* Copied some macros shamelessly from wanpipe.h */
#define  is_digit(ch) (((ch)>=(unsigned)'0'&&(ch)<=(unsigned)'9')?1:0)
#define  is_alpha(ch) ((((ch)>=(unsigned)'a'&&(ch)<=(unsigned)'z')||\
			        ((ch)>=(unsigned)'A'&&(ch)<=(unsigned)'Z'))?1:0)
/*
 * Allocate and return a superblock
 */
static struct super_block *capfs_get_sb(
		struct file_system_type *fst,
		int    flags,
		const  char *devname,
		void   *data)
{
	struct super_block *sb = ERR_PTR(-EINVAL);
	struct capfs_mount *mnt = (struct capfs_mount *) data;
	char *tmp_ptr = NULL, *ptr1 = NULL, *ptr2 = NULL, *ptr3 = NULL;
	int i;

	PENTRY;
	/* 
	 * On the Fedora Core 3 kernel for some reason, the "data"
	 * pointer does not contain the fields that the user-space
	 * mount binary seems to be passing rightly.
	 * Need to track this issue some other time.
	 * For now, I am going to have to enforce that
	 * devname is of the format
	 * <IP>:<CONS-POLICY>:<DIR NAME>
	 *
	 * On some other 2.6 kernels, we don't use the devname here since that has not been
	 * translated by the mount.capfs binary to an IPaddress * separated meta-data locator.
	 * Instead we use the data parameter.
	 */
	if (data == NULL) {
		printk(KERN_ERR "capfs_get_sb did not receive the data parameter\n");
		PEXIT;
		return sb;
	}
	ptr1 = strchr(devname, ':');
	if (ptr1 == NULL) {
		printk(KERN_ERR "capfs_get_sb did not receive a valid devname %s (<IP>:<FLAGS>_<CONS>:<DIR>)\n", devname);
		PEXIT;
		return sb;
	}
	ptr2 = strrchr(devname, ':');
	if (ptr2 == NULL || ptr2 == ptr1)
	{
		printk(KERN_ERR "capfs_get_sb did not receive a valid devname %s (<IP>:<FLAGS>_<CONS>:<DIR>)\n", devname);
		PEXIT;
		return sb;
	}
	ptr3 = strchr(devname, '_');
	if (ptr3 == NULL || ptr2 == ptr3 || ptr1 == ptr3)
	{
		printk(KERN_ERR "capfs_get_sb did not receive a valid devname %s (<IP>:<FLAGS>_<CONS>:<DIR>)\n", devname);
		PEXIT;
		return sb;
	}
	i = 0;
	/* UGLY hack begins due to FC3 kernel madness */
	for (tmp_ptr = (char *) devname; tmp_ptr < ptr1; tmp_ptr++) {
		if (!is_digit(*tmp_ptr) && *tmp_ptr != '.') {
			printk(KERN_ERR "capfs_get_sb did not get a valid dotted IP address for meta-server\n");
			PEXIT;
			return sb;
		}
		mnt->mgr[i++] = *tmp_ptr;
	}
	/* Remember that mnt already points to a zeroed out page, so no need to NUL terminate here */
	strncpy(mnt->cons, ptr3 + 1, (ptr2 - ptr3 - 1));
	strncpy(mnt->dir, ptr2 + 1, CAPFS_DIRLEN);
	sscanf(ptr1 + 1, "%d", &flags);
	mnt->flags = flags;
	mnt->tcp   = (flags & CAPFS_MOUNT_TCP) ? 1: 0;
	PDEBUG(D_INODE, "capfs_get_sb got <cons>: %s, <mgr>: %s, <dir>: %s <flags>: %x <tcp>: %d <devname>: %s\n",
			mnt->cons, mnt->mgr, mnt->dir, mnt->flags, mnt->tcp, devname);
	sb = get_sb_nodev(fst, flags, data, capfs_fill_sb);
	if (sb && !IS_ERR(sb) && CAPFS_SB(sb)) {
		PDEBUG(D_INODE, "get_sb_nodev returned a valid superblock!\n");
	}
	else {
		PDEBUG(D_INODE, "get_sb_nodev crapped out!\n");
	}
	PEXIT;
	return sb;
}

/* capfs_kill_sb()
 * Frees memory we allocated to store superblock information.
 */
static void capfs_kill_sb(struct super_block *sb)
{
	PENTRY;
	PDEBUG(D_SUPER, "capfs_kill_sb called.\n");

	if (sb && !IS_ERR(sb)) 
	{
		shrink_dcache_sb(sb);
		/* provided cleanup routine */
		kill_litter_super(sb);
		/* release root dentry */
		if (sb->s_root)
		{
			dput(sb->s_root);
		}
		kfree(CAPFS_SB(sb));
	}
	else {
		PERROR("capfs_kill_sb skipping due to invalid superblock\n");
	}
	PEXIT;
	return;
}

struct file_system_type capfs_fs_type =
{
	.owner = THIS_MODULE,
	.name = "capfs",
	.get_sb = capfs_get_sb,
	.kill_sb = capfs_kill_sb,
	.fs_flags = FS_REQUIRES_DEV
};

int init_capfs_fs(void)
{
	int ret = -1;
	PENTRY;
	ret = register_filesystem(&capfs_fs_type);
	PEXIT;
	return(ret);
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
