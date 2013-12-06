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
#include "ll_capfs.h"
#include "capfs_proc.h"

/* external variables */
extern int capfs_maxsz;



/* capfs_file_truncate()
 *
 * This is called from fs/open.c:do_truncate() and is called AFTER
 * the notify_change.  The notify_change MUST use inode_setattr() or
 * some similar method to get the updated size into the inode, or we
 * won't have it to use.
 *
 */
static void capfs_file_truncate(struct inode *inode)
{
	int error;
	struct capfs_meta meta;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.truncate++;     
	PENTRY;

	PDEBUG(D_FILE, "capfs_truncate called for %s, size = %Ld\n",
			CAPFS_I(inode)->name, (long long) i_size_read(inode));

	memset(&meta, 0, sizeof(meta));
	meta.valid = V_SIZE;
	meta.size = i_size_read(inode);

	if ((error = ll_capfs_setmeta(CAPFS_I(inode), &meta, current->fsuid, current->fsgid)) != 0) {
		PERROR("capfs_truncate failed\n");
		PEXIT;
		return;
	}

	/* if we were so inclined, we could update all our other values here,
	 * since the setmeta gives us updated values back.
	 */

	PEXIT;
	return;
}

/* capfs_file_mmap()
 */
static int capfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct address_space *mapping = file->f_dentry->d_inode->i_mapping;
	struct inode *inode = file->f_dentry->d_inode;

	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.mmap++;    
	PENTRY;

	PDEBUG(D_FILE, "capfs_file_mmap called.\n");
	/* do not allow sneaky stores/mov's into the mapped address range */
	if ((vma->vm_flags & VM_SHARED) && (vma->vm_flags & VM_MAYWRITE)) {
		if (!mapping->a_ops->writepage)
			return -EINVAL;
	}
	inode->i_mapping->host = inode;
	inode->i_mapping->a_ops = &capfs_file_aops;
	if (!mapping->a_ops->readpage)
		return -ENOEXEC;
	vma->vm_flags |= VM_SEQ_READ;
	vma->vm_flags &= ~VM_RAND_READ;
	inode->i_mapping->backing_dev_info = &capfs_backing_dev_info;
	PEXIT;
	return generic_file_readonly_mmap(file, vma);
}

static int capfs_invalidatepage(struct page* page, unsigned long offset)
{
	ClearPageUptodate(page);
	ClearPageMappedToDisk(page);
	return 0;
}

/* capfs_readpage()
 *
 * Inside the page structure are "offset", the offset into the file, and
 * the page address, which can be found with "page_address(page)".
 *
 * See fs/nfs/read.c for readpage example.
 */
static int capfs_readpage(struct file *file, struct page *page)
{
	int error = 0;
	struct inode *inode;
	char *buf;
	capfs_off_t offset;
	size_t count = PAGE_SIZE;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.readpage++;   
	PENTRY;

	/* from generic_readpage() */
	get_page(page);
	/* from brw_page() */
	ClearPageUptodate(page);
	ClearPageError(page);

	/* this should help readpage work correctly for big mem machines */
	buf = (char *)kmap(page);

	offset = ((loff_t)page->index) << PAGE_CACHE_SHIFT;
#if 0
	/* THIS IS WHAT I HAD BEFORE */
	offset = pgoff2loff(page->index);
#endif

	inode = file->f_dentry->d_inode;

	/* Added by Alan Rainey 9-10-2003 */
	if(strcmp(file->f_dentry->d_name.name, (char *)(strrchr(CAPFS_I(inode)->name, '/')+1)))
	{
		if ((error = capfs_inode_getattr(file->f_dentry)) < 0) {
			put_page(page);
			PEXIT;
			return error;
		}
	}

	memset(buf, 0, count);

	PDEBUG(D_FILE, "capfs_readpage called for %s (%ld), offset %ld, size %ld\n",
			CAPFS_I(inode)->name, (unsigned long) CAPFS_I(inode)->handle,
	(long) offset, (long) count);

	error = ll_capfs_file_read(CAPFS_I(inode), buf, count, &offset, 1);
	if (error <= 0) 
	{
		SetPageError(page);
	}
	else
	{
		SetPageUptodate(page);
		ClearPageError(page);
	}
	flush_dcache_page(page);
	kunmap(page);
	unlock_page(page);
	put_page(page); 

	PEXIT;
	return error;
}


/* capfs_file_read(file, buf, count, ppos)
 *
 * Notes:
 * - generally the pointer to the position passed in is a pointer to
 * 	file->f_pos, so that doesn't need to be updated as well.
 *
 * TODO: Combine with capfs_file_write() to save space.
 */
