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

#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>



#include "capfs_linux.h"
#include "ll_capfs.h"
#include "capfs_proc.h"

/* external variables */
extern int capfs_maxsz;

/* file operations */
static int capfs_readpage(struct file *file, struct page * page);
static ssize_t capfs_file_read(struct file *file, char *buf, size_t count,
	loff_t *ppos);
static ssize_t capfs_file_write(struct file *file, const char *buf,
	size_t count, loff_t *ppos);
static void capfs_file_truncate(struct inode *);
static int capfs_file_mmap(struct file *, struct vm_area_struct *);
static loff_t capfs_file_llseek(struct file *, loff_t, int);

struct address_space_operations capfs_file_aops = {
	readpage:      capfs_readpage     /* readpage */
};

struct inode_operations capfs_file_inode_operations = {
	create:     capfs_file_create,      /* create */
	rename:     capfs_rename,           /* rename */
	truncate:   capfs_file_truncate,    /* truncate */
	revalidate: capfs_revalidate_inode, /* revalidate */
	setattr:    capfs_notify_change     /* setattr */
};

/* file_operations notes:
 *
 * we're going to use mm/filemap.c:generic_file_mmap() here.  It will 
 * rely on our readpage function to work, so we'll have to get that
 * going.
 *
 */
struct file_operations capfs_file_operations = {
	llseek:  capfs_file_llseek,/* lseek */
	read:    capfs_file_read,  /* read */
	write:   capfs_file_write, /* write */
	mmap:    capfs_file_mmap,  /* mmap - we'll try the default */
	open:    capfs_open,       /* open called on first open instance of file */
	release: capfs_release,    /* release called when last open instance closed */
	fsync:   capfs_fsync       /* fsync */
};

/* capfs_file_truncate()
 *
 * This is called from fs/open.c:do_truncate() and is called AFTER
 * the notify_change.  The notify_change MUST use inode_setattr() or
 * some similar method to get the updated size into the inode, or we
 * won't have it to use.
 *
 * Perhaps notify_change() should be setting the inode values but isn't?
 */
