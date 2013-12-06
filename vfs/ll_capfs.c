/*
 * Modified for CAPFS by
 * Murali Vilayannur (C) 2005.
 * vilayann@cse.psu.edu
 *
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

/* Low-level CAPFS VFS operations, called by our VFS functions.
 *
 * CALLS IMPLEMENTED IN THIS FILE:
 *   WORKING:
 *     ll_capfs_file_read()
 *     ll_capfs_readdir()
 *     ll_capfs_getmeta()
 *     ll_capfs_create()
 *     ll_capfs_lookup()
 *     ll_capfs_mkdir()
 *     ll_capfs_rmdir()
 *     ll_capfs_unlink()
 *     ll_capfs_file_write()
 *     ll_capfs_fsync()
 *     ll_capfs_statfs()
 *     ll_capfs_truncate()
 *     ll_capfs_setmeta()
 *     ll_capfs_hint()
 *   NOT IMPLEMENTED:
 *     ll_capfs_rename()
 *
 * NOTES:
 *
 * These calls are responsible for:
 * - handling communication with the CAPFS device
 * - checking error conditions on the returned downcall
 *
 */

#ifdef __KERNEL__
#define __NO_VERSION__
#include "capfs_kernel_config.h"
#endif

#include "ll_capfs.h"
#include "capfs_linux.h"
#include "capfsdev.h"

static void init_structs(struct capfs_upcall *up, struct capfs_downcall *dp,
	capfs_type_t type, int tcp, int cons);
static void init_mount_structs(struct capfs_upcall *up,
	struct capfs_downcall *dp, capfs_type_t type, int tcp, char *cons);

/* ll_capfs_lookup(sb, name, len, meta)
 * FIXME?: Ask Rob/Phil about the need to check if(len > CAPFSNAMELEN) and
 * return -ENAMETOOLONG. Currently, this parameter is ignored in all 
 * the functions. Should we nuke it out completely or use it as indicated above?
 *
 * Special_op is set to 1 only for the first mount time call.
 * All other lookups set this to 0. Needed for the user-space
 * daemon to go and register itself.
 */
int ll_capfs_lookup(struct capfs_super *sb, const capfs_char_t *name,
capfs_size_t len, struct capfs_meta *meta, int special_op)
{
	int error;
	struct capfs_upcall up;
	struct capfs_downcall down;

	PDEBUG(D_LLEV, "ll_capfs_lookup called for %s\n", name);
	if (special_op) {
		/* At mount time sb->cons is only set */
		init_mount_structs(&up, &down, LOOKUP_OP, sb->tcp, sb->cons);
	}
	else {
		/* At all other times sb->i_cons is also set */
		init_structs(&up, &down, LOOKUP_OP, sb->tcp, sb->i_cons);
	}
	strncpy(up.u.lookup.name, name, CAPFSNAMELEN);
	up.u.lookup.register_cb = special_op;
	strncpy(up.v1.fhname, name, CAPFSNAMELEN);
	up.flags = sb->flags;

	if ((error = capfsdev_enqueue(&up, &down)) < 0) {
		PERROR("ll_capfs_lookup failed on enqueue for %s\n", name);
		return error;
	}

	if (down.error < 0) {
		PDEBUG(D_DOWNCALL, "ll_capfs_lookup failed on downcall for %s\n", name);
		return down.error;
	}
	if (special_op) {
		/* ok, now the client has filled in the policy identifier for this file system */
		sb->i_cons = down.u.lookup.cons;
		PDEBUG(D_DOWNCALL, "ll_capfs_lookup obtained policy identifier %d\n", sb->i_cons);
	}
	*meta = down.u.lookup.meta;
#ifdef HAVE_MGR_LOOKUP
	meta->blocks = -1;
#else
	/* NOTE: for some reason the "blocks" value in the linux stat
	 * structure seems to need to be in terms of 512-byte blocks.  Dunno
	 * why, but du and others rely on it.  There are three places in this
	 * file where we account for this: ll_capfs_setmeta(),
	 * ll_capfs_lookup() and ll_capfs_getmeta().
	 */
	meta->blocks = (meta->size / 512) + ((meta->size % 512) ? 1 : 0);
#endif

	return 0;
}


