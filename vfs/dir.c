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

/* dentry ops */
static int capfs_dentry_revalidate(struct dentry *de, int);

/* dir inode-ops */
static struct dentry *capfs_lookup(struct inode *dir, struct dentry *target);
static int capfs_link(struct dentry*, struct inode*, struct dentry*);
static int capfs_symlink(struct inode *dir, struct dentry* dentry, const char* symname);
static int capfs_unlink(struct inode *dir_inode, struct dentry *entry);
static int capfs_mkdir(struct inode *dir_inode, struct dentry *entry, int mode);
static int capfs_rmdir(struct inode *dir_inode, struct dentry *entry);

/* dir file-ops */
static int capfs_readdir(struct file *file, void *dirent, filldir_t filldir);
static ssize_t capfs_dir_read(struct file *file, char *buf, size_t count,
	loff_t *ppos);

struct dentry_operations capfs_dentry_operations =
{
	d_revalidate:     capfs_dentry_revalidate
};

struct inode_operations capfs_dir_inode_operations =
{
	/* put this in right later... capfs_notify_change */
	/* &capfs_dir_operations, */
	create: capfs_file_create,    /* create TODO: DOES THIS NEED TO BE HERE? */
	lookup: capfs_lookup,      /* lookup */
	link: capfs_link, /* hard links */
	symlink: capfs_symlink, /* symbolic links */
	unlink: capfs_unlink, /* unlink called on parent directory to unlink a file */
	mkdir: capfs_mkdir,           /* mkdir */
	rmdir: capfs_rmdir,              /* rmdir */
	rename: capfs_rename,         /* rename */
	revalidate: capfs_revalidate_inode,   /* revalidate */
	setattr: capfs_notify_change
};

struct file_operations capfs_dir_operations = {
	read: capfs_dir_read,   /* read -- returns -EISDIR  */
	readdir: capfs_readdir  /* readdir */
};

/* inode operations for directories */

/* capfs_lookup(dir, entry)
 *
 * Notes:
 * - name to look for is in the entry (entry->d_name.name, entry->d_name.len)
 * - dir is the directory in which we are doing the lookup
 *
 * Behavior:
 * - call ll_capfs_lookup to get a handle
 * - grab an inode for the new handle
 * - fill in the inode metadata
 * - add the dentry/inode pair into the dcache
 * - (optionally) set up pointer to new dentry functions
 *
 * Returns NULL pretty much all the time.  I know, this seems really
 * screwed up, but the ext2 fs seems to be doing the same thing...
 */
static struct dentry *capfs_lookup(struct inode *dir, struct dentry *entry)
{
	int error = 0, len_dir, len_file;
	struct capfs_inode *pinode, *parent_pinode = NULL;
	struct capfs_meta meta;
	struct inode *inode = NULL;
	char *ptr;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.lookup++;
	PENTRY;
	parent_pinode = capfs_inop(dir);

	if(read_lock_inode(parent_pinode) < 0) {
			  return NULL;
	}

	PDEBUG(D_DIR, "capfs_lookup called on %ld for %s\n", dir->i_ino,
	entry->d_name.name);
	
	PDEBUG(D_DIR, "name might be %s/%s\n", capfs_inop(dir)->name,
	entry->d_name.name);

	len_dir = strlen(capfs_inop(dir)->name);
	len_file = entry->d_name.len;
	if ((pinode = (struct capfs_inode *) kmalloc(sizeof(struct capfs_inode)
	+ len_dir + len_file + 2, GFP_KERNEL)) == NULL){
		read_unlock_inode(parent_pinode);
		PEXIT;
		return NULL;
	}

	/* fill in capfs_inode name field first */
	init_capfs_rwsem(&pinode->lock);
	pinode->name = (int8_t *)pinode + sizeof(struct capfs_inode);
	ptr = pinode->name;
	strcpy(ptr, capfs_inop(dir)->name);
	ptr += len_dir;
	*ptr = '/';
	ptr++;
	strcpy(ptr, entry->d_name.name);

	/* do the lookup, grabs metadata. Not a special lookup */
	error = ll_capfs_lookup(capfs_sbp(dir->i_sb), pinode->name,
		len_dir + len_file + 1, &meta, 0);
	if (error < 0 && error != -ENOENT) {
		/* real error */
		kfree(pinode);
		read_unlock_inode(parent_pinode);
		PEXIT;
		return ERR_PTR(error);
	}

	if (error == -ENOENT) {
		/* no file found; need to insert NULL inode so that create calls
		 * can work correctly (or so says Gooch)
		 */
		entry->d_time = 0;
		entry->d_op = &capfs_dentry_operations;
		d_add(entry, NULL);
		kfree(pinode);
		read_unlock_inode(parent_pinode);
		PEXIT;
		return NULL;
	}

