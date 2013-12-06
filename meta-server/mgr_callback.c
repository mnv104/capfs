/*
 * Copyright (C) Murali Vilayannur
 * vilayann@cse.psu.edu
 * Keeps track of every file's callback identifier/sharer sets.
 */
#include "mgr_prot.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <rpc/rpc.h>
#include <rpc/clnt.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include "bit.h" /* Really inefficient but portable bit-vector implementation */
#include "mquickhash.h"
#include "quicklist.h"
#include "log.h"

/* Minimum and maximum values a `signed long long int' can hold.  */
#ifndef LLONG_MAX
#define LLONG_MAX						9223372036854775807LL
#endif

#ifndef LLONG_MIN
#define LLONG_MIN						(-LLONG_MAX - 1LL)
#endif

struct File {
	int64_t f_ino;
	int64_t fs_ino;
	char    *fname;
};

/*
 * A counter is associated with each callback_entry object to prevent objects from being deleted
 * if any one else holds a reference to that object!
 */
struct callback_entry {
	struct File 		cb_file;
	int	  				cb_fix, cb_nwaiters;
	/* FIXME: This will mean that we can have only upto 32 sharers per file */
	unsigned long 		cb_bitmap;
	struct qlist_head cb_hash;
	pthread_mutex_t  	*cb_mutex;
	pthread_cond_t   	*cb_cv;
};

static struct mqhash_table *cb_hash_table = NULL;

#define CHAIN_READ  0
#define CHAIN_WRITE 1

enum cb_mode {ADD_CB = 0, DEL_CB = 1, LOOKUP_CB = 2, RESET_CB = 3, MAX_CBS = 4};

struct cb_options {
	enum cb_mode mode;
	union {
		int 	add_cb_id;
		int   del_cb_id;
		unsigned long lookup_bitmap;
		unsigned long reset_bitmap;
	} u;
	char *fname;
};

static inline int check_sanity(struct cb_options *options)
{
	if (options == NULL) {
		return -EINVAL;
	}
	if (options->mode < 0 || options->mode >= MAX_CBS) {
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "mode [%d] is invalid.\n", options->mode);
		return -EINVAL;
	}
	if (options->mode == ADD_CB) {
		if (options->u.add_cb_id < 0 || options->u.add_cb_id >= BITS_PER_LONG) {
			LOG(stderr, DEBUG_MSG, SUBSYS_META, "Add cb_id [%d] cannot be outside the range [ 0 - %d ]\n", options->u.add_cb_id, BITS_PER_LONG);
			return -EINVAL;
		}
	}
	else if (options->mode == DEL_CB) {
		if (options->u.del_cb_id < 0 || options->u.del_cb_id >= BITS_PER_LONG) {
			LOG(stderr,DEBUG_MSG, SUBSYS_META, "Del cb_id [%d] cannot be outside the range [ 0 - %d]\n", options->u.del_cb_id, BITS_PER_LONG);
			return -EINVAL;
		}
	}
	return 0;
}

/*
 * Tries to lookup the object in the cache, and if it finds it, returns it
 * else adds it atomically.
 * Also tries to add the sharer to the bitmap.
 */