/* ll_capfs_hint(inode, hint)
 *
 */
int ll_capfs_hint(struct capfs_inode *inode, capfs_type_t hint, struct capfs_stats *stats)
{
	int error;
	struct capfs_upcall up;
	struct capfs_downcall down; /* just needed to keep init_structs() happy */

	/* inode will be set to NULL in case we want stats */
	if (inode)
	{
		PDEBUG(D_LLEV, "ll_capfs_hint called for %s\n", inode->name);
		init_structs(&up, &down, HINT_OP, inode->super->tcp, inode->super->i_cons);
		up.u.hint.handle = inode->handle;
		strncpy(up.v1.fhname, inode->name, CAPFSNAMELEN);
		up.flags = inode->super->flags;
	}
	else 
	{
		if (hint != HINT_STATS)
		{
			PERROR("ll_capfs_hint: Invalid NULL inode passed! %d\n", hint);
			return -EINVAL;
		}
		else if (stats == NULL)
		{
			PERROR("ll_capfs_hint: Invalid NULL stats passed! %d\n", hint);
			return -EINVAL;
		}
		/* Last two parameters are of no use here */
		init_structs(&up, &down, HINT_OP, 1, 0);
		up.u.hint.handle = 0;
	}
	up.u.hint.hint = hint;

	if (hint == HINT_CLOSE || hint == HINT_OPEN)
	{
		if ((error = capfsdev_enqueue_noresponse(&up)) < 0) {
			PERROR("ll_capfs_hint failed on enqueue for %s\n", inode->name);
			return error;
		}
		/* there is no response; just return */
		return 0;
	}
	else 
	{
		if ((error = capfsdev_enqueue(&up, &down)) < 0) {
			PERROR("ll_capfs_hint[stats] failed on enqueue\n");
			return error;
		}
		/* copy the requested stats */
		memcpy(stats, &down.u.hint.stats, sizeof(struct capfs_stats));
		return 0;
	}
}

/* ll_capfs_rmdir(inode)
 */
int ll_capfs_rmdir(struct capfs_inode *inode)
{
	int error;
	struct capfs_upcall up;
	struct capfs_downcall down;

	PDEBUG(D_LLEV, "ll_capfs_rmdir called for %s\n", inode->name);

	init_structs(&up, &down, RMDIR_OP, inode->super->tcp, inode->super->i_cons);
	strncpy(up.u.rmdir.name, inode->name, CAPFSNAMELEN);
	strncpy(up.v1.fhname, inode->name, CAPFSNAMELEN);
	up.flags = inode->super->flags;

	if ((error = capfsdev_enqueue(&up, &down)) < 0) {
		PERROR("ll_capfs_rmdir failed on enqueue for %s\n", inode->name);
		return error;
	}

	if (down.error < 0) {
		PERROR("ll_capfs_rmdir failed on downcall for %s\n", inode->name);
		return down.error;
	}

	return 0;
}

/* ll_capfs_mkdir(sb, name, len, meta)
 */
int ll_capfs_mkdir(struct capfs_super *sb, const capfs_char_t *name,
capfs_size_t len, struct capfs_meta *meta)
{
	int error;
	struct capfs_upcall up;
	struct capfs_downcall down;

	PDEBUG(D_LLEV, "ll_capfs_mkdir called for %s\n", name);

	init_structs(&up, &down, MKDIR_OP, sb->tcp, sb->i_cons);
	strncpy(up.v1.fhname, name, CAPFSNAMELEN);
	strncpy(up.u.mkdir.name, name, CAPFSNAMELEN);
	up.u.mkdir.meta = *meta;
	up.flags = sb->flags;

	if ((error = capfsdev_enqueue(&up, &down)) < 0) {
		PERROR("ll_capfs_mkdir failed on enqueue for %s\n", name);
		return error;
	}

	if (down.error < 0) {
		PERROR("ll_capfs_mkdir failed on downcall for %s\n", name);
		return down.error;
	}

	return 0;
}