	if ((inode = iget(dir->i_sb, meta.handle)) == NULL) {
		/* let's drop the dentry here... */
		kfree(pinode);
		read_unlock_inode(parent_pinode);
		PEXIT;
		return ERR_PTR(-ENOMEM);
	}

	/* fill in inode structure, remainder of capfs_inode */
	pinode->handle = inode->i_ino;
	pinode->super = capfs_sbp(inode->i_sb);
	capfs_meta_to_inode(&meta, inode);
	inode->u.generic_ip = pinode;

	entry->d_time = 0;
	entry->d_op = &capfs_dentry_operations;

	PDEBUG(D_DIR, "saved name is %s\n", capfs_inop(inode)->name);

	if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &capfs_dir_inode_operations;
		inode->i_fop = &capfs_dir_operations;
	}
   else if (S_ISREG(inode->i_mode)) {
		inode->i_op = &capfs_file_inode_operations;
		inode->i_fop = &capfs_file_operations;
		inode->i_data.a_ops = &capfs_file_aops;
	}
	else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &capfs_symlink_inode_operations;
		inode->i_data.a_ops = &capfs_file_aops;
	}
   else {
		inode->i_op = NULL;
		inode->i_fop = NULL;
	}

	d_add(entry, inode);
	read_unlock_inode(parent_pinode);

	PEXIT;
	return NULL;
}


/* capfs_mkdir()
 *
 * Called on parent directory with dentry of directory to create.
 *
 * This is pretty much a carbon copy of capfs_file_create().
 */
static int capfs_mkdir(struct inode *dir, struct dentry *entry, int mode)
{
	int error = 0, len_dir, len_file;
	struct inode *inode;
	struct capfs_meta meta;
	struct capfs_inode *pinode, *parent_pinode = NULL;
	char *ptr;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.mkdir++;    
	PENTRY;
	parent_pinode = capfs_inop(dir);
	if((error = write_lock_inode(parent_pinode)) < 0) {
		 PEXIT;
		 return error;
	}
	if((error = may_create(dir, entry))) {
		 write_unlock_inode(parent_pinode);
		 PEXIT;
		 return error;
	}

	PDEBUG(D_DIR, "capfs_mkdir called on %ld for %s\n", dir->i_ino,
	entry->d_name.name);

	len_dir = strlen(capfs_inop(dir)->name);
	len_file = entry->d_name.len;
	if ((pinode = (struct capfs_inode *) kmalloc(sizeof(struct capfs_inode)
	+ len_dir + len_file + 2, GFP_KERNEL)) == NULL){
		write_unlock_inode(parent_pinode);
		PEXIT;
		return -ENOMEM;
	}

	/* build capfs_inode name field first */
	init_capfs_rwsem(&pinode->lock);
	/* build capfs_inode name field first */
	pinode->name = (int8_t *)pinode + sizeof(struct capfs_inode);
	ptr = pinode->name;
	strcpy(ptr, capfs_inop(dir)->name);
	ptr += len_dir;
	*ptr = '/';
	ptr++;
	strcpy(ptr, entry->d_name.name);

	/* do the create */
	meta.valid = V_MODE | V_UID | V_GID | V_TIMES;
	meta.uid = current->fsuid;
	meta.gid = current->fsgid;
	meta.mode = mode;
	meta.mtime = meta.atime = meta.ctime = CURRENT_TIME;

	PDEBUG(D_DIR, "capfs_mkdir calling ll_capfs_mkdir\n");
	if ((error = ll_capfs_mkdir(capfs_sbp(dir->i_sb), pinode->name,
	len_dir + len_file + 1, &meta )) < 0)
	{
		kfree(pinode);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return error;
	}

	/* do a lookup so we can fill in the inode. not a special lookup */
	PDEBUG(D_DIR, "capfs_mkdir calling ll_capfs_lookup\n");
	if ((error = ll_capfs_lookup(capfs_sbp(dir->i_sb), pinode->name,
	len_dir + len_file + 1, &meta, 0)) < 0)
	{
		kfree(pinode);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return error;
	}

	/* fill in inode structure and remainder of capfs_inode */
	if ((inode = iget(dir->i_sb, meta.handle)) == NULL) {
		kfree(pinode);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return -ENOMEM;
	}

	pinode->handle = inode->i_ino;
	pinode->super = capfs_sbp(inode->i_sb);
	capfs_meta_to_inode(&meta, inode);
	inode->u.generic_ip = pinode;

	d_instantiate(entry, inode); /* do I know what this does?  nope! */

	PDEBUG(D_DIR, "saved name is %s\n", capfs_inop(inode)->name);

	if (S_ISDIR(inode->i_mode)) {
		inode->i_op = &capfs_dir_inode_operations;
		inode->i_fop = &capfs_dir_operations;
	}
   else if (S_ISREG(inode->i_mode)) { /* can this be a non-directory? */
		inode->i_op = &capfs_file_inode_operations;
		inode->i_fop = &capfs_file_operations;
		inode->i_data.a_ops = &capfs_file_aops;
	}
	else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &capfs_symlink_inode_operations;
		inode->i_data.a_ops = &capfs_file_aops;
	}
   else {
		inode->i_op = NULL;
		inode->i_fop = NULL;
	}

	write_unlock_inode(parent_pinode);
	PEXIT;
	return error;
}

