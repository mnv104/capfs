/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */
/*
 * FSLIST.C - functions to handle the creation and modification of 
 * lists of filesystem information
 *
 * This is built on top of the llist.[ch] files
 *
 */

#include <config.h>
#include <stdlib.h>
#include <fslist.h>
#include <flist.h>
#include <log.h>

/* prototypes for internal functions */
static int fs_ino_cmp(void *key, void *fs_p);
static void fs_free(void * fs_p);

fslist_p fslist_new(void)
{
	fslist_p fsp;
	fsp = (fslist_p) malloc(sizeof(fslist));
	if (fsp) {
		pthread_rwlock_init(&fsp->lock, NULL);
		fsp->list = llist_new();
		if (fsp->list == NULL) {
			free(fsp);
			return NULL;
		}
	}
	return fsp;
}

void fslist_wrlock(fslist_p fs_p)
{
	pthread_rwlock_wrlock(&fs_p->lock);
	return;
}

void fslist_unlock(fslist_p fs_p)
{
	pthread_rwlock_unlock(&fs_p->lock);
}

void fslist_rdlock(fslist_p fs_p)
{
	pthread_rwlock_rdlock(&fs_p->lock);
	return;
}

int fslist_tryrdlock(fslist_p fs_p)
{
	return pthread_rwlock_tryrdlock(&fs_p->lock);
}

int fslist_trywrlock(fslist_p fs_p)
{
	return pthread_rwlock_trywrlock(&fs_p->lock);
}

int fs_add(fslist_p fsl_p, fsinfo_p fs_p)
{
	int ret;

	fslist_wrlock(fsl_p);
	fs_p->fl_p = flist_new(); /* get new file list */
	ret = llist_add(fsl_p->list, (void *) fs_p);
	fslist_unlock(fsl_p);
	return ret;
}

fsinfo_p fs_search(fslist_p fsl_p, ino_t fs_ino)
{
	fsinfo_p fs_p;

	fslist_rdlock(fsl_p);
	fs_p = (fsinfo_p) llist_search(fsl_p->list, (void *) (&fs_ino), fs_ino_cmp);
	fslist_unlock(fsl_p);
	return fs_p;
}

int fs_rem(fslist_p fsl_p, ino_t fs_ino)
{
	fsinfo_p fs_p;
	
	fslist_wrlock(fsl_p);
	fs_p = (fsinfo_p) llist_rem(fsl_p->list, (void *) (&fs_ino), fs_ino_cmp);
	if (fs_p) {
		fs_free(fs_p);
		fslist_unlock(fsl_p);
		return(0);
	}
	fslist_unlock(fsl_p);
	return(-1);
}

int forall_fs(fslist_p fsl_p, int (*fn)(void *))
{
	return(llist_doall(fsl_p->list, fn));
}

int fslist_dump(fslist_p fsl_p)
{
	return(forall_fs(fsl_p, fs_dump));
}

int fs_dump(void *v_p)
{
	fsinfo_p fs_p = (fsinfo_p) v_p;

	LOG(stderr, DEBUG_MSG, SUBSYS_META, "fs_ino: %Ld\nnr_iods: %d\n", fs_p->fs_ino, fs_p->nr_iods);
	flist_dump(fs_p->fl_p);
	return(0);
}

void fslist_cleanup(fslist_p fsl_p)
{
	llist_free(fsl_p->list, fs_free);
}

static int fs_ino_cmp(void *key, void *fs_p)
{
	if (fs_p && (ino_t) (*(ino_t *)key) == ((fsinfo_p) fs_p)->fs_ino) return(0);
	else return(1);
}

static void fs_free(void * fs_p)
{
	flist_cleanup(((fsinfo_p) fs_p)->fl_p);
	free((fsinfo_p) fs_p);
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