/* ll_capfs_create(sb, name, len, meta, phys)
 *
 * NOTE:
 * This function will silently ignore the situation where a file exists
 * prior to this call -- specifically if another node has created a file
 * between the time when this node has identified a file as not
 * existing, then this would normally cause an error (EEXIST).  Since
 * this can happen all the time with parallel applications, we will
 * ignore the error.
 *
 * PARAMETERS:
 * sb - pointer to CAPFS superblock information
 * name - pointer to full path name
 * len - length of file name (not used)
 * meta - pointer to desired metadata (input only)
 * phys - pointer to desired physical distribution (input only)
 *
 * Returns 0 on success, -errno on failure.
 */
int ll_capfs_create(struct capfs_super *sb, const capfs_char_t *name,
capfs_size_t len, struct capfs_meta *meta, struct capfs_phys *phys)
{
	int error;
	struct capfs_upcall up;
	struct capfs_downcall down;

	PDEBUG(D_LLEV, "ll_capfs_create called for %s\n", name);

	init_structs(&up, &down, CREATE_OP, sb->tcp, sb->i_cons);
	strncpy(up.v1.fhname, name, CAPFSNAMELEN);
	strncpy(up.u.create.name, name, CAPFSNAMELEN);
	up.u.create.meta = *meta;
	up.u.create.phys = *phys;
	up.flags = sb->flags;

	if ((error = capfsdev_enqueue(&up, &down)) < 0) {
		PERROR("ll_capfs_create failed on enqueue for %s\n", name);
		return error;
	}
	if (down.error < 0) {
		if (down.error == -EEXIST) {
			/* this is a common occurrence; file is created by one task
			 * between the time that one stat's for existence and then
			 * tries to create the file.  we will just act like everything
			 * is ok.
			 */
			return 0;
		}
		PERROR("ll_capfs_create failed on downcall for %s\n", name);
		return down.error;
	}

	return 0;
}

/* ll_capfs_rename(old_inode, new_inode)
 *
 */
int ll_capfs_rename(struct capfs_inode *old_inode, struct capfs_inode *new_inode)
{
	int error;
	struct capfs_upcall up;
	struct capfs_downcall down;

	PDEBUG(D_LLEV, "ll_capfs_rename called for %ld\n",
	(long) old_inode->handle);
	init_structs(&up, &down, RENAME_OP, old_inode->super->tcp, old_inode->super->i_cons);
	up.u.rename.handle = old_inode->handle;
	strncpy(up.v1.fhname, old_inode->name, CAPFSNAMELEN);
	strncpy(up.u.rename.new_name, new_inode->name, CAPFSNAMELEN);
	/* FIXME?: Why the redundant strncpy here? */
	strncpy(up.v1.fhname, old_inode->name, CAPFSNAMELEN);
	up.flags = old_inode->super->flags;

	if ((error = capfsdev_enqueue(&up, &down)) < 0) {
		PERROR("ll_capfs_rename failed on enqueue for %s\n", old_inode->name);
		return error;
	}

	if (down.error < 0) {
		PERROR("ll_capfs_rename failed on downcall for %s\n", old_inode->name);
		return down.error;
	}

	return 0;
}

/* ll_capfs_link(new_inode, old_inode)
 * new_inode is the inode of the hard link that is going to be created and
 * old_inode is the inode of the file to which it is going to be a link.
 */
int ll_capfs_link(struct capfs_inode *new_inode, struct capfs_inode *old_inode, struct capfs_meta *meta)
{
	int error;
	struct capfs_upcall up;
	struct capfs_downcall down;
	PDEBUG(D_LLEV, "ll_capfs_link called for %ld\n", (long) new_inode->handle);
	init_structs(&up, &down, LINK_OP, old_inode->super->tcp, old_inode->super->i_cons);
	up.u.link.meta = *meta;
	strncpy(up.v1.fhname, new_inode->name, CAPFSNAMELEN);
	strncpy(up.u.link.target_name, old_inode->name, CAPFSNAMELEN);

	if ((error = capfsdev_enqueue(&up, &down)) < 0) {
		PERROR("ll_capfs_link failed on enqueue for %s->%s\n", new_inode->name, old_inode->name);
		return error;
	}

	if (down.error < 0) {
		PERROR("ll_capfs_link failed on downcall for %s->%s\n", new_inode->name,old_inode->name);
		return down.error;
	}
	return 0;
}

