/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#ifndef FSLIST_H
#define FSLIST_H

#include <llist.h>
#include <desc.h>
#include <sys/stat.h>
#include <unistd.h>
#include <flist.h>
#include <pthread.h>

typedef struct fslist fslist, *fslist_p;

struct fslist {
	pthread_rwlock_t lock;
	llist_p list;
};

typedef struct fsinfo fsinfo, *fsinfo_p;

struct fsinfo {
	ino_t fs_ino;    /* inode # of root directory for this filesystem */
	int nr_iods;     /* # of iods for this filesystem */
	flist_p fl_p;    /* list of open files for this filesystem */
	iod_info iod[1]; /* list of iod addresses */
};

fslist_p fslist_new(void);
int fs_add(fslist_p, fsinfo_p);
fsinfo_p fs_search(fslist_p, ino_t);
int fs_rem(fslist_p, ino_t);
void fslist_cleanup(fslist_p);
int forall_fs(fslist_p, int (*fn)(void *));
int fs_dump(void *);
int fslist_dump(fslist_p fsl_p);

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