/* capfs_unlink(dir, entry)
 *
 * Called on parent directory with dentry of file to unlink.  We call
 * d_delete(entry) when done to take the dentry out of the cache.  This
 * call also frees the inode associated with the unlinked file.
 * 
 */
int capfs_unlink(struct inode *dir, struct dentry *entry)
{
	int error = 0;
	struct capfs_inode *parent_pinode = NULL;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.unlink++;   
	PENTRY;
	parent_pinode = capfs_inop(dir);
	if((error = write_lock_inode(parent_pinode)) < 0) {
		 return error;
	}

	PDEBUG(D_DIR, "capfs_unlink called on %ld for %s\n", dir->i_ino,
	entry->d_name.name);
	if (entry->d_inode == NULL) {
		/* negative dentry ?!? */
		PERROR("capfs_unlink: negative dentry?\n");
		/* TODO: should look up the file? */
		write_unlock_inode(parent_pinode);
		PEXIT;
		return -ENOENT;
	}
	/* Check if we are permitted to delete */
	if((error=may_delete(dir, entry, 0))) {
		write_unlock_inode(parent_pinode);
		PEXIT;
		return error;
	}
	if (entry->d_inode->i_nlink == 0) {
		PERROR("capfs_unlink: deleting nonexistent file?\n");
	}

	error = ll_capfs_unlink(capfs_inop(entry->d_inode));

	if (error == 0) {
		entry->d_inode->i_nlink--;	
	}
	write_unlock_inode(parent_pinode);

	PEXIT;
	return error;
}

/* capfs_link()
 * Called with the old directory entry of the file, parent directory of that file
 * and the new directory entry for the link file.
 */
static int capfs_link(struct dentry* old_dentry, struct inode* dir, struct dentry* dentry)
{
	struct capfs_inode *pinode = NULL, *parent_pinode = NULL, *old_pinode = NULL;
	struct inode *inode = NULL;
	struct capfs_meta meta;
	int error = 0, len_dir, len_link;
	char* ptr = NULL;

	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.link++;
	PENTRY;
	parent_pinode = capfs_inop(dir);
	if((error = write_lock_inode(parent_pinode)) < 0) {
		 return error;
	}
	PDEBUG(D_DIR, "capfs_link called on %ld for %s->%s\n", dir->i_ino,
	dentry->d_name.name, old_dentry->d_name.name);
	old_pinode = capfs_inop(old_dentry->d_inode);

	len_dir = strlen(capfs_inop(dir)->name);
	len_link = dentry->d_name.len;
	if ((pinode = (struct capfs_inode *) kmalloc(sizeof(struct capfs_inode)
	+ len_dir + len_link + 2, GFP_KERNEL)) == NULL){
		write_unlock_inode(parent_pinode);
		PEXIT;
		return -ENOMEM;
	}
	/* build capfs_inode name field first */
	init_capfs_rwsem(&pinode->lock);
	/* build capfs_inode name field first */
	pinode->name = (int8_t *)pinode + sizeof(struct capfs_inode);
	ptr = pinode->name;
	strcpy(ptr, capfs_inop(dir)->name);
	ptr += len_dir;
	*ptr = '/';
	ptr++;
	strcpy(ptr, dentry->d_name.name);
	meta.valid = V_UID | V_GID | V_TIMES | V_MODE;
	meta.uid = current->fsuid;
	meta.gid = current->fsgid;
	meta.mtime = meta.atime = meta.ctime = CURRENT_TIME;
	meta.mode = S_IFLNK | S_IRWXUGO;
	PDEBUG(D_DIR, "capfs_link calling ll_capfs_link\n");
	if ((error = ll_capfs_link(pinode, old_pinode, &meta)) < 0)
	{
		kfree(pinode);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return error;
	}
	/* do a lookup so we can fill in the inode. not a special lookup */
	PDEBUG(D_DIR, "capfs_link calling ll_capfs_lookup\n");
	if ((error = ll_capfs_lookup(capfs_sbp(dir->i_sb), pinode->name,
	len_dir + len_link + 1, &meta, 0)) < 0)
	{
		kfree(pinode);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return error;
	}

	/* fill in inode structure and remainder of capfs_inode */
	if ((inode = iget(dir->i_sb, meta.handle)) == NULL) {
		kfree(pinode);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return -ENOMEM;
	}

	pinode->handle = inode->i_ino;
	pinode->super = capfs_sbp(inode->i_sb);
	capfs_meta_to_inode(&meta, inode);
	inode->u.generic_ip = pinode;

	d_instantiate(dentry, inode); 

	PDEBUG(D_DIR, "saved name is %s\n", capfs_inop(inode)->name);

	if (S_ISDIR(inode->i_mode)) { /* can this be a non-link */
		inode->i_op = &capfs_dir_inode_operations;
		inode->i_fop = &capfs_dir_operations;
	}
   else if (S_ISREG(inode->i_mode)) { /* can this be a non-link? */
		inode->i_op = &capfs_file_inode_operations;
		inode->i_fop = &capfs_file_operations;
		inode->i_data.a_ops = &capfs_file_aops;
	}
	else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &capfs_symlink_inode_operations;
		inode->i_data.a_ops = &capfs_file_aops;
	}
   else {
		inode->i_op = NULL;
		inode->i_fop = NULL;
	}
	PEXIT;
	write_unlock_inode(parent_pinode);
	return error;
}