/* ll_capfs_symlink(new_inode, old name, length)
 * new_inode is the inode of the symbolic link that is going to 
 * be created. old name is the name of the target file name and length
 * is the length of old_name.
 */
int ll_capfs_symlink(int use_tcp, int cons,
		struct capfs_inode *new_inode, const capfs_char_t* name, capfs_size_t len, struct capfs_meta *meta)
{
	int error;
	struct capfs_upcall up;
	struct capfs_downcall down;

	PDEBUG(D_LLEV, "ll_capfs_symlink called on %ld->%s\n", (long) new_inode->handle,name);
	init_structs(&up, &down, SYMLINK_OP, use_tcp, cons);
	if(len > CAPFSNAMELEN) {
		return -ENAMETOOLONG;
	}
	/* create a soft link */
	up.u.symlink.meta = *meta;
	strncpy(up.v1.fhname, new_inode->name, CAPFSNAMELEN);
	strncpy(up.u.symlink.target_name, name, CAPFSNAMELEN);

	if ((error = capfsdev_enqueue(&up, &down)) < 0) {
		PERROR("ll_capfs_symlink failed on enqueue for %s->%s\n", new_inode->name, name);
		return error;
	}

	if (down.error < 0) {
		PERROR("ll_capfs_symlink failed on downcall for %s->%s\n", new_inode->name, name);
		return down.error;
	}

	return 0;
}

/*
 * Read the contents of the symbolic link
 */
int ll_capfs_readlink(struct capfs_inode *new_inode, char *buffer, unsigned int buflen)
{
	int error;
	struct capfs_upcall up;
	struct capfs_downcall down;
	PDEBUG(D_LLEV, "ll_capfs_readlink called on %ld->%s\n", (long) new_inode->handle,new_inode->name);
	init_structs(&up, &down, READLINK_OP, new_inode->super->tcp, new_inode->super->i_cons);

	/* describe buffer */
	up.xfer.to_kmem = 1;
	up.xfer.ptr = buffer; /* note that this is a kernel space pointer */
	up.xfer.size = buflen;
	strncpy(up.v1.fhname, new_inode->name, CAPFSNAMELEN);
	PDEBUG(D_LLEV, "ll_capfs_readlink queued %p of size %ld\n", up.xfer.ptr, (long)up.xfer.size);
	if ((error = capfsdev_enqueue(&up, &down)) < 0) {
		PERROR("ll_capfs_readlink failed on enqueue for %s\n", new_inode->name);
		return error;
	}
	if (down.error < 0) {
		PERROR("ll_capfs_readlink failed on downcall for %s\n", new_inode->name);
		return down.error;
	}
	return 0;
}

/* ll_capfs_unlink(inode)
 *
 */
int ll_capfs_unlink(struct capfs_inode *inode)
{
	int error;
	struct capfs_upcall up;
	struct capfs_downcall down;
	
	PDEBUG(D_LLEV, "ll_capfs_unlink called for %ld\n", (long) inode->handle);
	init_structs(&up, &down, REMOVE_OP, inode->super->tcp, inode->super->i_cons);
	up.u.remove.handle = inode->handle;
	strncpy(up.v1.fhname, inode->name, CAPFSNAMELEN);
	up.flags = inode->super->flags;

	if ((error = capfsdev_enqueue(&up, &down)) < 0) {
		PERROR("ll_capfs_unlink failed on enqueue for %s\n", inode->name);
		return error;
	}

	if (down.error < 0) {
		PERROR("ll_capfs_unlink failed on downcall for %s\n", inode->name);
		return down.error;
	}

	return 0;
}

/* ll_capfs_setmeta(inode, meta)
 *
 */
