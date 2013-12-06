#ifndef _LL_CAPFS_H
#define _LL_CAPFS_H

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
 * Contact:  Murali Vilayannur    vilayann@cse.psu.edu
 */

#ifdef __KERNEL__
#include <linux/types.h>
#endif

#ifndef __KERNEL__
#ifdef LINUX
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#endif
#endif

#include "capfs_types.h"  /* from capfs source */

/* miscellaneous defines */
typedef uint32_t capfs_magic_t;
typedef int32_t capfs_error_t;
typedef int32_t capfs_seq_t; /* sequence number for identifying requests */
typedef uint8_t capfs_boolean_t;
enum {
	CAPFS_UPCALL_MAGIC = 0x12345678,
	CAPFS_DOWNCALL_MAGIC = 0x9abcdef0
};

enum {
	NO_SEQUENCE_NR = -1
};

/* these operation types are purely for debugging purposes! */
enum {
	TEST_UPWAIT = 667,
	TEST_UPWAIT_COMM = 668,
	TEST_CHEAT_DEC = 669, 
	TEST_RD_XFER = 670, 
	TEST_WR_XFER = 672,
	/* this ioctl is used by the daemon to invalidate any pages of a file from the page cache */
	INVALIDATE_CACHE = 674,
};

/* metadata associated defines */
typedef uint64_t capfs_size_t;
typedef time_t capfs_time_t;
typedef uint32_t capfs_uid_t;
typedef uint32_t capfs_gid_t;
typedef uint32_t capfs_mode_t;
typedef uint32_t bitfield_t;
enum {
	V_MODE = 1, /* mode is valid */
	V_UID = 2, /* UID is valid */
	V_GID = 4, /* GID is valid */
	V_SIZE = 8, /* file size is valid */
	V_TIMES = 16, /* access and modification time values are valid */
	V_BLKSIZE = 32, /* blocksize is valid */
	V_BLOCKS = 64, /* number of blocks is valid */
	V_CTIME = 128 /* ctime is valid */
};

/* physical distribution associated defines */
typedef uint32_t capfs_count_t;
typedef int16_t capfs_type_t;
enum {
	DIST_RROBIN = 1
};

/* superblock associated defines */
typedef uint32_t capfs_flags_t;
enum {
	CAPFSHOSTLEN = 64,
	CAPFSDIRLEN = 1023,
	CAPFSCONSLEN = 256,
};

/* io description associated defines */
enum {
	IO_CONTIG = 1
};

/* data transfer protocol associated defines */
typedef uint8_t capfs_xferproto_t;
enum {
	CAPFS_PROTO_TCP = 1,
	CAPFS_PROTO_UDP = 2,
};

/* hint associated defines */
enum {
	HINT_OPEN = 1,
	HINT_CLOSE = 2,
	HINT_STATS = 3,
};

/* getdents xfer granularity */
enum {
	FETCH_DENTRY_COUNT = 64, /* Impacts the amount of memory allocated per readdir also */
};

/* all the major structures */
struct capfs_statfs {
	capfs_size_t bsize; /* block size (in bytes) */
	capfs_size_t blocks; /* total blocks */
	capfs_size_t bfree; /* free blocks */
	capfs_size_t bavail; /* free blocks available to non-superuser */
	capfs_size_t files; /* total files */
	capfs_size_t ffree; /* free files */
	uint16_t namelen; /* maximum name length */
};

struct capfs_meta {
	capfs_handle_t handle; /* similar to inode number, always valid */
	bitfield_t valid; /* bitfield specifying what fields are valid */
	capfs_mode_t mode; /* protection */
	capfs_uid_t uid; /* user ID of owner */
	capfs_gid_t gid; /* group ID of owner */
	capfs_size_t size; /* total size of file in bytes */
	capfs_time_t atime; /* time of last access (all times under one bit) */
	capfs_time_t mtime; /* time of last modification */
	capfs_time_t ctime; /* time of last change */
	capfs_size_t blksize; /* ``suggested'' block size (usually strip size) */
	capfs_size_t blocks; /* number of ``suggested'' blocks */
};

struct capfs_procinfo {
	capfs_uid_t uid;
	capfs_gid_t gid;
};

enum {
	DEFAULT_BLKSIZE = 65536,
	DEFAULT_NODECT = -1,
	DEFAULT_DIST = DIST_RROBIN
};

struct capfs_phys {
	capfs_size_t blksize; /* unit of distribution (strip), in bytes */
	capfs_count_t nodect; /* number of nodes in distribution */
	capfs_type_t dist; /* distribution function indicator */
};