static void capfs_file_truncate(struct inode *inode)
{
	int error;
	struct capfs_meta meta;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.truncate++;     
	PENTRY;

	PDEBUG(D_FILE, "capfs_truncate called for %s, size = %Ld\n",
	capfs_inop(inode)->name, (long long) inode->i_size);

	memset(&meta, 0, sizeof(meta));
	meta.valid = V_SIZE;
	meta.size = inode->i_size;

	if ((error = ll_capfs_setmeta(capfs_inop(inode), &meta, current->fsuid, current->fsgid)) != 0) {
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

/*
 * try to invalidate file-blocks from the Linux-page cache. I think this stuff should work,
 * but I dont know if I am doing everything right here.
 */
static int do_invalidate_file_pages(struct inode *inode, unsigned long start_offset, unsigned long size)
{
	struct address_space *asp;
	unsigned long start, end, index;
	if(!inode || !(asp = inode->i_mapping)) {
		return -EINVAL;
	}
	/* start is the logical page cache granular starting index 
	 * and end is the logical page cache granular ending index
	 */
	start = PAGE_CACHE_ALIGN(start_offset) >> PAGE_CACHE_SHIFT;
	end   = PAGE_CACHE_ALIGN(start_offset + size) >> PAGE_CACHE_SHIFT;
	for(index = start; index <= end; index++) {
		struct page* page = find_lock_page(asp, index);
		if(page) {
			/* Mark it as not uptodate, necessitating(sp?) a reread from disk/network whatever */
			ClearPageUptodate(page); 
			UnlockPage(page);
		}
		/* not there in page cache.*/
	}
	return 0;
}

/* Needed to trap munmap operations to free up the data structures */
static void capfs_vma_close(struct vm_area_struct *vma)
{
	return;
}

static struct vm_operations_struct capfs_vm_ops = {
	close: capfs_vma_close,
	nopage: filemap_nopage,
};

/* capfs_file_mmap()
 *
 * NOTES:
 * Tried setting the VM_WRITE flag in the vma to get through our tests
 * in capfs_map_userbuf, but that didn't work.
 */
static int capfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct address_space *mapping = file->f_dentry->d_inode->i_mapping;
	struct inode *inode = mapping->host;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.mmap++;    
	PENTRY;

	PDEBUG(D_FILE, "capfs_file_mmap called.\n");
	/* do not allow sneaky stores/mov's into the mapped address range */
	if ((vma->vm_flags & VM_SHARED) && (vma->vm_flags & VM_MAYWRITE)) {
		if (!mapping->a_ops->writepage)
			return -EINVAL;
	}
	if (!mapping->a_ops->readpage)
		return -ENOEXEC;
	UPDATE_ATIME(inode);  
	vma->vm_ops = &capfs_vm_ops;
	/* We want this mmap to fetch pages afresh from the IOD servers */
	do_invalidate_file_pages(inode, 0, inode->i_size);
	PEXIT;
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
	atomic_inc(&page->count);
	set_bit(PG_locked, &page->flags);
#if 0
	/* NOTE: THIS WAS COMMENTED OUT IN 2.4 CODE; I JUST WENT AHEAD AND
	 * COMMENTED IT OUT HERE TOO...
	 */
	set_bit(PG_free_after, &page->flags); /* not necessary ??? */
#endif
	
	/* from brw_page() */
	clear_bit(PG_uptodate, &page->flags);
	clear_bit(PG_error, &page->flags);

	/* this should help readpage work correctly for big mem machines */
	buf = (char *)kmap(page);

	offset = ((loff_t)page->index) << PAGE_CACHE_SHIFT;
#if 0
	/* THIS IS WHAT I HAD BEFORE */
	offset = pgoff2loff(page->index);
#endif

	inode = file->f_dentry->d_inode;

	/* Added by Alan Rainey 9-10-2003 */
	if(strcmp(file->f_dentry->d_name.name, (char *)(strrchr(capfs_inop(inode)->name, '/')+1)))
	{
		if ((error = capfs_revalidate_inode(file->f_dentry)) < 0) {
			PEXIT;
			return error;
		}
	}

	memset(buf, 0, count);

	PDEBUG(D_FILE, "capfs_readpage called for %s (%ld), offset %ld, size %ld\n",
	capfs_inop(inode)->name, (unsigned long) capfs_inop(inode)->handle,
	(long) offset, (long) count);

	error = ll_capfs_file_read(capfs_inop(inode), buf, count, &offset, 1);
	if (error <= 0) goto capfs_readpage_error;

	/* from brw_page() */
	set_bit(PG_uptodate, &page->flags);
capfs_readpage_error:
	kunmap(page);
	UnlockPage(page);
#if 0
	free_page(page_address(page)); /* NFS does this */
#endif
	__free_page(page); /* after_unlock_page() does this */

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
	if(strcmp(file->f_dentry->d_name.name, (char *)(strrchr(capfs_inop(inode)->name, '/')+1)))
	{
		if ((error = capfs_revalidate_inode(file->f_dentry)) < 0) {
			PEXIT;
			return error;
		}
	}

	PDEBUG(D_FILE, "capfs_file_read called for %s (%ld), offset %ld, size %ld\n",
	capfs_inop(inode)->name, (unsigned long) capfs_inop(inode)->handle,
	(long) capfs_pos, (long) count);

	if (access_ok(VERIFY_WRITE, buf, count) == 0){
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
		error = ll_capfs_file_read(capfs_inop(inode), buf, xfersize, &capfs_pos, kern_read);
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
	if (strcmp(file->f_dentry->d_name.name, (char *)(strrchr(capfs_inop(inode)->name, '/')+1)))
	{
		if ((error = capfs_revalidate_inode(file->f_dentry)) < 0) {
			PEXIT;
			return error;
		}
	}

	PDEBUG(D_FILE, "capfs_file_write called for %s (%ld), offset %ld, size %ld\n",
	capfs_inop(inode)->name, (unsigned long) capfs_inop(inode)->handle,
	(long) capfs_pos, (long) count);


	if	(access_ok(VERIFY_READ, (char *)buf, count) == 0){
		PEXIT;
		return -EFAULT;
	}

	if (count == 0){
		PEXIT;
		return 0;
	}
	/* 
	 *  COHERENCE_DESIRED:
	 *  Write Lock on the file from the desired offset to the desired size.
	 */

	/* split our operation into blocks of capfs_maxsz or smaller */
	do {
		xfersize = (count < capfs_maxsz) ? count : capfs_maxsz;

		error = ll_capfs_file_write(capfs_inop(inode), buf, xfersize, &capfs_pos, kern_write);
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
	if (capfs_pos > inode->i_size) {
		inode->i_size = capfs_pos;
	}

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

	pinode = capfs_inop(inode);

	PDEBUG(D_FILE, "capfs_release called for %s (%ld)\n", pinode->name,
	(unsigned long) pinode->handle);

	error = ll_capfs_hint(pinode, HINT_CLOSE, NULL);
	
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

	/* we need to calculate file size if a) the user wants append mode or
	 * b) if O_LARGEFILE is not used (so that we can throw an error if
	 * the file is to large)
	 */
	if ((file->f_flags & O_APPEND) || !(file->f_flags & O_LARGEFILE))
	{
		/* force a revalidate */
		PDEBUG(D_FILE, "capfs_open: getting most up to date size\n");

		if ((error = capfs_revalidate_inode(file->f_dentry)) < 0) {
			unlock_kernel();
			PEXIT;
			return error;
		}
	}

	/* make sure the file isn't too large */
	if (!(file->f_flags & O_LARGEFILE) && inode->i_size > MAX_NON_LFS)
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
		file->f_pos = inode->i_size;
	}

	unlock_kernel();
	PEXIT;
	return error;
}

/* capfs_file_create()
 */
int capfs_file_create(struct inode *dir, struct dentry *entry, int mode)
{
	int error = 0, len_dir, len_file;
	struct inode *inode;
	struct capfs_meta meta;
	struct capfs_phys phys;
	struct capfs_inode *pinode, *parent_pinode = NULL;
	char *ptr;
	/* update the statistics */
	if(capfs_collect_stats) capfs_vfs_stat.create++;
	PENTRY;
	parent_pinode = capfs_inop(dir);
	if((error = write_lock_inode(parent_pinode)) < 0) {
		 d_drop(entry);
		 PEXIT;
		 return error;
	}
	if((error = may_create(dir, entry))) {
		PDEBUG(D_FILE, "Not allowed to create file %d\n", error);
		d_drop(entry);
		write_unlock_inode(parent_pinode);
		 PEXIT;
		return error;
	}

	PDEBUG(D_FILE, "capfs_file_create called for %ld\n", dir->i_ino);

	len_dir = strlen(capfs_inop(dir)->name);
	len_file = entry->d_name.len;
	if ((pinode = (struct capfs_inode *) kmalloc(sizeof(struct capfs_inode)
	+ len_dir + len_file + 2, GFP_KERNEL)) == NULL){
		d_drop(entry);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return -ENOMEM;
	}

	/* build capfs_inode name field first */
	init_capfs_rwsem(&pinode->lock);
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

	phys.blksize = DEFAULT_BLKSIZE;
	phys.dist = DEFAULT_DIST;
	phys.nodect = DEFAULT_NODECT;
	
	PDEBUG(D_FILE, "capfs_file_create calling ll_capfs_create\n");
	if ((error = ll_capfs_create(capfs_sbp(dir->i_sb), pinode->name,
	len_dir + len_file + 1, &meta, &phys)) < 0)
	{
		kfree(pinode);
		d_drop(entry);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return error;
	}

	/* do a lookup so we can fill in the inode. not special */
	PDEBUG(D_FILE, "capfs_file_create calling ll_capfs_lookup\n");
	if ((error = ll_capfs_lookup(capfs_sbp(dir->i_sb), pinode->name,
	len_dir + len_file + 1, &meta, 0)) < 0)
	{
		kfree(pinode);
		d_drop(entry);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return error;
	}

	/* fill in inode structure and remainder of capfs_inode */
	if ((inode = iget(dir->i_sb, meta.handle)) == NULL) {
		kfree(pinode);
		d_drop(entry);
		write_unlock_inode(parent_pinode);
		PEXIT;
		return -ENOMEM;
	}

	pinode->handle = inode->i_ino;
	pinode->super = capfs_sbp(inode->i_sb);
	capfs_meta_to_inode(&meta, inode);
	inode->u.generic_ip = pinode;

	d_instantiate(entry, inode); /* do I know what this does?  nope! */

	PDEBUG(D_FILE, "capfs_file_create: saved name is %s\n",
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
			if ((error = capfs_revalidate_inode(file->f_dentry)) < 0) {
				PEXIT;
				return error;
			}

			offset += file->f_dentry->d_inode->i_size;
			break;
		case 1:
			offset += file->f_pos;
	}

	retval = -EINVAL;
	if (offset>=0 && offset<=file->f_dentry->d_inode->i_sb->s_maxbytes) {
		if (offset != file->f_pos) {
			file->f_pos = offset;
			file->f_reada = 0;
		}
		retval = offset;
	}
	
	PEXIT;
	return retval;
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