int ll_capfs_setmeta(struct capfs_inode *inode, struct capfs_meta *meta,
	capfs_uid_t caller_uid, capfs_gid_t caller_gid)
{
	int error;
	struct capfs_upcall up;
	struct capfs_downcall down;
	
	PDEBUG(D_LLEV, "ll_capfs_setmeta called for %ld\n", (long) inode->handle);
	init_structs(&up, &down, SETMETA_OP, inode->super->tcp, inode->super->i_cons);
	up.u.setmeta.meta = *meta;
	up.u.setmeta.caller_uid = caller_uid;
	up.u.setmeta.caller_gid = caller_gid;
	strncpy(up.v1.fhname, inode->name, CAPFSNAMELEN);
	up.flags = inode->super->flags;

	if ((error = capfsdev_enqueue(&up, &down)) < 0) {
		PERROR("ll_capfs_setmeta failed on enqueue for %s\n", inode->name);
		return error;
	}

	if (down.error < 0) {
		PERROR("ll_capfs_setmeta failed on downcall for %s\n", inode->name);
		return down.error;
	}

	*meta = down.u.setmeta.meta;
	/* NOTE: for some reason the "blocks" value in the linux stat
	 * structure seems to need to be in terms of 512-byte blocks.  Dunno
	 * why, but du and others rely on it.  There are three places in this
	 * file where we account for this: ll_capfs_setmeta(),
	 * ll_capfs_lookup() and ll_capfs_getmeta().
	 */
	meta->blocks = (meta->size / 512) + ((meta->size % 512) ? 1 : 0);
	/* not doing anything with the phys info in the downcall for now */
	return 0;
}

/* ll_capfs_getmeta(inode, meta, phys)
 *
 * Note: the only value in these structures that has to be set correctly 
 *       prior to calling this function is inode->name.
 */
int ll_capfs_getmeta(struct capfs_inode *inode, struct capfs_meta *meta,
struct capfs_phys *phys)
{
	int error;
	struct capfs_upcall up;
	struct capfs_downcall down;
	
	PDEBUG(D_LLEV, "ll_capfs_getmeta called for %ld\n", (long) inode->handle);
	init_structs(&up, &down, GETMETA_OP, inode->super->tcp, inode->super->i_cons);
	strncpy(up.v1.fhname, inode->name, CAPFSNAMELEN);
	up.flags = inode->super->flags;

	if ((error = capfsdev_enqueue(&up, &down)) < 0) {
		PERROR("ll_capfs_getmeta failed on enqueue for %s\n", inode->name);
		return error;
	}

	if (down.error < 0) {
		PERROR("ll_capfs_getmeta failed on downcall for %s\n", inode->name);
		return down.error;
	}

	*meta = down.u.getmeta.meta;
	/* NOTE: for some reason the "blocks" value in the linux stat
	 * structure seems to need to be in terms of 512-byte blocks.  Dunno
	 * why, but du and others rely on it.  There are three places in this
	 * file where we account for this: ll_capfs_setmeta(),
	 * ll_capfs_lookup() and ll_capfs_getmeta().
	 */
	meta->blocks = (meta->size / 512) + ((meta->size % 512) ? 1 : 0);

	if (phys != NULL) *phys = down.u.getmeta.phys;
	return 0;
}

/* ll_capfs_statfs(sb, statfsbuf)
 */
int ll_capfs_statfs(struct capfs_super *sb, struct capfs_statfs *sbuf)
{
	int error;
	struct capfs_upcall up;
	struct capfs_downcall down;
	char *namebuf;

	/* can't put things like this on the stack, or we get an 
	 * over-run.
	 */
   namebuf = (char *) kmalloc(CAPFSHOSTLEN + CAPFSNAMELEN + 8, GFP_KERNEL);
	if (namebuf == NULL)	return 0;

	sprintf(namebuf, "%s:%d%s", sb->mgr, sb->port, sb->dir);
	
	PDEBUG(D_LLEV, "ll_capfs_statfs called\n");
	init_structs(&up, &down, STATFS_OP, sb->tcp, sb->i_cons);
	up.u.statfs.handle = 0;

	/* NOTE: THERE IS POTENTIAL FOR TRUNCATION HERE!!! */
	strncpy(up.v1.fhname, namebuf, CAPFSNAMELEN);
	up.flags = sb->flags;

	if ((error = capfsdev_enqueue(&up, &down)) < 0) {
		PERROR("ll_capfs_statfs failed on enqueue for %s\n", namebuf);
		kfree(namebuf);
		return error;
	}

	if (down.error < 0) {
		PERROR("ll_capfs_statfs failed on downcall for %s\n", namebuf);
		kfree(namebuf);
		return down.error;
	}

	*sbuf = down.u.statfs.statfs;
	kfree(namebuf);
	return 0;
}

