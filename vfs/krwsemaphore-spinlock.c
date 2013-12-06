/*
 * copyright (c) 2002 Clemson University and Argonne National Lab
 *
 * Written by Murali Vilayannur, with help/suggestions from 
 * David Howells(dhowells@redhat.com)
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
 *
 */

#define __NO_VERSION__

#include "capfs_kernel_config.h"

#include <asm/errno.h>
#include "rwsemaphore.h"

struct capfs_rwsem_waiter {
	struct list_head	list;
	struct task_struct	*task;
	unsigned int		flags;
#define RWSEM_WAITING_FOR_READ	0x00000001
#define RWSEM_WAITING_FOR_WRITE	0x00000002
};

/*
 * initialise the semaphore
 */
void fastcall init_capfs_rwsem(struct capfs_rw_semaphore *sem)
{
	sem->activity = 0;
	spin_lock_init(&sem->wait_lock);
	INIT_LIST_HEAD(&sem->wait_list);
	return;
}

/*
 * handle the lock being released whilst there are processes blocked on it that can now run
 * - if we come here, then:
 *   - the 'active count' _reached_ zero
 *   - the 'waiting count' is non-zero
 * - the spinlock must be held by the caller
 * - woken process blocks are discarded from the list after having flags zeroised
 */
static inline struct capfs_rw_semaphore *__rwsem_do_wake(struct capfs_rw_semaphore *sem)
{
	struct capfs_rwsem_waiter *waiter;
	int woken;

	waiter = list_entry(sem->wait_list.next,struct capfs_rwsem_waiter,list);

	/* try to grant a single write lock if there's a writer at the front of the queue
	 * - we leave the 'waiting count' incremented to signify potential contention
	 */
	if (waiter->flags & RWSEM_WAITING_FOR_WRITE) {
		sem->activity = -1;
		list_del(&waiter->list);
		waiter->flags = 0;
		wake_up_process(waiter->task);
		goto out;
	}

	/* grant an infinite number of read locks to the readers at the front of the queue */
	woken = 0;
	do {
		list_del(&waiter->list);
		waiter->flags = 0;
		wake_up_process(waiter->task);
		woken++;
		if (list_empty(&sem->wait_list))
			break;
		waiter = list_entry(sem->wait_list.next,struct capfs_rwsem_waiter,list);
	} while (waiter->flags&RWSEM_WAITING_FOR_READ);

	sem->activity += woken;

 out:
	return sem;
}

/*
 * wake a single writer
 */
static inline struct capfs_rw_semaphore *__rwsem_wake_one_writer(struct capfs_rw_semaphore *sem)
{
	struct capfs_rwsem_waiter *waiter;

	sem->activity = -1;

	waiter = list_entry(sem->wait_list.next,struct capfs_rwsem_waiter,list);
	list_del(&waiter->list);

	waiter->flags = 0;
	wake_up_process(waiter->task);
	return sem;
}

/*
 * get a read lock on the semaphore
 */
void fastcall __Down_read(struct capfs_rw_semaphore *sem)
{
	struct capfs_rwsem_waiter waiter;
	struct task_struct *tsk;

	spin_lock(&sem->wait_lock);

	if (sem->activity>=0 && list_empty(&sem->wait_list)) {
		/* granted */
		sem->activity++;
		spin_unlock(&sem->wait_lock);
		goto out;
	}

	tsk = current;
	set_task_state(tsk,TASK_UNINTERRUPTIBLE);

	/* set up my own style of waitqueue */
	waiter.task = tsk;
	waiter.flags = RWSEM_WAITING_FOR_READ;

	list_add_tail(&waiter.list,&sem->wait_list);

	/* we don't need to touch the semaphore struct anymore */
	spin_unlock(&sem->wait_lock);

	/* wait to be given the lock */
	for (;;) {
		if (!waiter.flags)
			break;
		schedule();
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	}

	tsk->state = TASK_RUNNING;

 out:
	return;
}

/*
 * trylock for reading -- returns 1 if successful, 0 if contention
 */
int fastcall __Down_read_trylock(struct capfs_rw_semaphore *sem)
{
	int ret = 0;
	spin_lock(&sem->wait_lock);
	if (sem->activity>=0 && list_empty(&sem->wait_list)) {
		/* granted */
		sem->activity++;
		ret = 1;
	}
	spin_unlock(&sem->wait_lock);
	return ret;
}

/*
 * get a read lock on the semaphore.
 * Can get interrupted though and will 
 * return with -EINTR.
 */