struct capfs_sb_info {
	capfs_flags_t flags;
	uint16_t port;
	uint32_t tcp;
	uint32_t    i_cons;
	capfs_char_t cons[CAPFSCONSLEN+1];/* null terminated */
	capfs_char_t mgr[CAPFSHOSTLEN+1]; /* null terminated */
	capfs_char_t dir[CAPFSDIRLEN+1]; /* null terminated */
	struct super_block *sb;
};

#ifdef __KERNEL__

struct capfs_inode {
	capfs_handle_t handle;
	capfs_char_t *name;
	struct capfs_sb_info *super;
	struct inode vfs_inode;
};

/* All of these functions are no-ops */
static __inline__ int read_lock_inode(struct capfs_inode *pinode)
{
	return 0;
}

static __inline__ int read_unlock_inode(struct capfs_inode *pinode)
{
	return 0;
}

static __inline__ int write_lock_inode(struct capfs_inode *pinode)
{
	return 0;
}

static __inline__ int write_unlock_inode(struct capfs_inode *pinode)
{
  return 0;
}

#endif

struct capfs_io {
	capfs_type_t type; /* type of I/O description */
	union {
		struct {
			capfs_off_t off; /* offset relative to entire logical file */
			capfs_size_t size; /* size of contiguous region to access */
		} contig;
	} u;
};

/* low level protocol structures
 *
 * Here we're doing something similar to the Coda upcall/downcall
 * concept.  This will probably be continued into the user-space
 * implementation at some point...
 *
 * Or maybe we'll decide we don't like it and scrap the whole thing?
 */

enum {
	NULL_OP     = -1, /* not implemented for v1.x */
	GETMETA_OP  = 2,
	SETMETA_OP  = 3,
	LOOKUP_OP   = 4,
	RLOOKUP_OP  = -1, /* not implemented for v1.x */
	READLINK_OP = 6, 
	CREATE_OP   = 7,
	REMOVE_OP   = 8,
	RENAME_OP   = 9,
	SYMLINK_OP  = 10,
	MKDIR_OP    = 11,
	RMDIR_OP    = 12,
	GETDENTS_OP = 13,
	STATFS_OP   = 14, /* not implemented until 1.4.8-pre-xxx */
	EXPORT_OP   = -1, /* not implemented for v1.x */
	MKDIRENT_OP = -1, /* not implemented for v1.x */
	RMDIRENT_OP = -1, /* not implemented for v1.x */
	FHDUMP_OP   = -1, /* not implemented for v1.x */
	HINT_OP     = 19,
	READ_OP     = 20,
	WRITE_OP    = 21,
	FSYNC_OP    = 22,
	LINK_OP     = 23,
	NUM_OPS     = 24,
	/* SHOULD THERE BE SOME SORT OF SYNC CALL?  DATASYNC? */
};

/* Add any other esoteric options here */
struct capfs_extra_options {
	int32_t tcp;/* should we use tcp or udp for meta-data operations? */
	union {
		int32_t i_cons; /* We either use an integer for the consistency semantic option */
		char    s_cons[CAPFSCONSLEN]; /* or the entire string */
	} u;
};

#define CAPFS_STATS_MAX 16
/* Extra statistics */
struct capfs_stats {
	/* hcache related stuff */
	int64_t    hcache_hits, hcache_misses, hcache_fetches, hcache_invalidates, hcache_evicts;
	/* hcache callback stats */
	int64_t    hcache_inv,  hcache_inv_range, hcache_upd;
	/* Number of write-retries */
	int64_t    retries;
	/* Time spent in SHA1 computation related stuff */
	int64_t    sha1_time;
	/* Time spent in get RPC and put RPC */
	int64_t    rpc_get, rpc_put, rpc_gethashes, rpc_compute;
	/* Time spent in committing writes alone */
	int64_t    rpc_commit;
	/* Time spent on each server */
	int64_t    server_get_time[CAPFS_STATS_MAX];
	int64_t    server_put_time[CAPFS_STATS_MAX];
};

