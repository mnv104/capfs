/*
 * (C) 2005 Penn State University 
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#ifndef META_H
#define META_H

/* includes for struct stat */
#include <sys/stat.h>
#include <unistd.h>

/* includes for int64_t on some systems */
#include <stdint.h>

/* includes for sockaddr_in */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <capfs_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dmeta dmeta, *dmeta_p;

struct dmeta {
	int64_t fs_ino; /* file system root directory inode # */
	uid_t dr_uid; /* directory owner uid # */
	gid_t dr_gid; /* directory owner gid # */
	mode_t dr_mode; /* directory mode # */
	u_int16_t port;
	char *host;
	char *rd_path;
};

typedef struct capfs_filestat capfs_filestat, *capfs_filestat_p;

/*
 * Just the useful fields from struct stat, rounded up in size to fit
 * all architectures.
 *
 * glibc-2.3.2 introduces backwards compatibility macros for st_[amc]time,
 * which means we can't use the names from struct stat
 */
struct capfs_stat {
    uint64_t st_size;
    uint64_t st_ino;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad;    /* round up to 64-bit fields */
    /* unused standard stat fields: dev, nlink, rdev, blksize, blocks */
};

/* use this when copying FROM a stat or stat64 TO a stat or stat64 struct */
#define COPY_STAT_TO_STAT(dest, src) do { \
	memset(dest, 0, sizeof(*(dest))); \
	(dest)->st_size = (src)->st_size; \
	(dest)->st_ino = (src)->st_ino; \
	(dest)->st_atime = (src)->st_atime; \
	(dest)->st_mtime = (src)->st_mtime; \
	(dest)->st_ctime = (src)->st_ctime; \
	(dest)->st_mode = (src)->st_mode; \
	(dest)->st_uid = (src)->st_uid; \
	(dest)->st_gid = (src)->st_gid; \
    } while (0);

/* use this when copying FROM a capfs_stat TO a stat or stat64 struct */
#define COPY_PSTAT_TO_STAT(dest, src) do { \
	memset(dest, 0, sizeof(*(dest))); \
	(dest)->st_size = (src)->st_size; \
	(dest)->st_ino = (src)->st_ino; \
	(dest)->st_atime = (src)->atime; \
	(dest)->st_mtime = (src)->mtime; \
	(dest)->st_ctime = (src)->ctime; \
	(dest)->st_mode = (src)->st_mode; \
	(dest)->st_uid = (src)->st_uid; \
	(dest)->st_gid = (src)->st_gid; \
    } while (0);

/* use this when copying FROM a stat or stat64 struct TO a capfs_stat struct */
#define COPY_STAT_TO_PSTAT(dest, src) do { \
	memset(dest, 0, sizeof(*(dest))); \
	(dest)->st_size = (src)->st_size; \
	(dest)->st_ino = (src)->st_ino; \
	(dest)->atime = (src)->st_atime; \
	(dest)->mtime = (src)->st_mtime; \
	(dest)->ctime = (src)->st_ctime; \
	(dest)->st_mode = (src)->st_mode; \
	(dest)->st_uid = (src)->st_uid; \
	(dest)->st_gid = (src)->st_gid; \
    } while (0);


struct capfs_filestat {
	int32_t base;
	int32_t pcount;
	int32_t ssize;
	int32_t __pad;  /* round up to 64-bit fields */
};

typedef struct fmeta fmeta, *fmeta_p;

/* this is the structure we're actually using */
struct fmeta {
	int64_t fs_ino; /* file system root directory inode # */
	struct capfs_stat u_stat;
	struct capfs_filestat p_stat;
	struct sockaddr mgr;
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