static ssize_t capfs_file_read(struct file *file, char *cbuf, size_t count,
	loff_t *ppos)
{
	int error = 0, retsz = 0, kern_read = 0;
	struct inode *inode;
	capfs_off_t capfs_pos = *ppos;
	size_t xfersize;
	char *buf = (char *) cbuf;
	/* update the statistics and the access size*/
	if(capfs_collect_stats) {
		capfs_vfs_stat.read++;
		capfs_rwop_stats(0,count);
	} 
	/* So someone is passing us a kernel address instead of a user-address */
	if(get_fs().seg == get_ds().seg) {
		kern_read = 1;
	}    
	PENTRY;

	/* should we error check here? do we know for sure that the dentry is
	 * there?
	 */
	inode = file->f_dentry->d_inode;

	/* Added by Alan Rainey 9-10-2003 */
	if(strcmp(file->f_dentry->d_name.name, (char *)(strrchr(CAPFS_I(inode)->name, '/')+1)))
	{
		if ((error = capfs_inode_getattr(file->f_dentry)) < 0) {
			PEXIT;
			return error;
		}
	}

	PDEBUG(D_FILE, "capfs_file_read called for %s (%ld), offset %ld, size %ld\n",
			CAPFS_I(inode)->name, (unsigned long) CAPFS_I(inode)->handle,
			(long) capfs_pos, (long) count);

	if (access_ok(VERIFY_WRITE, buf, count) == 0){
		PEXIT;
		return -EFAULT;
	}

	if (count == 0)
	{
		PEXIT;
		return 0;
	}

	/* split our operation into blocks of capfs_maxsz or smaller */
	do {
		xfersize = (count < capfs_maxsz) ? count : capfs_maxsz;
		error = ll_capfs_file_read(CAPFS_I(inode), buf, xfersize, &capfs_pos, kern_read);
		if (error < 0){
			int err = error;
			PEXIT;
			return err;
		}
		retsz += error;

		/* position is updated by ll_capfs_file_read() */
		count -= xfersize;
		buf += xfersize;
	} while (count > 0 && error != 0);

	*ppos = capfs_pos;

	PEXIT;
	return retsz;
}

/* capfs_file_write(file, buf, count, ppos)
 *
 * Notes:
 * - generally the pointer to the position passed in is a pointer to
 * 	file->f_pos, so that doesn't need to be updated as well.
 */
static ssize_t capfs_file_write(struct file *file, const char *cbuf,
size_t count, loff_t *ppos)
{
	int error = 0, retsz = 0, kern_write = 0;
	struct inode *inode;
	capfs_off_t capfs_pos = *ppos;
	size_t xfersize;
	char *buf = (char *) cbuf;
	/* update the statistics */
	if(capfs_collect_stats) {
		capfs_vfs_stat.write++;
		capfs_rwop_stats(1,count);
	}
	if(get_fs().seg == get_ds().seg) {
		kern_write = 1;
	} 
	PENTRY;

	/* should we error check here? do we know for sure that the dentry is
	 * there?
	 */
	inode = file->f_dentry->d_inode;

	/* Added by Alan Rainey 9-10-2003 */
	if (strcmp(file->f_dentry->d_name.name, (char *)(strrchr(CAPFS_I(inode)->name, '/')+1)))
	{
		if ((error = capfs_inode_getattr(file->f_dentry)) < 0) {
			PEXIT;
			return error;
		}
	}

	PDEBUG(D_FILE, "capfs_file_write called for %s (%ld), offset %ld, size %ld\n",
			CAPFS_I(inode)->name, (unsigned long) CAPFS_I(inode)->handle,
			(long) capfs_pos, (long) count);


	if	(access_ok(VERIFY_READ, (char *)buf, count) == 0){
		PEXIT;
		return -EFAULT;
	}

	if (count == 0){
		PEXIT;
		return 0;
	}
	/* split our operation into blocks of capfs_maxsz or smaller */
	do {
		xfersize = (count < capfs_maxsz) ? count : capfs_maxsz;

		error = ll_capfs_file_write(CAPFS_I(inode), buf, xfersize, &capfs_pos, kern_write);
		if (error <= 0){
			int err = error;
			PEXIT;
			return err;
		}
		retsz += error;

		/* position is updated by ll_capfs_file_write() */
		count -= xfersize;
		buf += xfersize;
	} while (count > 0);

	*ppos = capfs_pos;
	if (capfs_pos > i_size_read(inode)) {
		i_size_write(inode, capfs_pos);
	}
	update_atime(inode);

	PEXIT;
	return retsz;
}