struct capfs_upcall {
	capfs_magic_t magic;
	capfs_seq_t seq; /* only positive sequence numbers should be used */
	capfs_type_t type;
	struct capfs_procinfo proc; /* info on process for determining priveleges */
	capfs_flags_t flags; /* pass the super-block associated flag */
	struct capfs_extra_options options; /* capfs specific options for consistency semantics etc */
	union {
		struct {
			/* empty */
		} null;
		struct {
			capfs_handle_t handle;
		} getmeta;
		struct {
			struct capfs_meta meta;
			capfs_uid_t caller_uid;  /* uid and gid of caller */
			capfs_gid_t caller_gid;
		} setmeta;
		struct {
			capfs_char_t name[CAPFSNAMELEN+1];
			int         register_cb; /* this flag is set only on the first special lookup at mount time */
		} lookup;
		struct {
			/* not implemented in v1.x */
		} rlookup;
		struct {
			/* Name of the link is passed in v1 */
		} readlink;
		struct {
			capfs_char_t name[CAPFSNAMELEN+1];
			struct capfs_meta meta;
			struct capfs_phys phys;
		} create;
		struct {
			capfs_handle_t handle;
		} remove;
		struct {
			capfs_handle_t handle;
			capfs_char_t new_name[CAPFSNAMELEN+1];
		} rename;
		struct {
			struct capfs_meta meta;
			/* the symlink name is passed in fhname of v1 */
			capfs_char_t target_name[CAPFSNAMELEN+1]; /* the name of the file to which the symlink points */
		} symlink;
		struct {
			capfs_char_t name[CAPFSNAMELEN+1];
			struct capfs_meta meta;
		} mkdir;
		struct {
			/* why do we pass this name but not one for remove? */
			capfs_char_t name[CAPFSNAMELEN+1];
		} rmdir;
		struct {
			capfs_handle_t handle;
			capfs_off_t off;
			capfs_count_t count;
		} getdents;
		struct {
			capfs_handle_t handle;
		} statfs;
		struct {
			/* not implemented in v1.x */
		} export;
		struct {
			/* not implemented in v1.x */
		} mkdirent;
		struct {
			/* not implemented in v1.x */
		} rmdirent;
		struct {
			/* not implemented in v1.x */
		} fhdump;
		struct {
			capfs_handle_t handle;
			capfs_type_t hint;
		} hint;
		struct {
			capfs_handle_t handle;
		} fsync;
		struct {
			struct capfs_meta meta;
			/* the link name is passed in fhname of v1 */
			capfs_char_t target_name[CAPFSNAMELEN+1]; /* the name of the file to which the link points */
		} link;
		struct {
			capfs_handle_t handle;
			struct capfs_io io;
		} rw; /* read and write both handled here */
	} u;
	struct {
		/* v1.xx needs names, in terms of the manager's file system, for
		 * all calls.  newer code will not have this problem, so we're
		 * going to stick the name down here out of the way so it doesn't
		 * pollute our otherwise clean v2.xx operation list
		 *
		 * this structure will always hold the file name.
		 */
		capfs_char_t fhname[CAPFSNAMELEN+1];
	} v1;
	struct {
		void *ptr;
		capfs_size_t size;
		/* used in kernel to differentiate between
		 * user-space and kernel-space addresses
		 */
		capfs_boolean_t to_kmem;
		/* subtle: We also need a field to tell kcapfsd whether to free the pointer or not */
		capfs_boolean_t to_free_ptr;
	} xfer; /* implementation-specific data transfer elements */
};

struct capfs_downcall {
	capfs_magic_t magic;
	capfs_seq_t seq; /* matching sequence number, or NO_SEQUENCE_NR */
	capfs_type_t type;
	int64_t       total_time;
	capfs_error_t error; /* 0 on success, negative for error performing op */
	union {
		struct {
			/* empty */
		} null;
		struct {
			struct capfs_meta meta;
			struct capfs_phys phys;
		} getmeta;
		struct {
			struct capfs_meta meta;
			struct capfs_phys phys;
		} setmeta;
		struct {
			struct capfs_meta meta; /* includes handle */
			/* client responds with the cons_id on a mount operation that can be hooked onto the superblock */
			int32_t	cons;
		} lookup;
		struct {
			/* not implemented in v1.x */
		} rlookup;
		struct {
			/* empty */
		} readlink;
		struct {
			/* empty */
		} create;
		struct {
			/* empty */
		} remove;
		struct {
			/* empty */
		} rename;
		struct {
		   /* empty */
		} symlink;
		struct {
			/* empty */
		} mkdir;
		struct {
			/* empty */
		} rmdir;
		struct {
			capfs_boolean_t eof;
			capfs_off_t off; /* new offset, after operation */
			capfs_count_t count;
		} getdents;
		struct {
			struct capfs_statfs statfs;
		} statfs;
		struct {
			/* not implemented in v1.x */
		} export;
		struct {
			/* not implemented in v1.x */
		} mkdirent;
		struct {
			/* not implemented in v1.x */
		} rmdirent;
		struct {
			/* not implemented in v1.x */
		} fhdump;
		struct {
			struct capfs_stats stats;
		} hint;
		struct {
			capfs_boolean_t eof; /* non-zero when trying to read past eof */
			capfs_size_t size; /* size of data read, in bytes */
		} rw; /* read and write both handled here */
	} u;
	struct {
		void *ptr;
		/* this is the actual size that the operation actually did */
		capfs_size_t size;
		/* original size */
		capfs_size_t orig_size;
		/* Just copy back the boolean field from the upcall also */
		capfs_boolean_t from_kmem;
		/* subtle: We also need a field to tell kcapfsd whether to free the pointer or not */
		capfs_boolean_t to_free_ptr;
	} xfer; /* implementation-specific data transfer elements */
};

