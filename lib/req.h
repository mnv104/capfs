/*
 * (C) 2005 Penn State University (vilayann@cse.psu.edu)
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#ifndef REQ_H
#define REQ_H

#include <meta.h>
#include <utime.h>

#ifdef __cplusplus
extern "C" {
#endif

/* defines for requests to manager */
#define MGR_MAJIK_NR 0x4a87c9fe

#define MGR_CHMOD    0
#define MGR_CHOWN    1
#define MGR_CLOSE    2
#define MGR_LSTAT    3
#define MGR_MOUNT    -1
#define MGR_OPEN     5
#define MGR_UNLINK   6
#define MGR_SHUTDOWN 7
#define MGR_UMOUNT   -1
#define MGR_FSTAT    9
#define MGR_RENAME   10
#define MGR_IOD_INFO 11
#define MGR_MKDIR    12
#define MGR_FCHOWN   13
#define MGR_FCHMOD   14
#define MGR_RMDIR    15
#define MGR_ACCESS   16
#define MGR_TRUNCATE 17
#define MGR_UTIME	   18
#define MGR_GETDENTS 19
#define MGR_STATFS   20
#define MGR_NOOP     21
#define MGR_LOOKUP   22
#define MGR_CTIME    23
#define MGR_LINK     24
#define MGR_READLINK 25
#define MGR_STAT     26
#define MGR_GETHASHES 27
#define MGR_WCOMMIT  28

#define MAX_MGR_REQ  28

/* structure for request to manager */
typedef struct mreq mreq, *mreq_p;

/* NOTE: when adding members to this structure, don't forget to align
 * elements on a 64-bit boundary */
struct mreq {
	int32_t majik_nr;
	int32_t release_nr;
	int8_t type; /* type of request */
	int32_t uid;   /* uid of program making request */
	int32_t gid;   /* gid of program making request */
	int32_t pad;   /* 64-bit machines will naturally align dsize,
	                  ensure interoperability with 32-bit ones */
	int64_t dsize; /* size of data following request */
	union {
		struct {
			fmeta meta; /* metadata for file (when creating?) */
			int32_t flag;
			int32_t mode; /* permissions when creating new file */
			int64_t ackdsize; 
			int32_t need_hashes;
			/*
			   to avoid unnecessary context switches, the sender process
				initializes this field to indicate to the MGR that it is
				waiting for atleast ackdsize bytes. If it is 0, then it is 
				waiting only for the ack from the MGR. So the MGR has to send
				junk data if this field is non-zero and greater than the 
				usual trailer size after the ack. Hopefully this will help
				in improving performance!
		 	*/
		} open;
		struct {
			int32_t force_group_change; /* even if setgid bit was set on parent dir */
			int32_t owner;
			int32_t group;
		} chown;
		struct {
			int32_t mode;
		} chmod;
		struct {
			fmeta meta;
		} close;
		struct {
			fmeta meta;
		} fstat;
		struct {
			int32_t mode;
		} mkdir;
		struct {
			int64_t fs_ino; /* using fs_ino and file_ino instead of meta struct */
			int64_t file_ino;
			int32_t owner;
			int32_t group;
		} fchown;
		struct {
			int64_t fs_ino; /* using fs_ino and file_ino instead of meta struct */
			int64_t file_ino;
			int32_t mode;
		} fchmod;
		struct {
			int32_t mode;
			int32_t to_follow; /* should the access be done on the symlink or the target? */
			/* note that POSIX requires that access is always on the target. Hence the kernel
			 * module should always set this field to 1 if at all it is using this.
			 * However the library will set this depending on the caller!
			 */
		} access;
		struct {
			int64_t length;
		} truncate;
		struct {
			int64_t actime;
			int64_t modtime;
		} utime;
		struct {
			int64_t createtime;
		} ctime;
		struct {
			int64_t offset;
			int64_t length;
		} getdents;
		struct {
			fmeta meta; /* metadata for the link file */
			int32_t soft; /* Do we need a hard or soft link? */
		} link;
#if 0   
		/* the PGI 4.0-2 compiler does not like empty structs */
		struct {
			/* name of the link follows the mreq */
		} readlink;
#endif
		struct {
			int64_t begin_chunk;
			int64_t nchunks;
		} gethashes;
		struct {
			int64_t begin_chunk;
			int64_t write_size;
		} wcommit;
	} req;
};

/* structure for ack from manager */
typedef struct mack mack, *mack_p;

/* NOTE: when adding members to this structure, don't forget to align
 * elements on a 64-bit boundary */
