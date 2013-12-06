/*
 * copyright (c) 2002 Clemson University and Argonne National Lab
 *
 * Written by Murali Vilayannur.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Contact:  Rob Ross    rbross@parl.clemson.edu
 *           Phil Carns  pcarns@parl.clemson.edu
 */
#define __NO_VERSION__
#include "capfs_kernel_config.h"

/*
 * Preprocessor directives in C files are not good. 
 * Need to fix this someday...
 */

#ifdef CONFIG_RWSEM_XCHGADD_ALGORITHM 

#include <linux/rwsem.h>
#include <asm/errno.h>
#include "rwsemaphore.h"

/*
 * THIS CODE IS ALMOST AN EXACT RIPOFF OF THE READER-WRITER SEMAPHORE
 * CODE FROM lib/rwsem.c and other headers and C programs from the 
 * linux kernel sources.
 * It tries to implement interruptible reader-writer semaphores
 * both using the generic spinlock based approaches and the arch. 
 * specific approach.
 */

struct rwsem_waiter {
	struct list_head	list;
	struct task_struct	*task;
	unsigned int		flags;
#define RWSEM_WAITING_FOR_READ	0x00000001
#define RWSEM_WAITING_FOR_WRITE	0x00000002
};

/*
 * handle the lock being released whilst there are processes blocked on it that can now run
 * - if we come here, then:
 *   - the 'active part' of the count (&0x0000ffff) reached zero but has been re-incremented
 *   - the 'waiting part' of the count (&0xffff0000) is negative (and will still be so)
 *   - there must be someone on the queue
 * - the spinlock must be held by the caller
 * - woken process blocks are discarded from the list after having flags zeroised
 */
static inline struct rw_semaphore *__rwsem_do_wake(struct rw_semaphore *sem)
{
	struct rwsem_waiter *waiter;
	struct list_head *next;
	signed long oldcount;
	int woken, loop;

	rwsemtrace(sem,"Entering __rwsem_do_wake");
	/* only wake someone up if we can transition the active part of the count from 0 -> 1 */
 try_again:
	oldcount = rwsem_atomic_update(RWSEM_ACTIVE_BIAS,sem) - RWSEM_ACTIVE_BIAS;
	if (oldcount & RWSEM_ACTIVE_MASK)
		goto undo;

	waiter = list_entry(sem->wait_list.next,struct rwsem_waiter,list);

	/* try to grant a single write lock if there's a writer at the front of the queue
	 * - note we leave the 'active part' of the count incremented by 1 and the waiting part
	 *   incremented by 0x00010000
	 */
	if (!(waiter->flags & RWSEM_WAITING_FOR_WRITE))
		goto readers_only;

	list_del(&waiter->list);
	waiter->flags = 0;
	wake_up_process(waiter->task);
	goto out;

	/* grant an infinite number of read locks to the readers at the front of the queue
	 * - note we increment the 'active part' of the count by the number of readers (less one
	 *   for the activity decrement we've already done) before waking any processes up
	 */
 readers_only:
	woken = 0;
	do {
		woken++;

		if (waiter->list.next==&sem->wait_list)
			break;

		waiter = list_entry(waiter->list.next,struct rwsem_waiter,list);

	} while (waiter->flags & RWSEM_WAITING_FOR_READ);

	loop = woken;
	woken *= RWSEM_ACTIVE_BIAS-RWSEM_WAITING_BIAS;
	woken -= RWSEM_ACTIVE_BIAS;
	rwsem_atomic_add(woken,sem);

	next = sem->wait_list.next;
	for (; loop>0; loop--) {
		waiter = list_entry(next,struct rwsem_waiter,list);
		next = waiter->list.next;
		waiter->flags = 0;
		wake_up_process(waiter->task);
	}

	sem->wait_list.next = next;
	next->prev = &sem->wait_list;

 out:
	rwsemtrace(sem,"Leaving __rwsem_do_wake");
	return sem;
	/* undo the change to count, but check for a transition 1->0 */
 undo:
	if (rwsem_atomic_update(-RWSEM_ACTIVE_BIAS,sem)!=0)
		goto out;
	goto try_again;
}

/*
 * wait for a lock to be granted or to be interrupted
 */
static inline int rwsem_down_interruptible_failed_common(struct rw_semaphore *sem,
								 struct rwsem_waiter *waiter,
								 signed long adjustment)
{
	int ret = 0;
	struct task_struct *tsk = current;
	signed long count;


	set_task_state(tsk,TASK_INTERRUPTIBLE);

	/* set up my own style of waitqueue */
	spin_lock(&sem->wait_lock);
	waiter->task = tsk;

	list_add_tail(&waiter->list,&sem->wait_list);

	/* note that we're now waiting on the lock, but no longer actively read-locking */
	count = rwsem_atomic_update(adjustment,sem);

	/* if there are no longer active locks, wake the front queued process(es) up
	 * - it might even be this process, since the waker takes a more active part
	 */
	if (!(count & RWSEM_ACTIVE_MASK))
		sem = __rwsem_do_wake(sem);

	spin_unlock(&sem->wait_lock);

	/* wait to be given the lock or to be interrupted */
	for (;;) {
		if (!waiter->flags)
			break;
		/* is there a signal pending for this task? */
		if(signal_pending(tsk)) {
			ret = -EINTR;
			spin_lock(&sem->wait_lock);
			if(!waiter->flags) {
				ret = 0;
			}
			else {
				rwsem_atomic_add(-RWSEM_WAITING_BIAS,sem);
				list_del(&waiter->list);
			}
			spin_unlock(&sem->wait_lock);
			break;
		}
		schedule();
		set_task_state(tsk, TASK_INTERRUPTIBLE);
	}
	tsk->state = TASK_RUNNING;
	return ret;
}