#ifdef __KERNEL__
/* low level interface prototypes */
int ll_capfs_rename(struct capfs_inode *old_inode, const capfs_char_t *new_name, capfs_size_t len);
int ll_capfs_lookup(struct capfs_sb_info *sb, const capfs_char_t *name,
capfs_size_t len, struct capfs_meta *meta, int special_op);
int ll_capfs_link(int use_tcp, int cons,
		const capfs_char_t *link_name, capfs_size_t len,
		struct capfs_inode *old_inode, struct capfs_meta *meta);
int ll_capfs_symlink(int use_tcp, int cons,
		const capfs_char_t *symlink_name, capfs_size_t symlink_len,
		const capfs_char_t* name, capfs_size_t len, struct capfs_meta *meta);
int ll_capfs_readlink(struct capfs_inode *new_inode, char*, unsigned int, unsigned int);
int ll_capfs_mkdir(struct capfs_sb_info *sb, const capfs_char_t *name,
	capfs_size_t len, struct capfs_meta *meta);
int ll_capfs_create(struct capfs_sb_info *sb, const capfs_char_t *name,
	capfs_size_t len, struct capfs_meta *meta, struct capfs_phys *phys);
int ll_capfs_setmeta(struct capfs_inode *inode, struct capfs_meta *meta,
	capfs_uid_t caller_uid, capfs_gid_t caller_gid);
int ll_capfs_getmeta(struct capfs_inode *inode, struct capfs_meta *meta,
	struct capfs_phys *phys);
int ll_capfs_statfs(struct capfs_sb_info *sb, struct capfs_statfs *sbuf);
int ll_capfs_readdir(struct capfs_inode *inode, struct capfs_dirent *dirent,
	capfs_off_t *offp, int dir_count);
int ll_capfs_rmdir(struct capfs_inode *inode);
int ll_capfs_unlink(struct capfs_inode *inode);
int ll_capfs_file_read(struct capfs_inode *inode, capfs_char_t *buf,
	capfs_size_t count, capfs_off_t *offp, capfs_boolean_t to_kmem);
int ll_capfs_file_write(struct capfs_inode *inode, capfs_char_t *buf,
	capfs_size_t count, capfs_off_t *offp, capfs_boolean_t to_kmem);
int ll_capfs_fsync(struct capfs_inode *inode);
int ll_capfs_hint(struct capfs_inode *inode, capfs_type_t hint, struct capfs_stats *);

#endif

struct capfs_invalidate_ioctl {
	int type;
	union {
		struct {
			int len;
			char *name;
		} byname;
		struct {
			int64_t ino;
		} byid;
	} u;
};


#ifdef __KERNEL__
/* from linux/fs/namei.c */
static __inline__ int check_sticky(struct inode *dir, struct inode *inode)
{
        if (!(dir->i_mode & S_ISVTX))
                return 0;
        if (inode->i_uid == current->fsuid)
                return 0;
        if (dir->i_uid == current->fsuid)
                return 0;
        return !capable(CAP_FOWNER);
}

/* from linux/fs/namei.c */
static __inline__ int may_delete(struct inode *dir,struct dentry *victim, int isdir)
{
        int error;
        if (!victim->d_inode || victim->d_parent->d_inode != dir)
                return -ENOENT;
        error = permission(dir,MAY_WRITE | MAY_EXEC, NULL);
        if (error)
                return error;
        if (IS_APPEND(dir))
                return -EPERM;
        if (check_sticky(dir, victim->d_inode)||IS_APPEND(victim->d_inode)||
            IS_IMMUTABLE(victim->d_inode))
                return -EPERM;
        if (isdir) {
                if (!S_ISDIR(victim->d_inode->i_mode))
                        return -ENOTDIR;
                if (IS_ROOT(victim))
                        return -EBUSY;
        } else if (S_ISDIR(victim->d_inode->i_mode))
                return -EISDIR;
		  if (IS_DEADDIR(dir))
				return -ENOENT;
        return 0;
}

/* from linux/fs/namei.c */
static __inline__ int may_create(struct inode *dir, struct dentry *child,
		struct nameidata *nd) 
{
        if (child->d_inode)
                return -EEXIST;
        if (IS_DEADDIR(dir))
                return -ENOENT;
        return permission(dir,MAY_WRITE | MAY_EXEC, nd);
}
#endif



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