struct mack {
	int32_t majik_nr;
	int32_t release_nr;
	int8_t type;
	int32_t status;
	int32_t eno;
	int32_t pad;  /* 64-bit machine alignment */
	int64_t dsize;
	union {
		struct {
			fmeta meta; /* metadata of opened file */
			int32_t cap;
			int32_t __pad;  /* align 64-bit, largest item */
		} open;
		struct {
			fmeta meta; /* metadata of checked file */
		} access;
		struct {
			fmeta meta; /* metadata of stat'd file */
		} stat;
		struct {
			fmeta meta; /* metadata requested */
		} fstat;	
		struct {
			int32_t nr_iods;
		} iod_info;
		struct {
			int64_t offset;
		} getdents;
		struct {
			fmeta meta;	/* need to return more than just inode and mode */
		} lookup;
		struct {
			int64_t tot_bytes;
			int64_t free_bytes;
			int32_t tot_files;
			int32_t free_files;
			int32_t namelen;
		} statfs;
		struct {
			int64_t nhashes;
			fmeta   meta;
		} gethashes;
		struct {
			int64_t nhashes;
			fmeta   meta;
		} wcommit;
	} ack;
};

/* defines for requests to iod */
#define IOD_MAJIK_NR 0x49e3ac9f
#define IOD_OPEN     0
#define IOD_CLOSE    1
#define IOD_STAT     2
#define IOD_UNLINK   3
#define IOD_RW       4
#define IOD_SHUTDOWN 5

/* defines for group requests */
#define IOD_GSTART   -1
#define IOD_GADD     -1

/* defines for instrumentation */
#define IOD_INSTR		-1

/* more defines for iod requests */
#define IOD_FTRUNCATE 9
#define IOD_TRUNCATE  10
#define IOD_FDATASYNC 11 /* also used for fsync() */

/* a no-op request, for opening a connection w/out a request */
#define IOD_NOOP     12
#define IOD_STATFS   13

/* subtypes for RW requests */
#define IOD_RW_READ  0
#define IOD_RW_WRITE 1

/* defines list io requests */
#define IOD_LIST     14

#define MAX_IOD_REQ  14

/* structure for req to iod */
typedef struct ireq ireq, *ireq_p;

/* NOTE: when adding members to this structure, don't forget to align
 * elements on a 64-bit boundary */
struct ireq {
	int32_t majik_nr;
	int32_t release_nr;
	int8_t type;
	int32_t pad;  /* 64-bit machine alignment */
	int64_t dsize;
	union {
		struct {
			int64_t fs_ino;
			int64_t f_ino; /* inode # of metadata file */
			int32_t cap;
			int32_t flag; /* flags for opening */
			int32_t mode; /* permissions when creating new file */
			capfs_filestat p_stat; /* info on striping of file */
			int32_t pnum; /* partition # for this file */
		} open;
		struct {
			int64_t fs_ino;
			int64_t f_ino;
			int32_t cap;
		} close;
		struct {
			int64_t fs_ino;
			int64_t f_ino;
		} unlink;
		struct {
			int64_t fs_ino;
			int64_t f_ino;
		} stat;
		struct {
			char rw_type;  /* subtype - what kind of access */
			int32_t __pad1;
			int64_t fs_ino;
			int64_t f_ino;   /* file (metadata file) inode # */
			int32_t cap;
			int32_t __pad2;
			int64_t off;       /* initial offset */
			int64_t fsize;     /* first size */
			int64_t gsize;     /* group size */
			int64_t stride;    /* stride */
			int64_t gcount;    /* group count */
			int64_t lsize;     /* last size */
			int64_t gstride;   /* group stride */
			int64_t ngroups;   /* # of groups */
			int32_t c_nr;      /* connection # (for indirect I/O) */
			int32_t __pad3;
		} rw;
		/* listio type */
		struct {
			char rw_type;  /* subtype - what kind of access */
			int32_t __pad1;
			int64_t fs_ino;
			int64_t f_ino;   /* file (metadata file) inode # */
			int32_t cap;    /* has to do with permissions */
			int32_t file_list_count; /* length of previous arrrays */
		} listio;
		struct {
			int64_t fs_ino;
			int64_t f_ino;
			int32_t cap;
			int32_t __pad1;
			int64_t length;
		} ftruncate;
		struct {
			int64_t fs_ino;
			int64_t f_ino;
			capfs_filestat p_stat;
			int64_t length;
			int32_t part_nr;
			int32_t __pad1;
		} truncate;
		struct {
			int64_t fs_ino;
			int64_t f_ino;
			int32_t cap;
			int32_t __pad1;
		} fdatasync;
	} req;
};

typedef struct iack iack, *iack_p;

/* NOTE: when adding members to this structure, don't forget to align
 * elements on a 64-bit boundary */
struct iack {
	int32_t majik_nr;
	int32_t release_nr;
	int8_t type;
	int32_t status;
	int32_t eno;
	int32_t pad;  /* 64-bit machine alignment */
	int64_t dsize;
	union {
		struct {
			int64_t fsize;
			int64_t mtime;
		} stat;
		struct {
			int64_t tot_bytes;
			int64_t free_bytes;
		} statfs;
		struct {
			int64_t fsize;
		} rw;
		struct {
			int64_t modtime;
		} close;
	} ack;
};

#ifdef __cplusplus
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