/* capfs_symlink()
 *
 * Called with the parent directory inode, directory entry for the symbolic link
 * and the name to which the link points to. Note that oldname need not even exist.
 * Pretty much a carbon copy of capfs_file_create/capfs_mkdir and a host of other
 * functions :-).
 * Need to abstract out the common functionalities into helper routines I think!
 */
static int capfs_symlink(struct inode *dir, struct dentry* entry, const char* oldname)
{
	struct capfs_inode *pinode = NULL, *parent_pinode = NULL;
	struct inode *inode = NULL;
	struct capfs_meta meta;
	int error = 0, len_dir, len_link;
	char* ptr = NULL;
	int use_tcp = 0, cons = 0;

	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.symlink++;
	PENTRY;
	parent_pinode = capfs_inop(dir);
	if((error = write_lock_inode(parent_pinode)) < 0) {
		 return error;
	}
	PDEBUG(D_DIR, "capfs_symlink called on %ld for %s->%s\n", dir->i_ino,
	entry->d_name.name, oldname);

	len_dir = strlen(capfs_inop(dir)->name);
	len_link = entry->d_name.len;
	if ((pinode = (struct capfs_inode *) kmalloc(sizeof(struct capfs_inode)
	+ len_dir + len_link + 2, GFP_KERNEL)) == NULL){
		write_unlock_inode(parent_pinode);
		PEXIT;
		return -ENOMEM;
	}
	/* build capfs_inode name field first */
	init_capfs_rwsem(&pinode->lock);
	/* build capfs_inode name field first */
	pinode->name = (int8_t *)pinode + sizeof(struct capfs_inode);
	ptr = pinode->name;
	strcpy(ptr, capfs_inop(dir)->name);
	ptr += len_dir;
	*ptr = '/';
	ptr++;
	strcpy(ptr, entry->d_name.name);
	meta.valid = V_UID | V_GID | V_TIMES | V_MODE;
	meta.uid = current->fsuid;
	meta.gid = current->fsgid;
	meta.mtime = meta.atime = meta.ctime = CURRENT_TIME;
	meta.mode = S_IFLNK | S_IRWXUGO;
	PDEBUG(D_DIR, "capfs_symlink calling ll_capfs_symlink\n");
	use_tcp = tcp_inop(dir);
	cons = cons_inop(dir);
	if ((error = ll_capfs_symlink(use_tcp, cons, pinode, oldname, strlen(oldname) + 1, &meta)) < 0)
	{
		kfree(pinode);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return error;
	}
	/* do a lookup so we can fill in the inode. not a special lookup also */
	PDEBUG(D_DIR, "capfs_symlink calling ll_capfs_lookup\n");
	if ((error = ll_capfs_lookup(capfs_sbp(dir->i_sb), pinode->name,
	len_dir + len_link + 1, &meta, 0)) < 0)
	{
		kfree(pinode);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return error;
	}

	/* fill in inode structure and remainder of capfs_inode */
	if ((inode = iget(dir->i_sb, meta.handle)) == NULL) {
		kfree(pinode);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return -ENOMEM;
	}

	pinode->handle = inode->i_ino;
	pinode->super = capfs_sbp(inode->i_sb);
	capfs_meta_to_inode(&meta, inode);
	inode->u.generic_ip = pinode;

	d_instantiate(entry, inode); 

	PDEBUG(D_DIR, "saved name is %s\n", capfs_inop(inode)->name);

	if (S_ISDIR(inode->i_mode)) { /* can this be a non-link */
		inode->i_op = &capfs_dir_inode_operations;
		inode->i_fop = &capfs_dir_operations;
	}
   else if (S_ISREG(inode->i_mode)) { /* can this be a non-link? */
		inode->i_op = &capfs_file_inode_operations;
		inode->i_fop = &capfs_file_operations;
		inode->i_data.a_ops = &capfs_file_aops;
	}
	else if (S_ISLNK(inode->i_mode)) {
		inode->i_op = &capfs_symlink_inode_operations;
		inode->i_data.a_ops = &capfs_file_aops;
	}
   else {
		inode->i_op = NULL;
		inode->i_fop = NULL;
	}
	PEXIT;
	write_unlock_inode(parent_pinode);
	return error;
}