static int get_object(struct File* ref, struct cb_options *options, int *error) 
{
	struct mqhash_head *entry = NULL;
	struct callback_entry *o = NULL;
	int hindex = 0, mode = CHAIN_READ; 

	if (check_sanity(options) < 0) {
		if (error) {
			*error = -EINVAL;
		}
		return 0;
	}
	if (error)
	{
		*error = 0;
	}
	hindex = cb_hash_table->hash(ref, cb_hash_table->table_size);
retry:
	if (mode == CHAIN_READ) 
	{
		/* hold on to the readlock of the hash chain */
		mqhash_rdlock(&cb_hash_table->lock[hindex]);
	}
	else if (mode == CHAIN_WRITE)
	{
		/* hold on to the writelock of the hash chain */
		mqhash_wrlock(&cb_hash_table->lock[hindex]);
	}
	mqhash_for_each (entry, &(cb_hash_table->array[hindex]))
	{
		if (cb_hash_table->compare(ref, entry)) /* matches */
		{
			o = qlist_entry(entry, struct callback_entry, cb_hash);
			assert(o->cb_file.f_ino == ref->f_ino
				&& o->cb_file.fs_ino == ref->fs_ino);
			break;
		}
	}
	/* did we miss in the cache? */
	if (!o)
	{
		if (mode == CHAIN_READ)
		{
			/* reissue the search with mode set to CHAIN_WRITE only if we are adding objects to the cache */
			if (options->mode == ADD_CB)
			{
				mode = CHAIN_WRITE;
				/* unlock the hash chain */
				mqhash_unlock(&cb_hash_table->lock[hindex]);
				goto retry;
			}
			else
			{
				/* object is not even there in cache. no need to do anything further */
				options->fname = NULL;
				goto unlock_chain;
			}
		}
		else if (mode == CHAIN_WRITE)
		{
			o = (struct callback_entry *)calloc(1, sizeof(struct callback_entry));
			if (o == NULL)
			{
				if (error)
				{
					*error = -ENOMEM;
				}
				mqhash_unlock(&cb_hash_table->lock[hindex]);
				return 0;
			}
			o->cb_file.fs_ino = ref->fs_ino;
			o->cb_file.f_ino = ref->f_ino;
			o->cb_file.fname = strdup(options->fname);
			if (o->cb_file.fname == NULL)
			{
				free(o);
				if (error)
				{
					*error = -ENOMEM;
				}
				mqhash_unlock(&cb_hash_table->lock[hindex]);
				return 0;
			}
			o->cb_fix = 0;
			o->cb_nwaiters = 0;
			o->cb_mutex = NULL;
			o->cb_cv = NULL;
			/* Initial bitmap is all zeroes */
			o->cb_bitmap = 0;
			mqhash_add(cb_hash_table, ref, &o->cb_hash);
		}
	}
	o->cb_fix++;
	/* Add this callback id to the sharer bitmap */
	if (options->mode == ADD_CB) {
		SetBit((unsigned char *) &o->cb_bitmap, sizeof(o->cb_bitmap), options->u.add_cb_id);
	}
	else if (options->mode == DEL_CB) {
		ClearBit((unsigned char *) &o->cb_bitmap, sizeof(o->cb_bitmap), options->u.del_cb_id);
		options->fname = o->cb_file.fname;
	}
	else if (options->mode == LOOKUP_CB) {
		options->u.lookup_bitmap = o->cb_bitmap;
		options->fname = o->cb_file.fname;
	}
	else if (options->mode == RESET_CB) {
		options->u.reset_bitmap = o->cb_bitmap;
		o->cb_bitmap = 0;
	}
unlock_chain:
	mqhash_unlock(&cb_hash_table->lock[hindex]);
	return 0;
}

static inline void lock_object(struct callback_entry *o)
{
    if (o && o->cb_mutex)
    {
		 pthread_mutex_lock(o->cb_mutex);
    }
}

static inline void unlock_object(struct callback_entry *o)
{
	if (o && o->cb_mutex) {
		pthread_mutex_unlock(o->cb_mutex);
	}
}

static void put_object(struct File *ref)
{
	struct qlist_head *entry = NULL;
	struct callback_entry *o = NULL;
	int hindex = 0;

	hindex = cb_hash_table->hash(ref, cb_hash_table->table_size);
	mqhash_wrlock(&cb_hash_table->lock[hindex]);
	mqhash_for_each (entry, &cb_hash_table->array[hindex])
	{
		if (cb_hash_table->compare(ref, entry))
		{
			o = (struct callback_entry *) qlist_entry(entry, struct callback_entry, cb_hash);
			o->cb_fix--;
			if (o->cb_fix < 0)
			    o->cb_fix = 0;
			/* if some thread is waiting for this object to be deleted. wake it up */
			if (o->cb_fix == 0 && o->cb_mutex && o->cb_cv) 
			{
				lock_object(o);
				pthread_cond_broadcast(o->cb_cv);
				unlock_object(o);
				mqhash_unlock(&cb_hash_table->lock[hindex]);
				return;
			}
			break;
		}
	}
	mqhash_unlock(&cb_hash_table->lock[hindex]);
	return;
}

/*
 * Given an object identifier, we hunt for the object in the cache,
 * and if we find it, then we try to deallocate it from the cache.
 * However, we must make sure that noone else holds a reference
 * to the object and if they do we block until everyone else drops
 * their reference to this object. Return values are
 * a) -1 if we could not locate the object in the cache or on error.
 * b) 0 if we could locate it, but there were multiple deleters
 *    of this object and the last deleter would then deallocate this object.
 * c) 1 if we could locate it and deallocate it.
 * This routine could be called by unlink() kind of functions
 * or an fsync() function where any cached meta-data needs to be revalidated.
 * Returns the callback sharer bitmap at the time of the delete.
 */