/* capfs_release()
 *
 * Called when the last open file reference for a given file is
 * finished.  We use this as an opportunity to tell the daemon to close
 * the file when it wants to.
 */
int capfs_release(struct inode *inode, struct file *f)
{
	int error = 0;
	struct capfs_inode *pinode;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.release++;   
	PENTRY;

	update_atime(inode);
	if (S_ISDIR(inode->i_mode))
	{
		return dcache_dir_close(inode, f);
	}

	pinode = CAPFS_I(inode);

	PDEBUG(D_FILE, "capfs_release called for %s (%ld) [atime: %lu] [mtime: %lu] [ctime: %lu]\n", 
			pinode->name, (unsigned long) pinode->handle, inode->i_atime.tv_sec,
			inode->i_mtime.tv_sec, inode->i_ctime.tv_sec);

	error = ll_capfs_hint(pinode, HINT_CLOSE, NULL);

	/* 
	 * remove all associated inode pages from the page cache
	 */
	truncate_inode_pages(f->f_dentry->d_inode->i_mapping, 0);
	
	PEXIT;
	return error;
}


/* capfs_open()
 *
 * Called from fs/open.c:filep_open()
 *
 * NOTES:
 * Truncation and creation are handled by fs/namei.c:open_namei()
 * We need to take care of O_APPEND here.
 *
 * The inode for the file is not necessarily up to date.  This is
 * important in the O_APPEND case because we need to have the most
 * recent size, so we're going to force a revalidate_inode() in the case
 * of an append.
 */
int capfs_open(struct inode *inode, struct file *file)
{
	int error = 0;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.open++;
	PENTRY;
	
	lock_kernel();
	inode->i_mapping->host = inode;
	inode->i_mapping->a_ops = &capfs_file_aops;
	inode->i_mapping->backing_dev_info = &capfs_backing_dev_info;

	if (S_ISDIR(inode->i_mode))
	{
		error = dcache_dir_open(inode, file);
		unlock_kernel();
		return error;
	}
	/* we need to calculate file size if a) the user wants append mode or
	 * b) if O_LARGEFILE is not used (so that we can throw an error if
	 * the file is to large)
	 */
	if ((file->f_flags & O_APPEND) || !(file->f_flags & O_LARGEFILE))
	{
		/* force a revalidate */
		PDEBUG(D_FILE, "capfs_open: getting most up to date size\n");

		if ((error = capfs_inode_getattr(file->f_dentry)) < 0) {
			unlock_kernel();
			PEXIT;
			return error;
		}
	}

	/* make sure the file isn't too large */
	if (!(file->f_flags & O_LARGEFILE) && i_size_read(inode) > MAX_NON_LFS)
	{
		unlock_kernel();
		PEXIT;
		return -EFBIG;
	}

	if (file->f_flags & O_APPEND)
	{
		/* set the file position to point to one byte past the last byte
		 * in the file
		 */
		file->f_pos = i_size_read(inode);
	}
	error = generic_file_open(inode, file);
	unlock_kernel();
	PEXIT;
	return error;
}

/* capfs_file_create()
 */