/* ll_capfs_readdir(dir, off, dirent)
 *
 * Returns >=0 on success, (negative) error on failure.  In the case of EOF,
 * returns 0.
 */
int ll_capfs_readdir(struct capfs_inode *inode, struct capfs_dirent *dirent,
capfs_off_t *offp, int dir_count)
{
	int ret = 0;
	capfs_off_t off;
	struct capfs_upcall up;
	struct capfs_downcall down;

	off = *offp;

	init_structs(&up, &down, GETDENTS_OP, inode->super->tcp, inode->super->i_cons);
	up.u.getdents.handle = inode->handle;
	up.u.getdents.off = off;
	up.u.getdents.count = dir_count;

	/* describe buffer */
	up.xfer.to_kmem = 1;
	up.xfer.ptr = dirent; /* note: this is a kernel space buffer */
	up.xfer.size = sizeof(*dirent) * dir_count;
	strncpy(up.v1.fhname, inode->name, CAPFSNAMELEN);
	up.flags = inode->super->flags;

	if ((ret = capfsdev_enqueue(&up, &down)) < 0) {
		PERROR("ll_capfs_readdir failed on enqueue for %s\n", inode->name);
		return ret;
	}

	if (down.error < 0) {
		PERROR("ll_capfs_readdir failed on downcall for %s\n", inode->name);
		return down.error;
	}

	if ((down.u.getdents.eof != 0) || (down.u.getdents.count == 0)) {
		memset((dirent + down.u.getdents.count), 0, sizeof(struct capfs_dirent));
	}
	ret = down.u.getdents.count;

	PDEBUG(D_LLEV, "ll_capfs_readdir: name = %s, eof = %d, new off = %d\n",
	    dirent->name, down.u.getdents.eof, (int) down.u.getdents.off);

	/* pass back the new offset, as reported in the downcall */
	*offp = down.u.getdents.off;

	return ret;
}

/* ll_capfs_file_read()
 *
 * Returns size of data read on success.  Returns error (< 0) on
 * failure.  Sets new offset if and only if one or more bytes are
 * successfully read (is this a good idea?).
 *
 * The to_kmem flag indicates that the buffer is in kernel space, not in
 * user space.
 */
int ll_capfs_file_read(struct capfs_inode *inode, capfs_char_t *buf,
capfs_size_t count, capfs_off_t *offp, capfs_boolean_t to_kmem)
{
	int error = 0;
	struct capfs_upcall up;
	struct capfs_downcall down;

	PDEBUG(D_LLEV, "ll_capfs_file_read called for %s (%ld), size %ld, loc %ld\n",
	inode->name, (long) inode->handle, (long) count, (long) (*offp));

	init_structs(&up, &down, READ_OP, inode->super->tcp, inode->super->i_cons);
	up.u.rw.handle = inode->handle;
	up.u.rw.io.type = IO_CONTIG;
	up.u.rw.io.u.contig.off = *offp;
	up.u.rw.io.u.contig.size = count;

	/* buffer */
	up.xfer.to_kmem = to_kmem;
	up.xfer.ptr = buf;
	up.xfer.size = count;
	strncpy(up.v1.fhname, inode->name, CAPFSNAMELEN);
	up.flags = inode->super->flags;


	/* send off the request */
	if ((error = capfsdev_enqueue(&up, &down)) < 0) {
		/* error in servicing upcall */
		PERROR("ll_capfs_file_read failed on %ld\n", (long) inode->handle);
		return error;
	}

	if (down.error < 0) {
		/* error performing operation for process */
		PERROR("ll_capfs_file_read got error %d in downcall\n", down.error);
		return down.error;
	}

	if (down.u.rw.eof != 0) return 0;

	*offp += down.u.rw.size;
	return down.u.rw.size;
}

