/* 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef MQUICKHASH_H
#define MQUICKHASH_H

#include "quicklist.h"
#include "cmgr_constants.h"

extern  FILE			    *output_fp, *error_fp;

#define mqhash_malloc(x)            calloc((x),1)
#define mqhash_free(x)              free(x)
#define mqhash_head                 qlist_head
#define INIT_QHASH_HEAD             INIT_QLIST_HEAD
#define mqhash_entry                qlist_entry
#define mqhash_del		    qlist_del
#define mqhash_add_tail             qlist_add_tail
#define mqhash_for_each             qlist_for_each
#define mqhash_for_each_safe        qlist_for_each_safe

#define mqhash_lock_init(lock_ptr)  pthread_rwlock_init(lock_ptr, NULL)
#define mqhash_rdlock(lock_ptr)     \
    do {\
	int __ret;\
	lock_printf("RD lock on %p\n", lock_ptr);\
	if ((__ret = pthread_rwlock_tryrdlock(lock_ptr)) != 0) {\
	    lock_printf("RD lock on %p is going to block\n",lock_ptr);\
	    pthread_rwlock_rdlock(lock_ptr);\
	}\
    } while (0);
#define mqhash_wrlock(lock_ptr)     \
    do {\
	int __ret;\
	lock_printf("WR lock on %p\n", lock_ptr);\
	if ((__ret = pthread_rwlock_trywrlock(lock_ptr)) != 0) {\
	    lock_printf("WR lock on %p is going to block\n", lock_ptr);\
	    pthread_rwlock_wrlock(lock_ptr);\
	}\
    } while (0);
#define mqhash_unlock(lock_ptr)     \
    do {\
	lock_printf("UN lock on %p\n",lock_ptr);\
	pthread_rwlock_unlock(lock_ptr);\
    } while (0);

struct mqhash_table
{
    struct mqhash_head *array;
    int table_size;
    int (
    *compare) (
    void *key,
    struct mqhash_head * link);
    int (
    *hash) (
    void *key,
    int table_size);
    pthread_rwlock_t *lock;
};

/* mqhash_init()
 *
 * creates a new hash table with the specified table size.  The
 * hash function and compare function must be provided.
 * table_size should be a good prime number.
 *
 * returns pointer to table on success, NULL on failure
 */
static inline struct mqhash_table *mqhash_init(
    int (*compare) (void *key,
		    struct mqhash_head * link),
    int (*hash) (void *key,
		 int table_size),
    int table_size)
{
    int i = 0;
    struct mqhash_table *new_table = NULL;

    /* create struct to contain table information */
    new_table = (struct mqhash_table *)
        mqhash_malloc(sizeof(struct mqhash_table));
    if (new_table == NULL)
    {
	return (NULL);
    }
    /* fill in info */
    new_table->compare = compare;
    new_table->hash = hash;
    new_table->table_size = table_size;

    /* create array for actual table */
    new_table->array = (struct mqhash_head *)
	mqhash_malloc(sizeof(struct mqhash_head) * table_size);
    if (new_table->array == NULL)
    {
	mqhash_free(new_table);
	return (NULL);
    }
    /* create an array of read/write locks for each hash chain */
    new_table->lock = (pthread_rwlock_t *)
	mqhash_malloc(sizeof(pthread_rwlock_t) * table_size);
    if (new_table->lock == NULL)
    {
	mqhash_free(new_table->array);
	mqhash_free(new_table);
	return NULL;
    }
    /* initialize a doubly linked at each hash table index */
    for (i = 0; i < table_size; i++)
    {
	INIT_QHASH_HEAD(&new_table->array[i]);
	mqhash_lock_init(&new_table->lock[i]);
    }

    return (new_table);
}


/* mqhash_finalize()
 *
 * frees any resources created by the hash table
 *
 * no return value
 */
static inline void mqhash_finalize(
    struct mqhash_table *old_table)
{
    mqhash_free(old_table->lock);
    mqhash_free(old_table->array);
    mqhash_free(old_table);
    return;
}

/* mqhash_add()
 *
 * adds a new link onto the hash table, hashes based on given key
 *
 * no return value
 */
static inline void mqhash_add(
    struct mqhash_table *table,
    void *key,
    struct mqhash_head *link)
{
    int index = 0;

    /* hash on the key */
    index = table->hash(key, table->table_size);

    mqhash_add_tail(link, &(table->array[index]));
}

/* mqhash_search()
 *
 * searches for a link in the hash table
 * that matches the given key.
 *
 * returns pointer to link on success, NULL on failure (or item
 * not found)
 */
static inline struct mqhash_head *mqhash_search(
    struct mqhash_table *table,
    void *key)
{
    int index = 0;
    struct mqhash_head *tmp_link = NULL;

    /* find the hash value */
    index = table->hash(key, table->table_size);

    mqhash_rdlock( &(table->lock[index]));
    /* linear search at index to find match */
    mqhash_for_each(tmp_link, &(table->array[index]))
    {
	if (table->compare(key, tmp_link))
	{
	    mqhash_unlock (&(table->lock[index]));
	    return (tmp_link);
	}
    }
    mqhash_unlock (&(table->lock[index]));
    return (NULL);
}

/* mqhash_search_and_remove()
 *
 * searches for and removes a link in the hash table
 * that matches the given key
 *
 * returns pointer to link on success, NULL on failure (or item
 * not found).  On success, link is removed from hashtable.
 */
static inline struct mqhash_head *mqhash_search_and_remove(
    struct mqhash_table *table,
    void *key)
{
    int index = 0;
    struct mqhash_head *tmp_link = NULL;

    /* find the hash value */
    index = table->hash(key, table->table_size);

    /* linear search at index to find match */
    mqhash_wrlock(&table->lock[index]);
    mqhash_for_each(tmp_link, &(table->array[index]))
    {
	if (table->compare(key, tmp_link))
	{
	    mqhash_del(tmp_link);
	    mqhash_unlock(&table->lock[index]);
	    return (tmp_link);
	}
    }
    mqhash_unlock(&table->lock[index]);
    return (NULL);
}

#endif /* MQUICKHASH_H */
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 *
 * vim: ts=3
 */