int fastcall __Down_read_interruptible(struct capfs_rw_semaphore *sem)
{
	int result = 0;
	struct capfs_rwsem_waiter waiter;
	struct task_struct *tsk;

	spin_lock(&sem->wait_lock);

	if (sem->activity>=0 && list_empty(&sem->wait_list)) {
		/* granted */
		sem->activity++;
		spin_unlock(&sem->wait_lock);
		goto out;
	}

	tsk = current;
	set_task_state(tsk,TASK_INTERRUPTIBLE);

	/* set up my own style of waitqueue */
	waiter.task = tsk;
	waiter.flags = RWSEM_WAITING_FOR_READ;

	list_add_tail(&waiter.list,&sem->wait_list);

	/* we don't need to touch the semaphore struct anymore */
	spin_unlock(&sem->wait_lock);

	/* wait to be given the lock */
	for (;;) {
		if (!waiter.flags)
			break;
		/* Is there a signal pending for this task */
		if(signal_pending(tsk)) {
			result = -EINTR;
			spin_lock(&sem->wait_lock);
			/* was i given the lock in the meantime? might as well return 0 here */
			if(!waiter.flags) {
				result = 0;
			}
			else {
				list_del(&waiter.list);
			}
			spin_unlock(&sem->wait_lock);
			break;
		}
		schedule();
		set_task_state(tsk, TASK_INTERRUPTIBLE);
	}
	tsk->state = TASK_RUNNING;
 out:
	return result;
}

/*
 * get a read lock on the semaphore.
 * Can get interrupted though and will 
 * return with -EINTR or can timeout and
 * return with -ETIME or return 0 on success.
 * timeout should be in jiffies.
 */
int fastcall __Down_timed_read_interruptible(struct capfs_rw_semaphore *sem, signed long timeout)
{
	int result = 0;
	struct capfs_rwsem_waiter waiter;
	struct task_struct *tsk;

	spin_lock(&sem->wait_lock);

	if (sem->activity>=0 && list_empty(&sem->wait_list)) {
		/* granted */
		sem->activity++;
		spin_unlock(&sem->wait_lock);
		goto out;
	}

	tsk = current;
	set_task_state(tsk,TASK_INTERRUPTIBLE);

	/* set up my own style of waitqueue */
	waiter.task = tsk;
	waiter.flags = RWSEM_WAITING_FOR_READ;

	list_add_tail(&waiter.list,&sem->wait_list);

	/* we don't need to touch the semaphore struct anymore */
	spin_unlock(&sem->wait_lock);

	/* wait to be given the lock */
	for (;;) {
		if (!waiter.flags)
			break;
		/* Is there a signal pending for this task */
		if(signal_pending(tsk)) {
			result = -EINTR;
			spin_lock(&sem->wait_lock);
			/* was i given the lock in the meantime? might as well return 0 here */
			if(!waiter.flags) {
				result = 0;
			}
			else {
				list_del(&waiter.list);
			}
			spin_unlock(&sem->wait_lock);
			break;
		}
		timeout = schedule_timeout(timeout);
		/* timer expired */
		if(timeout == 0) {
			/* was I given the lock? */
			if(!waiter.flags) {
				break;
			}
			result = -ETIME;
			spin_lock(&sem->wait_lock);
			/* was i given the lock in the meantime? might as well return 0 here */
			if(!waiter.flags) {
				result = 0;
			}
			else {
				list_del(&waiter.list);
			}
			spin_unlock(&sem->wait_lock);
			break;
		}
		else {
			set_task_state(tsk, TASK_INTERRUPTIBLE);
		}
	}
	tsk->state = TASK_RUNNING;
 out:
	return result;
}

/*
 * get a write lock on the semaphore
 * - note that we increment the waiting count anyway to indicate an exclusive lock
 */
void fastcall __Down_write(struct capfs_rw_semaphore *sem)
{
	struct capfs_rwsem_waiter waiter;
	struct task_struct *tsk;

	spin_lock(&sem->wait_lock);

	if (sem->activity==0 && list_empty(&sem->wait_list)) {
		/* granted */
		sem->activity = -1;
		spin_unlock(&sem->wait_lock);
		goto out;
	}

	tsk = current;
	set_task_state(tsk,TASK_UNINTERRUPTIBLE);

	/* set up my own style of waitqueue */
	waiter.task = tsk;
	waiter.flags = RWSEM_WAITING_FOR_WRITE;

	list_add_tail(&waiter.list,&sem->wait_list);

	/* we don't need to touch the semaphore struct anymore */
	spin_unlock(&sem->wait_lock);

	/* wait to be given the lock */
	for (;;) {
		if (!waiter.flags)
			break;
		schedule();
		set_task_state(tsk, TASK_UNINTERRUPTIBLE);
	}

	tsk->state = TASK_RUNNING;

 out:
	return;
}

/*
 * trylock for writing -- returns 1 if successful, 0 if contention
 */
