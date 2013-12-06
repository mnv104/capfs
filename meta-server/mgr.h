/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#ifndef MGR_H
#define MGR_H

/* INCLUDES */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <fcntl.h>
#include <syscall.h>
#include <sys/syscall.h>
#include <utime.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
//#include <sys/statfs.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <syslog.h>
#include <grp.h>
#include <pwd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <malloc.h>

#include <desc.h> /* has iod_info structure */
#include <meta.h> /* has capfs_filestat structure */
#include <sockio.h>
#include <metaio.h>
#include <capfs_types.h>
#include <sockset.h>
#include <fslist.h>
#include <flist.h>
#include <iodtab.h>
#include <req.h>
#include <minmax.h>
#include <capfs_config.h>
#include <sys/uio.h>

#define NOFOLLOW_LINK 0
#define FOLLOW_LINK   1


struct ackdata {
	int type;
	union {
		struct {
			/* OUT parameters */
			int       niods;
			iod_info  **iod;
			int64_t	 nhashes;
			unsigned char		 *hashes;
		} open;
		struct {
			/* OUT parameters */
			char *link_name;
		} readlink;
		struct {
			/* OUT parameters */
			int       niods;
			iod_info *iod;
		} iodinfo;
		struct {
			/* OUT parameters */
			int nentries;
			struct capfs_dirent *pdir;
		} getdents;
		struct {
			/* OUT parameters */
			int64_t nhashes;
			unsigned char *hashes;
		} gethashes;
		struct {
			/* IN parameter */
			int64_t 			   old_hash_len;
			unsigned char   **old_hashes;
			int64_t 				new_hash_len;
			unsigned char   **new_hashes;
			int				  force_commit;
			/* needed for hcache coherence */
			int      		  desire_hcache_coherence;
			int				  owner_cbid;
			/* OUT parameter */
			int64_t current_hash_len;
			unsigned char   *current_hashes;
		} wcommit;
		struct {
			/* OUT parameters */
			int64_t old_length;
			int64_t fs_ino;
			int64_t f_ino;
			int64_t begin_chunk;
			int64_t nchunks;
		} truncate;
		struct {
			/* OUT parameter */
			int64_t fs_ino;
			int64_t f_ino;
		} unlink;
	} u;
};

/* Analogous thing on the client-side */
struct ackdata_c {
	int type;
	union {
		struct {
			/* niods is an in-out parameter */
			int       niods;
			iod_info  *iod;
			/* nhashes is an in-out sort of parameter */
			int64_t	 nhashes;
			unsigned char		 *hashes;
		} open;
		struct {
			/* nentries is an in-out sort of parameter */
			int		nentries;
			struct capfs_dirent *pdir;
		} getdents;
		struct {
			/* these are filled and returned */
			int 		niods;
			iod_info *pinfo;
		} iodinfo;
		struct {
			int		link_len;
			char 		*link_name;
		} readlink;
		struct {
			/* these are filled and returned */
			int nhashes;
			unsigned char *buf;
		} gethashes;
	} u;
};

typedef struct {
	int sha1_info_len;
	unsigned char **sha1_info_ptr;
}sha1_info;

struct capfs_options {
	/* What protocol are we using? */
	int tcp;
	/* Are we using the hcache at all? */
	int use_hcache;
	/* Do we desire the hcache to be coherent? */
	int desire_hcache_coherence;
	/* Do we require that commits be forced or retried in case of conflicts? */
	int force_commit;
	/* Do we require that commits be delayed? */
	int delay_commit;
};

/* callback registration (client) */
extern int cb_init(void);
extern void cb_finalize(void);
extern int add_callbacks(int64_t fs_ino, int64_t f_ino, char*, int cb_id);
extern int del_callbacks(int64_t fs_ino, int64_t f_ino, int cb_id);
extern unsigned long get_callbacks(int64_t fs_ino, int64_t f_ino, char **);
extern unsigned long clear_callbacks(int64_t fs_ino, int64_t f_ino);

extern int capfs_cbreg(struct capfs_options* , struct sockaddr *mgr_host, int prog, int vers, int proto);
extern int commit_write(struct capfs_options*, char *fname, int64_t begin_chunk, int64_t nchunks,
		sha1_info *old_hashes, sha1_info *new_hashes, sha1_info *current_hashes);

/* Compat routines (server) */
extern int process_compat_req(mreq *req, mack *ack, char *buf_p, struct ackdata *ackdata_p);
/* Compat routines (client) */
extern int encode_compat_req(struct capfs_options *, struct sockaddr* mgr, mreq *req, 
		mack *ack, char *buf_p, struct ackdata_c *recv_p);

extern void cb_update_hashes(char *fname, int cb_id, int64_t begin_chunk, int64_t nchunks, char *phashes);
extern void cb_invalidate_hashes(char *, unsigned long bitmap, int owner, int64_t, int64_t);
extern void cb_clear_hashes(char *, unsigned long bitmap, int owner);
#endif

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * vim: ts=3
 * End:
 */ 