/*
 * wait for a lock to be granted or to be interrupted or to timeout.
 * timeout is in jiffies.
 */
static inline int rwsem_down_timed_interruptible_failed_common(struct rw_semaphore *sem,
								 struct rwsem_waiter *waiter,
								 signed long adjustment, signed long timeout)
{
	int ret = 0;
	struct task_struct *tsk = current;
	signed long count;

	set_task_state(tsk,TASK_INTERRUPTIBLE);

	/* set up my own style of waitqueue */
	spin_lock(&sem->wait_lock);
	waiter->task = tsk;

	list_add_tail(&waiter->list,&sem->wait_list);

	/* note that we're now waiting on the lock, but no longer actively read-locking */
	count = rwsem_atomic_update(adjustment,sem);

	/* if there are no longer active locks, wake the front queued process(es) up
	 * - it might even be this process, since the waker takes a more active part
	 */
	if (!(count & RWSEM_ACTIVE_MASK))
		sem = __rwsem_do_wake(sem);

	spin_unlock(&sem->wait_lock);

	/* wait to be given the lock or to be interrupted */
	for (;;) {
		if (!waiter->flags)
			break;
		/* is there a signal pending for this task? */
		if(signal_pending(tsk)) {
			ret = -EINTR;
			spin_lock(&sem->wait_lock);
			if(!waiter->flags) {
				ret = 0;
			}
			else {
				rwsem_atomic_add(-RWSEM_WAITING_BIAS,sem);
				list_del(&waiter->list);
			}
			spin_unlock(&sem->wait_lock);
			break;
		}
		timeout = schedule_timeout(timeout);
		/* did we timeout */
		if(timeout == 0) {
			ret = -ETIME;
			spin_lock(&sem->wait_lock);
			/* did we get the lock? */
			if(!waiter->flags) {
				ret = 0;
			}
			else {
				rwsem_atomic_add(-RWSEM_WAITING_BIAS, sem);
				list_del(&waiter->list);
			}
			spin_unlock(&sem->wait_lock);
			break;
		}
		/* or did we get a signal? */
		else {
			set_task_state(tsk, TASK_INTERRUPTIBLE);
		}
	}
	tsk->state = TASK_RUNNING;
	return ret;
}

/*
 * wait for the read lock to be granted
 * Return if interrupted though with -EINTR
 */
int rwsem_down_read_interruptible_failed(struct rw_semaphore *sem)
{
	int ret;
	struct rwsem_waiter waiter;
	rwsemtrace(sem,"Entering rwsem_down_read_interruptible_failed");
	waiter.flags = RWSEM_WAITING_FOR_READ;
	ret = rwsem_down_interruptible_failed_common(sem,&waiter,RWSEM_WAITING_BIAS-RWSEM_ACTIVE_BIAS);
	rwsemtrace(sem,"Leaving rwsem_down_read_interruptible_failed");
	return ret;
}

/*
 * wait for the read lock to be granted or for timer to expire or to be interrupted.
 * Return if interrupted though with -EINTR, and if timer expired return -ETIME and
 * 0 on success. timeout is in jiffies.
 */
int rwsem_down_timed_read_interruptible_failed(struct rw_semaphore *sem, signed long timeout)
{
	int ret;
	struct rwsem_waiter waiter;
	rwsemtrace(sem,"Entering rwsem_down_timed_read_interruptible_failed");
	waiter.flags = RWSEM_WAITING_FOR_READ;
	ret = rwsem_down_timed_interruptible_failed_common(sem,&waiter,RWSEM_WAITING_BIAS-RWSEM_ACTIVE_BIAS, timeout);
	rwsemtrace(sem,"Leaving rwsem_down_timed_read_interruptible_failed");
	return ret;
}

/*
 * wait for the write lock to be granted
 * Return if interrupted with -EINTR.
 */
int rwsem_down_write_interruptible_failed(struct rw_semaphore *sem)
{
	int ret;
	struct rwsem_waiter waiter;
	rwsemtrace(sem,"Entering rwsem_down_write_interruptible_failed");
	waiter.flags = RWSEM_WAITING_FOR_WRITE;
	ret = rwsem_down_interruptible_failed_common(sem,&waiter,-RWSEM_ACTIVE_BIAS);
	rwsemtrace(sem,"Leaving rwsem_down_write_interruptible_failed");
	return ret;
}

/*
 * wait for the write lock to be granted or for timer to expire
 * Return if interrupted with -EINTR on interruption , -ETIME
 * if we time out and 0 on success.
 * timeout is in jiffies.
 */
int rwsem_down_timed_write_interruptible_failed(struct rw_semaphore *sem, signed long timeout)
{
	int ret;
	struct rwsem_waiter waiter;
	rwsemtrace(sem,"Entering rwsem_down_timed_write_interruptible_failed");
	waiter.flags = RWSEM_WAITING_FOR_WRITE;
	ret = rwsem_down_timed_interruptible_failed_common(sem,&waiter,-RWSEM_ACTIVE_BIAS, timeout);
	rwsemtrace(sem,"Leaving rwsem_down_timed_write_interruptible_failed");
	return ret;
}

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