int fastcall __Down_write_trylock(struct capfs_rw_semaphore *sem)
{
	int ret = 0;

	spin_lock(&sem->wait_lock);

	if (sem->activity==0 && list_empty(&sem->wait_list)) {
		/* granted */
		sem->activity = -1;
		ret = 1;
	}

	spin_unlock(&sem->wait_lock);

	return ret;
}

/*
 * get a write lock on the semaphore
 * - note that we increment the waiting count anyway to indicate an exclusive lock
 * We return -EINTR on a signal.
 */
int fastcall __Down_write_interruptible(struct capfs_rw_semaphore *sem)
{
	int result = 0;
	struct capfs_rwsem_waiter waiter;
	struct task_struct *tsk;

	spin_lock(&sem->wait_lock);

	if (sem->activity==0 && list_empty(&sem->wait_list)) {
		/* granted */
		sem->activity = -1;
		spin_unlock(&sem->wait_lock);
		goto out;
	}

	tsk = current;
	set_task_state(tsk,TASK_INTERRUPTIBLE);

	/* set up my own style of waitqueue */
	waiter.task = tsk;
	waiter.flags = RWSEM_WAITING_FOR_WRITE;

	list_add_tail(&waiter.list,&sem->wait_list);

	/* we don't need to touch the semaphore struct anymore */
	spin_unlock(&sem->wait_lock);

	/* wait to be given the lock */
	for (;;) {
		if (!waiter.flags)
			break;
		/* Is there a signal pending for this task? */
		if(signal_pending(tsk)) {
			result = -EINTR;
			spin_lock(&sem->wait_lock);
			/* was i given the lock in the meantime? might as well return 0 here */
			if(!waiter.flags) {
				result = 0;
			}
			else {
				list_del(&waiter.list);
			}
			spin_unlock(&sem->wait_lock);
			break;
		}
		schedule();
		set_task_state(tsk, TASK_INTERRUPTIBLE);
	}
	tsk->state = TASK_RUNNING;
 out:
	return result;
}

/*
 * get a write lock on the semaphore
 * - note that we increment the waiting count anyway to indicate an exclusive lock
 * We return -EINTR on a signal and -ETIME on timeout and 0 on su.
 * timeout is in jiffies.
 */
int fastcall __Down_timed_write_interruptible(struct capfs_rw_semaphore *sem, signed long timeout)
{
	int result = 0;
	struct capfs_rwsem_waiter waiter;
	struct task_struct *tsk;

	spin_lock(&sem->wait_lock);

	if (sem->activity==0 && list_empty(&sem->wait_list)) {
		/* granted */
		sem->activity = -1;
		spin_unlock(&sem->wait_lock);
		goto out;
	}

	tsk = current;
	set_task_state(tsk,TASK_INTERRUPTIBLE);

	/* set up my own style of waitqueue */
	waiter.task = tsk;
	waiter.flags = RWSEM_WAITING_FOR_WRITE;

	list_add_tail(&waiter.list,&sem->wait_list);

	/* we don't need to touch the semaphore struct anymore */
	spin_unlock(&sem->wait_lock);

	/* wait to be given the lock */
	for (;;) {
		if (!waiter.flags)
			break;
		/* Is there a signal pending for this task? */
		if(signal_pending(tsk)) {
			result = -EINTR;
			spin_lock(&sem->wait_lock);
			/* was i given the lock in the meantime? might as well return 0 here */
			if(!waiter.flags) {
				result = 0;
			}
			else {
				list_del(&waiter.list);
			}
			spin_unlock(&sem->wait_lock);
			break;
		}
		timeout = schedule_timeout(timeout);
		/* did timer expire */
		if(timeout == 0) {
			/* just check if we got the lock? */
			if(!waiter.flags) {
				break;
			}
			result = -ETIME;
			spin_lock(&sem->wait_lock);
			/* was i given the lock in the meantime? might as well return 0 here */
			if(!waiter.flags) {
				result = 0;
			}
			else {
				list_del(&waiter.list);
			}
			spin_unlock(&sem->wait_lock);
			break;
		}
		else {
			set_task_state(tsk, TASK_INTERRUPTIBLE);
		}
	}
	tsk->state = TASK_RUNNING;
 out:
	return result;
}

/*
 * release a read lock on the semaphore
 */
void fastcall __Up_read(struct capfs_rw_semaphore *sem)
{
	spin_lock(&sem->wait_lock);
	if (--sem->activity==0 && !list_empty(&sem->wait_list))
		sem = __rwsem_wake_one_writer(sem);
	spin_unlock(&sem->wait_lock);
	return;
}

/*
 * release a write lock on the semaphore
 */
void fastcall __Up_write(struct capfs_rw_semaphore *sem)
{
	spin_lock(&sem->wait_lock);
	sem->activity = 0;
	if (!list_empty(&sem->wait_list))
		sem = __rwsem_do_wake(sem);
	spin_unlock(&sem->wait_lock);
	return;
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