/* ll_capfs_file_write()
 *
 * Basically a carbon copy of ll_capfs_read_file()
 *
 * The to_kmem flag indicates that the buffer is in kernel space, not in
 * user space.
 */
int ll_capfs_file_write(struct capfs_inode *inode, capfs_char_t *buf,
capfs_size_t count, capfs_off_t *offp, capfs_boolean_t to_kmem)
{
	int error = 0;
	struct capfs_upcall up;
	struct capfs_downcall down;

	PDEBUG(D_LLEV, "ll_capfs_file_write called for %s (%ld), size %ld, loc %ld\n",
	inode->name, (long) inode->handle, (long) count, (long) (*offp));

	init_structs(&up, &down, WRITE_OP, inode->super->tcp, inode->super->i_cons);
	up.u.rw.handle = inode->handle;
	up.u.rw.io.type = IO_CONTIG;
	up.u.rw.io.u.contig.off = *offp;
	up.u.rw.io.u.contig.size = count;

	/* buffer */
	up.xfer.to_kmem = to_kmem;
	up.xfer.ptr = buf;
	up.xfer.size = count;
	strncpy(up.v1.fhname, inode->name, CAPFSNAMELEN);
	up.flags = inode->super->flags;

	/* send off the request */
	if ((error = capfsdev_enqueue(&up, &down)) < 0) {
		/* error in servicing upcall */
		PERROR("ll_capfs_file_write failed on %ld\n", (long) inode->handle);
		return error;
	}

	if (down.error < 0) {
		/* error performing operation for process */
		PERROR("ll_capfs_file_write got error %d in downcall\n", down.error);
		return down.error;
	}

	if (down.u.rw.eof != 0) return 0;

	*offp += down.u.rw.size;
	return down.u.rw.size;
}

/* ll_capfs_file_fsync()
 *
 */
int ll_capfs_fsync(struct capfs_inode *inode)
{
	int error = 0;
	struct capfs_upcall up;
	struct capfs_downcall down;

	PDEBUG(D_LLEV, "ll_capfs_fsync called for %ld\n", (long) inode->handle);
	init_structs(&up, &down, FSYNC_OP, inode->super->tcp, inode->super->i_cons);
	up.u.fsync.handle = inode->handle;
	strncpy(up.v1.fhname, inode->name, CAPFSNAMELEN);
	up.flags = inode->super->flags;

	/* send off the request */
	if ((error = capfsdev_enqueue(&up, &down)) < 0) {
		/* error in servicing upcall */
		PERROR("ll_capfs_fsync failed on %ld\n", (long) inode->handle);
		return error;
	}

	if (down.error < 0) {
		/* error performing operation for process */
		PERROR("ll_capfs_fsync got error %d in downcall\n", down.error);
		return down.error;
	}
	
	return 0;
}


/* init_structs()
 *
 * Initialize the downcall and upcall structures.
 */
static void init_structs(struct capfs_upcall *up,
	struct capfs_downcall *dp, capfs_type_t type, int tcp, int cons)
{
	memset(up, 0, sizeof(*up));
	memset(dp, 0, sizeof(*dp));
	up->magic = CAPFS_UPCALL_MAGIC;
	up->type = type;
	up->options.tcp = tcp;
	up->options.u.i_cons = cons;
	return;
}

static void init_mount_structs(struct capfs_upcall *up,
	struct capfs_downcall *dp, capfs_type_t type, int tcp, char *cons)
{
	memset(up, 0, sizeof(*up));
	memset(dp, 0, sizeof(*dp));
	up->magic = CAPFS_UPCALL_MAGIC;
	up->type = type;
	up->options.tcp = tcp;
	strcpy(up->options.u.s_cons, cons);
	return;
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