/* capfs_rmdir()
 *
 * Called on parent directory with dentry of directory to remove.
 *
 * TODO: I'm not so sure that I know everything this call should be
 * doing; look at minix/namei.c:minix_rmdir() for some possible ideas.
 */
int capfs_rmdir(struct inode *dir, struct dentry *entry)
{
	int error = 0;
	struct capfs_inode *parent_pinode = NULL;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.rmdir++;       
	PENTRY;
	parent_pinode = capfs_inop(dir);
	if((error = write_lock_inode(parent_pinode)) < 0) {
		 return error;
	}

	if (!d_unhashed(entry))
	{
		PDEBUG(D_DIR, "capfs_rmdir failing because dir (%s) is busy.\n", 
			entry->d_name.name);
		write_unlock_inode(parent_pinode);
		return -EBUSY;
	}

	PDEBUG(D_DIR, "capfs_rmdir called on %ld for %s\n", dir->i_ino,
	entry->d_name.name);
	if (entry->d_inode == NULL) {
		/* negative dentry ?!? */
		PERROR("capfs_rmdir: negative dentry?\n");
		/* TODO: should look up the file? */
		write_unlock_inode(parent_pinode);
		PEXIT;
		return -ENOENT;
	}
	if((error = may_delete(dir, entry, 1))) {
		write_unlock_inode(parent_pinode);
		PEXIT;
		return error;
	}
	if (entry->d_inode->i_nlink == 0) {
		PERROR("capfs_rmdir: deleting nonexistent directory?\n");
	}

	error = ll_capfs_rmdir(capfs_inop(entry->d_inode));

	if (error == 0)
	{
		entry->d_inode->i_nlink--;	
	}
	write_unlock_inode(parent_pinode);

	PEXIT;
	return error;
}


/* capfs_rename(old_dir, old_dentry, new_dir, new_dentry)
 *
 * PARAMETERS:
 * old_dir - inode of parent directory of file/dir to be moved
 * new_dir - inode of parent directory of destination
 * old_dentry - dentry referring to the file/dir to move
 * new_dentry - dentry referring to the destination; inode might or
 *   might not be available.
 *
 * NOTES:
 * If the target name already exists, there will be a valid inode in the
 * new_dentry.  If the inode pointer is NULL, then there is nothing
 * referred to by that name.
 *
 * Also need to update the dcache and such to take changes into account!
 *
 * see fs/nfs/dir.c:nfs_rename() for an example.
 * see fs/minix/namei.c:minix_rename() for a different example.
 *
 * It's not enough to just mark the various inodes as dirty.  We need to
 * ensure that the directory cache is updated.  This turns out to be
 * easy; just call d_move(old_dentry, new_dentry) to get the dentries
 * updated!  Or at least, that seems to be working <smile>...
 *
 * Returns 0 on success, -errno on failure.
 */
int capfs_rename(struct inode *old_dir, struct dentry *old_dentry, 
struct inode *new_dir, struct dentry *new_dentry)
{
	int error = -ENOSYS, isdir;
	struct inode *new_inode;
	struct capfs_inode *old_pinode, *new_pinode;
	struct capfs_inode *old_parent_pinode = NULL, *new_parent_pinode = NULL;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.rename++;   
	PENTRY;

