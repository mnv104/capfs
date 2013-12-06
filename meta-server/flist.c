/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/*
 * FLIST.C - functions to handle the creation and modification of 
 * lists of file information
 *
 * Note: a SINGLE entry is added to the list for an open file, 
 * no matter how many times the file has been opened.
 *
 * This is built on top of the llist.[ch] files
 *
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <flist.h>
#include <dfd_set.h>
#include <log.h>
#include <pthread.h>

/* prototypes for internal functions */
static int f_ino_cmp(void *key, void *f_p);

flist_p flist_new(void)
{
	flist_p fl;
	fl = (flist_p) malloc(sizeof(flist));
	if (fl) {
		pthread_rwlock_init(&fl->lock, NULL);
		fl->list = llist_new();
		if (fl->list == NULL) {
			free(fl);
			return NULL;
		}
	}
	return fl;
}

void flist_wrlock(flist_p fl_p)
{
	pthread_rwlock_wrlock(&fl_p->lock);
	return;
}

void flist_rdlock(flist_p fl_p)
{
	pthread_rwlock_rdlock(&fl_p->lock);
	return;
}

void flist_unlock(flist_p fl_p)
{
	pthread_rwlock_unlock(&fl_p->lock);
	return;
}

int flist_tryrdlock(flist_p fl_p)
{
	return pthread_rwlock_tryrdlock(&fl_p->lock);
}

int flist_trywrlock(flist_p fl_p)
{
	return pthread_rwlock_trywrlock(&fl_p->lock);
}

int flist_empty(flist_p fl_p)
{
	int empty;

	flist_rdlock(fl_p);
	empty = llist_empty(fl_p->list);
	flist_unlock(fl_p);
	return empty;
}

int f_add(flist_p fl_p, finfo_p f_p)
{
	int ret;

	flist_wrlock(fl_p);
	ret = llist_add(fl_p->list, (void *) f_p);
	flist_unlock(fl_p);
	return ret;
}

finfo_p f_search(flist_p fl_p, ino_t f_ino)
{
	finfo_p f_p;

	flist_rdlock(fl_p);
	f_p = ((finfo_p) llist_search(fl_p->list, (void *) (&f_ino), f_ino_cmp));
	flist_unlock(fl_p);
	return f_p;
}

int f_rem(flist_p fl_p, ino_t f_ino)
{
	finfo_p f_p;
	
	flist_wrlock(fl_p);
	f_p = (finfo_p) llist_rem(fl_p->list, (void *) (&f_ino), f_ino_cmp);
	if (f_p) {
		f_free(f_p);
		flist_unlock(fl_p);
		return(0);
	}
	flist_unlock(fl_p);
	return(-1);
}

void flist_cleanup(flist_p fl_p)
{
	llist_free(fl_p->list, f_free);
}

static int f_ino_cmp(void *key, void *f_p)
{
	if ((ino_t)(*(ino_t *) key) == ((finfo_p)f_p)->f_ino) return(0);
	else return(1);
}

finfo_p f_new(void) 
{
	finfo_p f_p;

	if (!(f_p = (finfo_p) malloc(sizeof(finfo)))) 
		return(0);
	memset(f_p, 0, sizeof(finfo));
	pthread_rwlock_init(&f_p->lock, NULL);
	f_p->cap = 0;
	f_p->cnt = 1;
	f_p->f_name = 0;
	f_p->unlinked = -1;
	f_p->utime_event = 0;
	dfd_init(&f_p->socks, 1);
	return(f_p);
}

void f_wrlock(finfo_p f_p)
{
	if (f_p) {
		if (pthread_rwlock_trywrlock(&f_p->lock) != 0) {
			LOG(stderr, DEBUG_MSG, SUBSYS_META, "[WRLOCK] Thread %lu about to block %p\n", pthread_self(), &f_p->lock);
			pthread_rwlock_wrlock(&f_p->lock);
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "[WRLOCK] Thread %lu acquired %p\n", pthread_self(), &f_p->lock);
	}
	return;
}

void f_rdlock(finfo_p f_p)
{
	if (f_p) {
		if (pthread_rwlock_tryrdlock(&f_p->lock) != 0) {
			LOG(stderr, DEBUG_MSG, SUBSYS_META, " [RDLOCK] Thread %lu about to block %p\n", pthread_self(), &f_p->lock);
			pthread_rwlock_rdlock(&f_p->lock);
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "[RDLOCK] Thread %lu acquired %p\n", pthread_self(), &f_p->lock);
	}
}

void f_unlock(finfo_p f_p)
{
	if (f_p) {
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "[UNLOCK] Thread %lu unlocked %p\n", pthread_self(), &f_p->lock);
		pthread_rwlock_unlock(&f_p->lock);
	}
}

void f_free(void *f_p)
{
	dfd_finalize(&((finfo_p)f_p)->socks);
	free(((finfo_p)f_p)->f_name);
	free((finfo_p)f_p);
}

int flist_dump(flist_p fl_p)
{
	int ret;

	ret = llist_doall(fl_p->list, f_dump);
	return ret;
}

int f_dump(void *v_p)
{
	finfo_p f_p = (finfo_p) v_p; /* quick cast */

	LOG(stderr, DEBUG_MSG, SUBSYS_META, "i %Ld, b %d, p %d, s %d, n %d, %s\n",
		(long long)f_p->f_ino, (int)f_p->p_stat.base, (int)f_p->p_stat.pcount,
		(int)f_p->p_stat.ssize, (int)f_p->cnt, f_p->f_name);
	return(0);
}

int forall_finfo(flist_p fl_p, int (*fn)(void *))
{
	int ret;

	ret = llist_doall(fl_p->list, fn);
	return ret;
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