int capfs_file_create(struct inode *dir, struct dentry *entry, int mode, struct nameidata *nd)
{
	int error = 0, len_dir, len_file;
	struct inode *inode;
	struct capfs_meta meta;
	struct capfs_phys phys;
	struct capfs_inode *pinode, *parent_pinode = NULL;
	char *ptr, *namebuf;

	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.create++;
	PENTRY;
	parent_pinode = CAPFS_I(dir);
	if((error = write_lock_inode(parent_pinode)) < 0) {
		 d_drop(entry);
		 PEXIT;
		 return error;
	}
	if((error = may_create(dir, entry, nd))) {
		PDEBUG(D_FILE, "Not allowed to create file %d\n", error);
		d_drop(entry);
		write_unlock_inode(parent_pinode);
		 PEXIT;
		return error;
	}

	PDEBUG(D_FILE, "capfs_file_create called for %ld\n", dir->i_ino);

	len_dir = strlen(CAPFS_I(dir)->name);
	len_file = entry->d_name.len;
	namebuf = (char *) kmalloc(len_dir + len_file + 2, GFP_KERNEL);
	if (namebuf == NULL)
	{
		d_drop(entry);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return -ENOMEM;
	}
	/* piece the whole file name together */
	ptr = namebuf;
	strcpy(ptr, CAPFS_I(dir)->name);
	ptr += len_dir;
	*ptr = '/';
	ptr++;
	strcpy(ptr, entry->d_name.name);

	/* do the create */
	meta.valid = V_MODE | V_UID | V_GID | V_TIMES;
	meta.uid = current->fsuid;
	meta.gid = current->fsgid;
	meta.mode = mode;
	meta.mtime = meta.atime = meta.ctime = CURRENT_TIME.tv_sec;

	phys.blksize = DEFAULT_BLKSIZE;
	phys.dist = DEFAULT_DIST;
	phys.nodect = DEFAULT_NODECT;
	
	PDEBUG(D_FILE, "capfs_file_create calling ll_capfs_create for %s [atime: %lu] [mtime: %lu] [ctime: %lu]\n", namebuf, 
			meta.atime, meta.mtime, meta.ctime);
	if ((error = ll_capfs_create(CAPFS_SB(dir->i_sb), namebuf,
					len_dir + len_file + 1, &meta, &phys)) < 0)
	{
		kfree(namebuf);
		d_drop(entry);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return error;
	}

	/* do a lookup so we can fill in the inode. not special */
	if ((error = ll_capfs_lookup(CAPFS_SB(dir->i_sb), namebuf,
					len_dir + len_file + 1, &meta, 0)) < 0)
	{
		kfree(namebuf);
		d_drop(entry);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return error;
	}

	/* fill in inode structure and remainder of capfs_inode */
	if ((inode = iget(dir->i_sb, meta.handle)) == NULL) 
	{
		kfree(namebuf);
		d_drop(entry);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return -ENOMEM;
	}
	pinode = CAPFS_I(inode);
	if (pinode == NULL)
	{
		PERROR("found NULL pinode pointer\n");
		iput(inode);
		kfree(namebuf);
		d_drop(entry);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return -EINVAL;
	}
	pinode->handle = inode->i_ino;
	capfs_meta_to_inode(&meta, inode);
	pinode->name = namebuf; /* name will be freed when the inode is freed and destroyed */
	pinode->super = CAPFS_SB(inode->i_sb);
	/* Initialize the newly allocated inode's operation vector etc  */
	capfs_fill_inode(inode, inode->i_mode, 0);
	PDEBUG(D_FILE, "capfs_file_create: after a lookup name %s [atime: %lu] [mtime: %lu] [ctime: %lu]\n", 
			CAPFS_I(inode)->name, meta.atime, meta.mtime, meta.ctime);
	d_instantiate(entry, inode); 
	/* Update the ctime and mtime of the parent directory */
	capfs_update_inode_time(dir);
	write_unlock_inode(parent_pinode);

	PEXIT;
	return 0;
}	     

/* capfs_file_llseek()
 *
 * NOTES: we mainly implement this so that we have the opportunity to
 * update our metadata information (ie file size) when lseek is called
 * with SEEK_END.  generic_file_llseek() relies on the inode's size
 * field being up to date and therefore is not always suitable in that
 * case
 */
static loff_t capfs_file_llseek(struct file *file, loff_t offset, int
	origin)
{
	long long retval;
	int error;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.lseek++;

	PENTRY;

	switch (origin) {
		case 2:
			/* force a revalidate */
			PDEBUG(D_FILE, "capfs_file_llseek: getting most up to date size\n");
			if ((error = capfs_inode_getattr(file->f_dentry)) < 0) {
				PEXIT;
				return error;
			}

			offset += i_size_read(file->f_dentry->d_inode);
			break;
		case 1:
			offset += file->f_pos;
	}

	retval = -EINVAL;
	if (offset >= 0 && offset <= file->f_dentry->d_inode->i_sb->s_maxbytes) {
		if (offset != file->f_pos) {
			file->f_pos = offset;
		}
		retval = offset;
	}
	
	PEXIT;
	return retval;
}

struct address_space_operations capfs_file_aops = {
	.readpage =       capfs_readpage,     /* readpage */
	.invalidatepage = capfs_invalidatepage,
};

struct inode_operations capfs_file_inode_operations = {
	.truncate =    capfs_file_truncate,    /* truncate */
	.getattr  =  	capfs_getattr,    /* getattr */
	.setattr =     capfs_setattr     /* setattr */
};

struct file_operations capfs_file_operations = {
	.llseek =   capfs_file_llseek,/* lseek */
	.read =     capfs_file_read,  /* read */
	.write =    capfs_file_write, /* write */
	.mmap =     capfs_file_mmap,  /* mmap - we'll try the default */
	.open =     capfs_open,       /* open called on first open instance of file */
	.release =  capfs_release,    /* release called when last open instance closed */
	.fsync =    capfs_fsync       /* fsync */
};

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 


