#ifndef _RW_SEMAPHORE_H
#define _RW_SEMAPHORE_H

#ifdef __KERNEL__

#include <linux/list.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>


/*
 * THIS CODE IS ALMOST AN EXACT COPY OF THE READER-WRITER SEMAPHORE
 * CODE FROM lib/rwsem-spinlock.c and other headers.
 * It tries to implement interruptible reader-writer semaphores,
 * Lots of suggestions and helpful comments from David Howells (dhowells@redhat.com)
 * We tried our best to extend the kernel functionality in our module code,
 * but each distro treats rw_semaphore differently and have different implementations.
 * So instead of keeping a version around for each distro, we decided to
 * copy the whole stuff outside and redo it. 
 * Tradeoffs in maintainance and code-bloat. Code-bloat won!
 */

/*
 * the rw-semaphore definition
 * - if activity is 0 then there are no active readers or writers
 * - if activity is +ve then that is the number of active readers
 * - if activity is -1 then there is one active writer
 * - if wait_list is not empty, then there are processes waiting for the semaphore
 */
struct capfs_rw_semaphore {
	__s32			activity;
	spinlock_t		wait_lock;
	struct list_head	wait_list;
};

/*
 * initialisation
 */

#define __CAPFS_RWSEM_INITIALIZER(name) \
{ 0, SPIN_LOCK_UNLOCKED, LIST_HEAD_INIT((name).wait_list) }

#define DECLARE_CAPFS_RWSEM(name) \
	struct capfs_rw_semaphore name = __CAPFS_RWSEM_INITIALIZER(name)

#ifndef fastcall
#ifdef __i386__
#define fastcall	__attribute__((regparm(3)))
#else
#define fastcall
#endif
#endif

extern void FASTCALL(init_capfs_rwsem(struct capfs_rw_semaphore *sem));
extern void FASTCALL(__Down_read(struct capfs_rw_semaphore *sem));
extern int FASTCALL(__Down_read_trylock(struct capfs_rw_semaphore *sem));
extern int FASTCALL(__Down_read_interruptible(struct capfs_rw_semaphore *));
extern int FASTCALL(__Down_timed_read_interruptible(struct capfs_rw_semaphore *, signed long timeout));
extern void FASTCALL(__Down_write(struct capfs_rw_semaphore *sem));
extern int FASTCALL(__Down_write_trylock(struct capfs_rw_semaphore *sem));
extern int FASTCALL(__Down_write_interruptible(struct capfs_rw_semaphore *));
extern int FASTCALL(__Down_timed_write_interruptible(struct capfs_rw_semaphore *, signed long timeout));
extern void FASTCALL(__Up_read(struct capfs_rw_semaphore *sem));
extern void FASTCALL(__Up_write(struct capfs_rw_semaphore *sem));

/*
 * lock for reading
 */
static inline void Down_read(struct capfs_rw_semaphore *sem)
{
	__Down_read(sem);
	return;
}

/*
 * trylock for reading -- returns 1 if successful, 0 if contention
 */
static __inline__ int Down_read_trylock(struct capfs_rw_semaphore *rwsem)
{
	return __Down_read_trylock(rwsem);
}

/* 
 * read lock which can be interrupted by a signal. Returns 0 on success and -EINTR on signal. 
 */
static __inline__ int Down_read_interruptible(struct capfs_rw_semaphore *rwsem)
{
	return __Down_read_interruptible(rwsem);
}

/*
 * read lock which can be interrupted by a signal or by a timeout. Returns 0 on success,
 * -EINTR on signal and -ETIME on time out
 * timeout is in seconds.
 */
static __inline__ int Down_timed_read_interruptible(struct capfs_rw_semaphore *rwsem, signed long timeout)
{
	return __Down_timed_read_interruptible(rwsem, timeout);
}

/*
 * lock for writing
 */
static inline void Down_write(struct capfs_rw_semaphore *sem)
{
	__Down_write(sem);
	return;
}

/*
 * trylock for writing -- returns 1 if successful, 0 if contention
 */
static __inline__ int Down_write_trylock(struct capfs_rw_semaphore *rwsem)
{
	return __Down_write_trylock(rwsem);
}

/* 
 * write lock which can be interrupted by a signal. Returns 0 on success and -EINTR on signal. 
 */
static __inline__ int Down_write_interruptible(struct capfs_rw_semaphore *rwsem)
{
	return __Down_write_interruptible(rwsem);
}

/*
 * write lock which can be interrupted by a signal or by a timeout. Returns 0 on success,
 * -EINTR on signal and -ETIME on time out
 * timeout is in seconds.
 */
static __inline__ int Down_timed_write_interruptible(struct capfs_rw_semaphore *rwsem, signed long timeout)
{
	return __Down_timed_write_interruptible(rwsem, timeout);
}

/*
 * release a read lock
 */
static inline void Up_read(struct capfs_rw_semaphore *sem)
{
	__Up_read(sem);
}

/*
 * release a write lock
 */
static inline void Up_write(struct capfs_rw_semaphore *sem)
{
	__Up_write(sem);
}

#endif

#endif
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 *
 * vim: ts=3
 */