unsigned long del_object(struct File *ref, int *error)
{
	struct qlist_head *entry = NULL;
	struct callback_entry *o = NULL;
	int hindex = 0;
	unsigned long bitmap = 0;

	if (error)
	{
		*error = 0;
	}
	hindex = cb_hash_table->hash(ref, cb_hash_table->table_size);
retry:
	/* Acquire a writelock on the hash chain */
	mqhash_wrlock(&cb_hash_table->lock[hindex]);
	mqhash_for_each (entry, &cb_hash_table->array[hindex])
	{
		/* Whoa! we hit in the object cache */
		if (cb_hash_table->compare(ref, entry))
		{
			o = (struct callback_entry *) qlist_entry(entry, struct callback_entry, cb_hash);
			/* can we delete this object right away? */
			if (o->cb_fix == 0) 
			{
				/* no other thread is waiting on the object's cb_cv yet */
				if (o->cb_nwaiters == 0)
				{
					/* delete it from the hash chain */
					qlist_del(&o->cb_hash);
					/* free up the object */
					if (o->cb_mutex)
					    free(o->cb_mutex);
					if (o->cb_cv)
					    free(o->cb_cv);
					o->cb_mutex = NULL;
					o->cb_cv = NULL;
					if (o->cb_file.fname)
						free(o->cb_file.fname);
					bitmap = o->cb_bitmap;
					free(o);
				}
				else
				{
				    bitmap = o->cb_bitmap;
				}
				break;
			}
			/* allocate the cb_mutex & cb_cv variables */
			if (o->cb_mutex == NULL) 
			{
				o->cb_mutex = (pthread_mutex_t *)calloc(1, sizeof(pthread_mutex_t));
				if (o->cb_mutex == NULL)
				{
					if (error)
					{
						*error = -ENOMEM;
					}
					break;
				}
				pthread_mutex_init(o->cb_mutex, NULL);
			}
			if (o->cb_cv == NULL)
			{
				o->cb_cv = (pthread_cond_t *)calloc(1, sizeof(pthread_cond_t));
				if (o->cb_cv == NULL)
				{
					if (error)
					{
						*error = -ENOMEM;
					}
					break;
				}
				pthread_cond_init(o->cb_cv, NULL);
			}
			lock_object(o);
			o->cb_nwaiters++;
			/* drop the hash table lock, since the put() function needs that lock to set decrement cb_fix */
			mqhash_unlock(&cb_hash_table->lock[hindex]);
			/* wait for the cb_fix counter to drop to 0 */
			while (o->cb_fix != 0) 
			{
				pthread_cond_wait(o->cb_cv, o->cb_mutex);
			}
			o->cb_nwaiters--;
			unlock_object(o);
			goto retry;
		}
	}
	mqhash_unlock(&cb_hash_table->lock[hindex]);
	return bitmap;
}

int add_callbacks(int64_t fs_ino, int64_t f_ino, char *fname, int cb_id)
{
	struct File f;
	int error = 0;
	struct cb_options options;

	if (fname == NULL)
	{
		return -1;
	}
	f.fs_ino = fs_ino;
	f.f_ino = f_ino;
	options.mode = ADD_CB;
	options.u.add_cb_id = cb_id;
	options.fname = fname;
	get_object(&f, &options, &error);
	if (error < 0) {
		return error;
	}
	put_object(&f);
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "add_callbacks called for %s callback id is %d\n", fname, cb_id);
	return 0;
}

int del_callbacks(int64_t fs_ino, int64_t f_ino, int cb_id)
{
	struct File f;
	int error = 0;
	struct cb_options options;

	f.fs_ino = fs_ino;
	f.f_ino = f_ino;
	options.mode = DEL_CB;
	options.u.del_cb_id = cb_id;
	get_object(&f, &options, &error);
	if (error < 0) {
		return error;
	}
	put_object(&f);
	if (options.fname) {
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "del_callbacks called for %s callback id is %d\n", options.fname, cb_id);
	}
	return 0;
}

long get_callbacks(int64_t fs_ino, int64_t f_ino, char **fname)
{
	struct File f;
	int error = 0;
	struct cb_options options;

	if (fname == NULL)
	{
		return -1;
	}
	f.fs_ino = fs_ino;
	f.f_ino = f_ino;
	options.mode = LOOKUP_CB;
	get_object(&f, &options, &error);
	if (error < 0) {
		return error;
	}
	put_object(&f);
	*fname = options.fname;
	return options.u.lookup_bitmap;
}

unsigned long clear_callbacks(int64_t fs_ino, int64_t f_ino)
{
	struct File f;
	int error;
	unsigned long bitmap;

	f.fs_ino = fs_ino;
	f.f_ino = f_ino;
	bitmap = del_object(&f, &error);
	return bitmap;
}

static int file_compare(void *key, struct mqhash_head *link)
{
	struct callback_entry *entry = qlist_entry(link, struct callback_entry, cb_hash);
	struct File *filp = (struct File *) key;

	/* Matches! */
	if (entry->cb_file.f_ino == filp->f_ino
			&& entry->cb_file.fs_ino == filp->fs_ino) {
		return 1;
	}
	return 0;
}

static int file_hash(void *key, int table_size)
{
	struct File *f1 = (struct File *) key;
	int hash_value = 0;

	hash_value = abs(hash2(f1->f_ino, f1->fs_ino)) % table_size;
	return hash_value;
}

int cb_init(void)
{
	if ((cb_hash_table = 
				mqhash_init(file_compare, file_hash, 101)) == NULL) 
	{
		return -1;
	}
	return 0;
}

void cb_finalize(void)
{
	mqhash_finalize(cb_hash_table);
}
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 *
 * vim: ts=3
 */
