/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/*
 * FLIST.H - header for flist.c, the manager file list manipulation
 * routines
 */

#ifndef FLIST_H
#define FLIST_H

#include <llist.h>
#include <meta.h>
#include <dfd_set.h>
#include <pthread.h>

typedef struct flist flist, *flist_p;

struct flist {
	pthread_rwlock_t lock;
	llist_p list;
};

typedef struct finfo finfo, *finfo_p;

struct finfo {
	pthread_rwlock_t lock; /* lock for serializing write commits */
	int unlinked;     /* fd of metadata file or -1 */
	ino_t f_ino;      /* inode # of metadata file */
	capfs_filestat p_stat; /* CAPFS metadata for file */
	int cap;          /* max. capability assigned thusfar */
	int cnt;          /* count of # of times file is open */
	char *f_name;     /* file name - for performing ops on metadata */
	dyn_fdset socks;  /* used to track what FDs have opened the file */
	int64_t utime_modtime; /* last explicitly set modtime */
	int64_t utime_event; /* used to track when a utime op. was performed */
};

flist_p flist_new(void);
int flist_empty(flist_p);
int f_add(flist_p, finfo_p);
finfo_p f_search(flist_p, ino_t);
finfo_p f_new(void);
void f_wrlock(finfo_p f_p);
void f_rdlock(finfo_p f_p);
void f_unlock(finfo_p f_p);
int f_rem(flist_p, ino_t);
void flist_cleanup(flist_p);
int f_dump(void *);
int forall_finfo(flist_p, int (*fn)(void *));
void f_free(void *f_p);
int flist_dump(flist_p fl_p);
void flist_wrlock(flist_p fl_p);
void flist_rdlock(flist_p fl_p);
void flist_unlock(flist_p fl_p);
int flist_tryrdlock(flist_p fl_p);
int flist_trywrlock(flist_p fl_p);


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