	old_parent_pinode = capfs_inop(old_dir);
	new_parent_pinode = capfs_inop(new_dir);
	if((error = write_lock_inode(old_parent_pinode)) < 0) {
		 return error;
	}
	if(new_parent_pinode != old_parent_pinode) {
		if((error = write_lock_inode(new_parent_pinode)) < 0) {
			 write_unlock_inode(old_parent_pinode);
			 return error;
		}
	}
	isdir = S_ISDIR(old_dentry->d_inode->i_mode);
	/* Are we allowed to delete from old_dir? */
	if((error = may_delete(old_dir, old_dentry, isdir))) {
	   if(new_parent_pinode != old_parent_pinode) {
			 write_unlock_inode(new_parent_pinode);
	   }
	   write_unlock_inode(old_parent_pinode);
	   return error;
	}
	old_pinode = capfs_inop(old_dentry->d_inode);
	new_inode = new_dentry->d_inode;
	if (new_inode == NULL) {
		/* no file at new name; need to create a fake capfs_inode */
		/* namebuf is big enough for <host>:port/<name> */
		char *namebuf;
		struct capfs_inode fake_pinode;
		
		if((error = may_create(new_dir, new_dentry))) {
			  if(new_parent_pinode != old_parent_pinode) {
				  write_unlock_inode(new_parent_pinode);
			  }
			  write_unlock_inode(old_parent_pinode);
			  return error;
		}
		
		init_capfs_rwsem(&fake_pinode.lock);

		namebuf = (char *) kmalloc(CAPFSHOSTLEN + CAPFSNAMELEN + 8, GFP_KERNEL);
		if (namebuf == NULL) {
			  if(new_parent_pinode != old_parent_pinode) {
				  write_unlock_inode(new_parent_pinode);
			  }
			  write_unlock_inode(old_parent_pinode);
			  return 0;
		}

		PDEBUG((D_DIR | D_FILE), "capfs_rename, no inode for new file\n");
		if ((strlen(old_pinode->name) + new_dentry->d_name.len + 2) >
		CAPFSHOSTLEN + CAPFSDIRLEN + 7) {
			/* should never happen */
			PERROR("capfs_rename: string too long?!?\n");
			PEXIT;
			kfree(namebuf);
			if(new_parent_pinode != old_parent_pinode) {
				write_unlock_inode(new_parent_pinode);
			}
	 		write_unlock_inode(old_parent_pinode);
			return -ENOSYS;
		}
		sprintf(namebuf, "%s/%s", capfs_inop(new_dir)->name,
		new_dentry->d_name.name);
		fake_pinode.handle = 0;
		fake_pinode.name = namebuf;
		fake_pinode.super = capfs_sbp(new_dir->i_sb);

		PDEBUG((D_DIR | D_FILE), "capfs_rename called, %s -> %s (new)\n",
		old_pinode->name, namebuf);

		error = ll_capfs_rename(old_pinode, &fake_pinode);
		kfree(namebuf);
	}
	else {
		/* a file already exists with new name */
		if((error = may_delete(new_dir, new_dentry, isdir))) {
			  if(new_parent_pinode != old_parent_pinode) {
				  write_unlock_inode(new_parent_pinode);
			  }
			  write_unlock_inode(old_parent_pinode);
			  return error;
		}
		new_pinode = capfs_inop(new_dentry->d_inode);
		PDEBUG((D_DIR | D_FILE), "capfs_rename called, %s -> %s\n",
		old_pinode->name, new_pinode->name);

		error = ll_capfs_rename(old_pinode, new_pinode);
	}
	
	/* Update the dcache */
	d_move(old_dentry, new_dentry);
	if(new_parent_pinode != old_parent_pinode) {
		write_unlock_inode(new_parent_pinode);
	}
 	write_unlock_inode(old_parent_pinode);

	PEXIT;
	return error;
}

/* capfs_readdir()
 *
 */
int capfs_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	int ret, len, i;
	struct capfs_dirent *dbuf = NULL;
	struct inode *inode;
	capfs_off_t pos;
	struct capfs_inode *parent_pinode = NULL;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.readdir++;   
	PENTRY;

	/* FIXME: Right now we are using FETCH_DENTRY_COUNT * ~1K of memory for this buffer */

	dbuf = (struct capfs_dirent *)vmalloc(FETCH_DENTRY_COUNT * sizeof(struct capfs_dirent));
	if(!dbuf) {
		PERROR("Could not allocate dbuf for process %d\n",(int)current->pid);
		return -ENOMEM;
	}

	inode = file->f_dentry->d_inode;
	parent_pinode = capfs_inop(inode);
	if((ret = read_lock_inode(parent_pinode)) < 0) {
		 vfree(dbuf);
		 return ret;
	}

	PDEBUG(D_DIR, "capfs_readdir called for %ld.\n", inode->i_ino);

	pos = file->f_pos;
	while(1) {
		int count;
		if ((ret = ll_capfs_readdir(capfs_inop(inode), dbuf, &pos, FETCH_DENTRY_COUNT)) < 0)
			goto out;  /* error */
		count = ret;

		for(i=0; i < count; i++) {
			len = strlen(dbuf[i].name);
			/* glibc did not give us sufficient buffer space */
			if ((ret = filldir(dirent, dbuf[i].name, len, dbuf[i].off, dbuf[i].handle, DT_UNKNOWN))) {
				file->f_pos = dbuf[i].off;
				ret = 0;
				goto out;
			}
		}
		/* ll_capfs_readdir gives us the position to start from the next time if count was != 0 */
		if (count != 0) {
			file->f_pos = pos;
		}
		
		/* eof */
		if(count != FETCH_DENTRY_COUNT) {
			ret = 0;
			goto out;
		}
	}
out:
	read_unlock_inode(parent_pinode);
	vfree(dbuf);
	PEXIT;
	return ret;
}

/* capfs_fsync(file, dentry, [datasync])
 *
 */
int capfs_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	int error = 0;
	struct capfs_inode *pinode;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.fsync++; 
	PENTRY;

	lock_kernel();
	pinode = capfs_inop(dentry->d_inode);

	error = ll_capfs_fsync(pinode);
	unlock_kernel();

	PEXIT;
	return error;
}

/* capfs_dir_read()
 *
 * Returns -EISDIR.  Just here to get the right error value back.
 */
static ssize_t capfs_dir_read(struct file *file, char *buf, size_t count,
loff_t *ppos)
{
	PENTRY;
	PEXIT;
	return -EISDIR;
}

/* capfs_revalidate_inode()
 *
 * Performs a getmeta operation to ensure that the data in the inode is
 * up-to-date.
 *
 * QUESTION: Do we need a lock on the inode in here?
 */
int capfs_revalidate_inode(struct dentry *dentry)
{
	int error = 0, len_dir, len_file;
	struct inode *inode;
	struct dentry *parent_dentry;
	struct inode *parent_inode;
	struct capfs_inode *pinode, *parent_pinode = NULL;
	struct capfs_meta meta;
	struct capfs_phys phys;
	capfs_handle_t old_handle;
	umode_t old_mode;
	char *ptr;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.revalidate++;    
	PENTRY;

	lock_kernel();

	inode = dentry->d_inode;
	if (inode == NULL) {
		PERROR("capfs_revalidate_inode called for NULL inode\n");
		unlock_kernel();
		PEXIT;
		return -EINVAL;
	}

	/* dead dirs are ones that have disappeared while there are still open files.
	 * this is ok; we were seeing this when we weren't expecting it though, thus
	 * the log message.
	 */
#ifdef IS_DEADDIR
	if(IS_DEADDIR(dentry->d_inode))
	{
		PERROR("DEAD directory detected going into capfs_revalidate_inode.\n");
	}
#endif

	/* get the parent dentry and inode -- this should be optimized later */
	parent_dentry = dentry->d_parent;
	if (parent_dentry == NULL) {
		PERROR("capfs_revalidate_inode: d_parent is NULL\n");
		unlock_kernel();
		PEXIT;
		return -EINVAL;
	}

	parent_inode = parent_dentry->d_inode;
	if (parent_inode == NULL) {
		PERROR("capfs_revalidate_inode: parent inode is NULL\n");
		unlock_kernel();
		PEXIT;
		return -EINVAL;
	}
	parent_pinode = capfs_inop(parent_inode);
	if((error = read_lock_inode(parent_pinode)) < 0) {
		unlock_kernel();
		PEXIT;
		return error;
	}
	
	/* reconstruct the name in the inode */
	len_dir = strlen(capfs_inop(parent_inode)->name);
	len_file = dentry->d_name.len;
	pinode = capfs_inop(inode);

	/* save the old handle value for debugging test */
	old_handle = pinode->handle;
	old_mode = inode->i_mode;

	/* if we're not looking at the root of a file system, we need to 
	 * rebuild the filename.  it's important that pinode point to
	 * capfs_inop(inode) at this point in case we don't go in the "if".
	 */
	if (parent_inode != inode) {
		/* the "1" takes into account the '/' between file and dir names
		 *
		 * if the old name is longer or of equal length, we don't realloc.
		 */
		if (strlen(pinode->name) < len_file + len_dir + 1) {
			struct capfs_inode *old_pinode;

			/* we need to reallocate the capfs_inode structure and file name.
			 *
			 * the "2" takes into account the '/' between names and the
			 * trailing '\0'.
			 */
         old_pinode = pinode;
			pinode = (struct capfs_inode *) kmalloc(sizeof(struct capfs_inode)
																+ len_dir + len_file + 2, 
																GFP_KERNEL);
			if (pinode == NULL) {
				PERROR("capfs_revalidate_inode: out of memory!\n");
				unlock_kernel();
				read_unlock_inode(parent_pinode);
				PEXIT;
				return -ENOMEM;
			}

			*pinode = *old_pinode; /* copy in all the old values */
			init_capfs_rwsem(&pinode->lock);
			pinode->name = (int8_t *)pinode + sizeof(struct capfs_inode);
			inode->u.generic_ip = pinode;

			kfree(old_pinode);
		}
		
		/* copy the name into the inode: copy dirname, add /, copy name */
		ptr = pinode->name;
		strcpy(ptr, capfs_inop(parent_inode)->name);
		ptr += len_dir;
		*ptr = '/';
		ptr++;
		strcpy(ptr, dentry->d_name.name);
	}

	PDEBUG(D_DIR, "capfs_revalidate_inode called for %s (%ld).\n",
	pinode->name, (unsigned long) pinode->handle);

	/* Note: the only parameter that really is used inside of ll_capfs_getmeta() 
	 *       is the file name. 
    */
	error = ll_capfs_getmeta(pinode, &meta, &phys);
	if (error < 0) {
		PDEBUG(D_DIR,
		"capfs_revalidate: ll_capfs_getmeta() failed; updating dcache\n");

		/* leave the dcache alone; the calling function handles cleanup */
		unlock_kernel();
		read_unlock_inode(parent_pinode);
		PEXIT;
		return error;
	}

	/* NOTE: not doing anything with the phys for now */
	capfs_meta_to_inode(&meta, inode);

	if (old_handle != meta.handle) {
		/* need to update the inode and redo the hash */
		PDEBUG(D_DIR, 
				 "capfs_revalidate_inode: handle changed from %ld to %ld for %s\n",
				 (unsigned long) old_handle, 
				 (unsigned long) meta.handle, pinode->name);

		remove_inode_hash(inode);
		inode->i_ino = meta.handle;
		pinode->handle = meta.handle;
		insert_inode_hash(inode);
	}
	if (old_mode != inode->i_mode) {
		/* need to update function pointers */
		if (S_ISDIR(inode->i_mode)) {
			inode->i_op = &capfs_dir_inode_operations;
			inode->i_fop = &capfs_dir_operations;
		}
		else if (S_ISREG(inode->i_mode)) {
			inode->i_op = &capfs_file_inode_operations;
			inode->i_fop = &capfs_file_operations;
			inode->i_data.a_ops = &capfs_file_aops;
		}
		else if (S_ISLNK(inode->i_mode)) {
			inode->i_op = &capfs_symlink_inode_operations;
			inode->i_data.a_ops = &capfs_file_aops;
		}
		else {
			PERROR("capfs_revalidate_inode: handle refers to something other than a file or a directory?\n");
			error = -EINVAL;
		}
	}
	PDEBUG(D_DIR, "capfs_revalidate_inode returning %d\n", error);
	unlock_kernel();
	read_unlock_inode(parent_pinode);
	PEXIT;
	return error;
}

/* capfs_dentry_revalidate()
 *
 * A return value of "1" indicates that things are ok.  A return value of "0"
 * should result in the upper layers trying to prune out the dentry (bad).
 */
static int capfs_dentry_revalidate(struct dentry *dentry, int flags)
{
	int ret;
	PENTRY;

	PDEBUG(D_DIR, "capfs_dentry_revalidate called for %s.\n",
	dentry->d_name.name);

	/* don't call revalidate inode if the inode isn't valid */
	if (dentry->d_inode == NULL) {
		PDEBUG(D_DIR, "capfs_dentry_revalidate: NULL inode (negative dentry) hit.\n");
		return 0;
		PEXIT;
	}

	ret = capfs_revalidate_inode(dentry);
	PEXIT;
	if (ret == 0) return 1;
	else return 0; /* we got an error - let it be handled elsewhere. */
}


/* capfs_meta_to_inode()
 *
 * Copies a capfs_meta structure's values into an inode structure.
 */
int capfs_meta_to_inode(struct capfs_meta *mbuf, struct inode *ibuf)
{
	int error = 0;
	PENTRY;

	/* leaving the handle/i_ino fields alone for now */

	ibuf->i_mode = mbuf->mode;
	ibuf->i_uid = mbuf->uid;
	ibuf->i_gid = mbuf->gid;
	ibuf->i_size = mbuf->size;
	ibuf->i_atime = mbuf->atime;
	ibuf->i_mtime = mbuf->mtime;
	ibuf->i_ctime = mbuf->ctime;
	ibuf->i_blksize = mbuf->blksize;
	ibuf->i_blocks = mbuf->blocks;

	PEXIT;
	return error;
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
