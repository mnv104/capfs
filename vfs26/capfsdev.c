/*
 * copyright (c) 1999 Rob Ross and Phil Carns, all rights reserved.
 *
 * Written by Rob Ross and Phil Carns, funded by Scyld Computing.
 *
 * This program is free software; you can redistribute it and/or
 * modify
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

/* 
 * This code could potentially use 3 different methods for memory
 * transfers on read and write depending on which option is defined.
 * CAPFS_BUFFER_DYNAMIC - dynamically allocate a buffer for each request
 * CAPFS_BUFFER_STATIC - use a single statically allocated buffer
 * CAPFS_BUFFER_MAPPED - use the kernel patch to map the user's buffer
 *   into kernel space, avoiding the copy and the allocate altogether.
 */

/* These calls are intended to handle communications between 
 *	the kernel low level vfs calls and the user level capfsd daemon.
 */

#include "capfs_kernel_config.h"
#include "ll_capfs.h"
#include "capfs_bufmap.h"
#include "capfs_linux.h"
#include "capfsdev.h"
#include "capfs_proc.h"
#include "capfs_mount.h"



/* and a maximum sequence number for kernel communications */
/* note: value stolen from INT_MAX macro, but adjusted to reflect
 * the capfs_seq_t type
 */
#define CAPFSDEV_MAX_SEQ ((capfs_seq_t)(~0U>>1))

extern int capfs_buffer;
extern int capfs_maxsz;
extern int capfs_major;



/********************************************************************
 * file operations:
 */

/* IO operations on the device */
static unsigned int capfsdev_poll(struct file *my_file, 
											struct poll_table_struct *wait);
static int capfsdev_ioctl (struct inode * inode, struct file * filp, 
								  unsigned int cmd, unsigned long arg);
static int capfsdev_open(struct inode * inode, struct file * filp);
static int capfsdev_release(struct inode * inode, struct file * filp);
static ssize_t capfsdev_read(struct file *my_file, char __user *buf, 
								size_t buf_sz, loff_t *offset);
static ssize_t capfsdev_write(struct file *my_file, const char __user *buf, 
								 size_t buf_sz, loff_t *offset);

static struct file_operations capfsdev_fops =
{
	.read = capfsdev_read,
	.write = capfsdev_write,
	.poll = capfsdev_poll,
	.ioctl = capfsdev_ioctl,
	.open = capfsdev_open,
	.release = capfsdev_release,
};

/*********************************************************************
 * various structures used in the support code:
 */

/* buffer and size for memory operations */
struct capfsdev_mem_reg {
	void* ptr;
	capfs_size_t size;
};

/* this is the internal representation of the upcalls as needed to
 * be placed on a queue
 */
struct capfsdev_upq_entry {
	struct capfs_upcall pending_up;
	struct list_head req_list;
};

/* this is the internal representation of the downcalls as needed to
 * be placed on a queue
 */
struct capfsdev_downq_entry {
	struct capfs_downcall pending_down;
	struct list_head req_list;
};

/* sequence number entries in the seq_pending queue */
struct capfsdev_seq_entry {
	capfs_seq_t seq;
	struct list_head seq_list;
	int invalid;
	int kernel_read; /* flag indicating that data is going to kernel space */

	/* not the most logical place to store this memory transfer
	 * buffer information, but it will keep me from having to
	 * manipulate an extra queue...
	 */

	/* this is the kiobuf for reads and writes to user areas */
	struct kiobuf *iobuf;
	/* this variable is used to determine what phase of the write
	 * operation we are in... may be phased out later.
	 */
	int step;
	/* this is the pointer and offset for reads and writes to kernel
	 * areas
	 */
	struct capfsdev_mem_reg kernelbuf;
};

/* total Time spent by daemon servicing calls */
int64_t total_service_time = 0, total_rd_time = 0, total_wr_time = 0;


/*********************************************************************
 * internal support functions:
 */

/* static int queue_len(struct list_head *my_list); */
static int down_dequeue(struct list_head *my_list, 
	capfs_seq_t my_seq, struct capfs_downcall* out_downcall); 
static int seq_is_pending(struct list_head *my_list, capfs_seq_t
	my_seq, int* invalid);
static int capfsdev_down_enqueue(struct capfs_downcall *in_downcall);
static int remove_seq_pending(struct list_head *my_list, capfs_seq_t my_seq);
static int invalidate_seq_pending(struct list_head *my_list, capfs_seq_t 
	my_seq);
static capfs_seq_t inc_locked_seq(void);
static int up_dequeue(struct list_head *my_list, capfs_seq_t my_seq);
static struct capfsdev_seq_entry* match_seq_entry(struct list_head *my_list, 
	capfs_seq_t my_seq);
static int is_fake_useraddr(void);
static int capfsdev_read_in_data(void *kptr, char *uptr, 
													capfs_size_t size);
static int capfsdev_write_out_data(char *uptr, void *kptr, 
													  capfs_size_t size);
static int check_for_bad_signals(int *sig_nr);
static void cleanup_buffer(struct capfs_upcall *in_upcall,
											 void **copybufp, void **capfs_map_bufp);
static int setup_buffer(struct capfs_upcall *in_upcall, 
								void **copybufp, void **capfs_map_bufp);
/**********************************************************************
 * various device internal queues and other device wide structures
 * that require locking before use. 
 */

/* this will be the queue of pending messages to capfsd */
LIST_HEAD(up_pending);

/* this is the queue of upcall sequence numbers to be matched 
 * with the corresponding downcall in capfsdev_enqueue.
 */
LIST_HEAD(seq_pending);

/* this is the list of pending downcalls */
LIST_HEAD(down_pending);

/* this the sequence number assigned to upcalls */
static capfs_seq_t current_seq = 1;

/* this variable keeps up with the number of processes which have
 * opened the device at any given time.  Mostly for sanity checking
 * so that capfs ops aren't attempted with no capfsd.
 */
static int nr_daemons;

/* this is a buffer which will be used in transfers between the
 * application and capfsd if static copies are used (the "static" buffer
 * option)
 */
static void *static_buf = NULL;

/* two semaphores:
 * static_buf_sem - protects our static buffer
 * seq_sem - protects sequence list
 */
static DECLARE_MUTEX(static_buf_sem);
static DECLARE_MUTEX(seq_sem);

/* keeps up with the amount of memory allocated for dynamic 
 * and mapped buffers 
 */
static unsigned long capfsdev_buffer_size = 0;

/* these locks are used to protect the data defined above */
static spinlock_t capfsdev_up_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t capfsdev_down_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t capfsdev_current_seq_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t capfsdev_nr_daemons_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t capfsdev_buffer_pool_lock = SPIN_LOCK_UNLOCKED;

/* three device wait queues:
 * capfsdev_waitq - used for polling
 * capfsdev_seq_wait - used for sleeping while waiting on a
 *   sequence-matched downcall
 * capfsdev_buffer_pool_wait - used to sleep when dynamic buffer has
 *   gotten too large
 */
static DECLARE_WAIT_QUEUE_HEAD(capfsdev_waitq);
static DECLARE_WAIT_QUEUE_HEAD(capfsdev_seq_wait);
static DECLARE_WAIT_QUEUE_HEAD(capfsdev_buffer_pool_wait);

/**********************************************************************
 * module specific code :
 */

/* capfsdev_init()
 *
 * Called at module load time.
 */
int capfsdev_init(void)
{
	int ret;       /* Dynamically assigned major number */
	if ((ret = register_chrdev(capfs_major, "capfsd", &capfsdev_fops)) < 0)
	{
		/* failed to register device */
		if(capfs_major){
			PERROR("capfsdev : unable to get major %d\n", CAPFSD_MAJOR);
		}
		else{
			PERROR("capfsdev : could not dynamically assign major number.\n");
		}

		return -ENODEV;
	}
	else{
		if(ret > 0){
			/* major number was dynamically assigned */
			capfs_major = ret;
		}
	}

	/* initialize the sequence counter */
	spin_lock(&capfsdev_current_seq_lock);
	current_seq = 0;
	spin_unlock(&capfsdev_current_seq_lock);
	spin_lock(&capfsdev_nr_daemons_lock);
	nr_daemons = 0;
	spin_unlock(&capfsdev_nr_daemons_lock);

	if (capfs_buffer == CAPFS_BUFFER_STATIC) {
		down(&static_buf_sem);
		static_buf = vmalloc(capfs_maxsz);
		up(&static_buf_sem);
		if (static_buf == NULL) return -ENOMEM;
	}

	PDEBUG(D_PSDEV, "capfsdev: device module loaded (major=%d)\n", capfs_major);
	capfs_sysctl_init();
	return 0;
}

/* capfsdev_cleanup()
 *
 * Called at module unload.
 */
void capfsdev_cleanup(void)
{
	struct capfsdev_upq_entry *up_check;
	struct capfsdev_downq_entry *down_check;
	struct capfsdev_seq_entry *seq_check;
	int rm_seqs = 0;
	int inv_seqs = 0;
   capfs_sysctl_clean();

	/* we need to free up the all of the pending lists at this point */

	spin_lock(&capfsdev_up_lock);
	while (up_pending.next != &up_pending) {
		up_check = list_entry(up_pending.next, struct capfsdev_upq_entry, 
									 req_list);
		list_del(up_pending.next);
		kfree(up_check);
	}
	spin_unlock(&capfsdev_up_lock);

	spin_lock(&capfsdev_down_lock);
	while (down_pending.next != &down_pending) {
		down_check = list_entry(down_pending.next, struct capfsdev_downq_entry, 
										req_list);
		list_del(down_pending.next);
		kfree(down_check);
	}
	spin_unlock(&capfsdev_down_lock);

	down(&seq_sem);
	while (seq_pending.next != &seq_pending) {
		seq_check = list_entry(seq_pending.next, struct capfsdev_seq_entry, 
									  seq_list);
		rm_seqs++;
		if(seq_check->invalid){
			inv_seqs++;
		}
		list_del(seq_pending.next);
		kfree(seq_check);
	}
	up(&seq_sem);

	if(rm_seqs){
		PERROR("capfsdev: %d upcall sequences removed, %d of which had become invalid.\n", rm_seqs, inv_seqs);
	}

	if (capfs_buffer == CAPFS_BUFFER_STATIC) {
		/* get rid of the static buffer we had laying around */
		down(&static_buf_sem);
		if (static_buf != NULL) {
			vfree(static_buf);
			static_buf = NULL;
		}
		up(&static_buf_sem);
	}

	unregister_chrdev(capfs_major, "capfsd");
	PDEBUG(D_PSDEV, "capfsdev: device module unloaded\n");
	return;
}


/***************************************************************
 * file operation code:
 */

/* capfsdev_open()
 *
 * Called when a process opens the CAPFS device
 *
 * Returns 0 on success, -errno on failure.
 */
static int capfsdev_open(struct inode *inode, struct file *filp)
{
	int ret;
	PDEBUG(D_PSDEV, "capfsdev: device opened.\n");
	
	spin_lock(&capfsdev_nr_daemons_lock);
	/* we really only allow one daemon to use this device at a time */
	if (nr_daemons != 0) 
	{
		printk(KERN_ERR "Cannot open the device file many times\n");
		spin_unlock(&capfsdev_nr_daemons_lock);
		return(-EBUSY);
	}
	else {
		ret = generic_file_open(inode, filp);
		if (ret == 0)
		{
			ret = (try_module_get(capfs_fs_type.owner) ? 0 : 1);
			if (ret == 0) {
				nr_daemons++;
			}
			else {
				printk(KERN_ERR "Could not obtain reference for device file\n");
			}
		}
	}
	spin_unlock(&capfsdev_nr_daemons_lock);

	return(0);
}

/* capfsdev_release()
 *
 * Called when process closes the device
 */
static int capfsdev_release(struct inode * inode, struct file * filp)
{

	PDEBUG(D_PSDEV, "capfsdev: device released.\n");

	spin_lock(&capfsdev_nr_daemons_lock);
	nr_daemons--;
	spin_unlock(&capfsdev_nr_daemons_lock);
	
	module_put(capfs_fs_type.owner);

	return(0);
}


/* capfsdev_write()
 *
 * Called when the CAPFS daemon writes into the CAPFS device file.
 *
 * Returns 0 on success, -errno on failure.
 */
static ssize_t capfsdev_write(struct file *my_file, const char __user *buf, 
	size_t buf_sz, loff_t *offset)
{
	struct capfs_downcall in_req;
	int req_sz = sizeof(struct capfs_downcall);
	int ret = 0;
	int invalid = 0;
	struct capfsdev_seq_entry *my_seq = NULL;

	PDEBUG(D_DOWNCALL, "capfsdev: somebody is writing to me\n");
	if(buf_sz != req_sz){
		return -EINVAL;
	}
	
	/* grab the request from user space so that we can manipulate it */
	ret = copy_from_user(&in_req, buf, buf_sz);
	if (ret != 0) {
		PERROR("capfsdev: error from copy_from_user.\n");
		return -ENOMEM;
	}

	/* verify the magic */
	if (in_req.magic != CAPFS_DOWNCALL_MAGIC) {
		PERROR("capfsdev: bad downcall magic.\n");
		return -EINVAL;
	}
	/* Add up the total service time */
	total_service_time += in_req.total_time;
	/* add up the total rd/wr service time if this is a response to a rd/wr op */
	if (in_req.type == WRITE_OP) 
	{
		total_wr_time += in_req.total_time;
	}
	else if (in_req.type == READ_OP)
	{
		total_rd_time += in_req.total_time;
	}

	if (in_req.seq == -1) {
		/* this is an independent downcall, do not match it to
		 * an outstanding upcall sequence. 
       */
		/* do something else instead- not defined yet. */
		PERROR("capfsdev: unexpected independent downcall.\n");
		return -ENOSYS;
	}

	PDEBUG(D_TIMING, "capfsdev: writing downcall number %d.\n",
		in_req.seq);

	/* ok- we are going to grab this semaphore right here, and then hang
	 * on to it for a long time to make sure that no one invalidates our
	 * sequence number while we are completing this downcall!
	 */
	down(&seq_sem);

	/* check to see if we are expecting this sequence number */
	ret = seq_is_pending(&seq_pending, in_req.seq, &invalid);
	if(!ret){
		/* Uh-oh... something is amok because we don't have the
		 * sequence number for this downcall queued up... bail
		 * out! 
       */
		up(&seq_sem);
		PDEBUG(D_TIMING, "capfsdev: can't match sequence number.\n");
		return -EINVAL;
	}

	/* at this point we need to be sure this write is still valid
	 * with respect to its comm sequence.  If not just eat it and
	 * return as normal- the user app on the the other side was
	 * probably terminated. 
    */
	if (invalid) {
		/* what sort of op has been invalidated? */
		my_seq = match_seq_entry(&seq_pending, in_req.seq);
		if(in_req.type == WRITE_OP && !my_seq->step) {
			/* this is the _first_ step in a write operation */
			/* we can't remove this sequence until the 2nd step */
			PDEBUG(D_TIMING, "capfsdev: invalid seq in 1st write step.\n");
			my_seq->step = 1;
			/* do nothing for now; cleanup on next step */
			up(&seq_sem);
			return req_sz;
		}

		PDEBUG(D_TIMING, "capfsdev: recieved write for invalid seq.\n");
		ret = remove_seq_pending(&seq_pending, in_req.seq);
		up(&seq_sem);
		if(ret < 0){
			PDEBUG(D_TIMING, "capfsdev: remove of seq_entry failed... \n");
		}
		return req_sz;
	}
	 
	/* now everything should be Ok for submitting this downcall */

	/* use the switch here to catch special test cases as well as
	 * normal requests that require memory transfers. 
    */

	switch (in_req.type) {
		case WRITE_OP:
			/* There are two separate downcalls for writes.  The first one is
			 * used by the capfsd to indicate the location in its address space
			 * where data should be placed.  The second indicates the result of
			 * the CAPFS request it performed on behalf of the application
			 * process.  
          */
			PDEBUG(D_SPECIAL, "capfsdev: performing memory xfer (write).\n");

			if ((my_seq = match_seq_entry(&seq_pending, in_req.seq)) == NULL) {
				up(&seq_sem);
				PDEBUG(D_SPECIAL, "capfsdev: couldn't find iobuf!\n");
				return -EINVAL;
			}

			/* Here is where we determine if this is the first or second downcall
          * for this write operation.  The my_seq->step value is 0 prior to
          * the first downcall.  We set it to 1 to indicate on the next pass
			 * that we have already received the first downcall for this 
			 * operation.
          */
			if (!my_seq->step) 
			{
				my_seq->step = 1;
								
				if (my_seq->kernel_read) 
				{
					PDEBUG(D_SPECIAL, "capfsdev: [step1] preparing to copy %ld bytes to %p <- %p.\n", 
							 (long)in_req.xfer.size, in_req.xfer.ptr, my_seq->kernelbuf.ptr);
					/* pulling data from kernel space */
					ret = capfsdev_write_out_data(in_req.xfer.ptr, 
														  my_seq->kernelbuf.ptr, 
														  in_req.xfer.size);
				}
				else {
					PDEBUG(D_SPECIAL, "capfsdev: [step2] preparing to copy %ld bytes to %p <- %p.\n", 
							 (long)in_req.xfer.size, in_req.xfer.ptr, my_seq->iobuf);
					ret = capfsdev_write_out_data(in_req.xfer.ptr,
														  my_seq->iobuf, 
														  in_req.xfer.size);
				}

				if (ret != 0) {
					up(&seq_sem);
					PDEBUG(D_SPECIAL, "capfsdev: capfs memory transfer failure (%d)\n", ret);
					return -ENOMEM;
				}
		
				/* we don't enqueue the first of the two write downcalls; 
             * just return instead.
				 */
				up(&seq_sem);
				return req_sz;
			}
			
			break;
		case TEST_RD_XFER:
		case READ_OP:
			PDEBUG(D_SPECIAL, "capfsdev: performing memory xfer (read).\n");
			if((my_seq = match_seq_entry(&seq_pending, in_req.seq)) == NULL){
				PERROR("capfsdev: couldn't find iobuf!\n");
				up(&seq_sem);
				return -EINVAL;
			}
			if (my_seq->kernel_read) {
				PDEBUG(D_SPECIAL, "capfsdev: [kernelread] preparing to copy %ld bytes from %p -> %p.\n",
						 (long)in_req.xfer.size, in_req.xfer.ptr, my_seq->kernelbuf.ptr);
				/* storing data in kernel space */
				ret = copy_from_user(my_seq->kernelbuf.ptr, 
					in_req.xfer.ptr, 
					in_req.xfer.size);
			}
			else {
				PDEBUG(D_SPECIAL, "capfsdev: [userread] preparing to copy %ld bytes from %p -> %p.\n",
						 (long)in_req.xfer.size, in_req.xfer.ptr, my_seq->iobuf);
				ret = capfsdev_read_in_data(my_seq->iobuf, 
													in_req.xfer.ptr, 
													in_req.xfer.size);
			}

			if (ret != 0) {
				up(&seq_sem);
				PDEBUG(D_SPECIAL, "capfsdev: copy_from_user/capfs_copy_to_userbuf failure.\n");
				return -ENOMEM;
			}
			break;
		case GETDENTS_OP:
			PDEBUG(D_SPECIAL, "capfsdev: performing memory xfer (getdents).\n");
			if ((my_seq = match_seq_entry(&seq_pending, in_req.seq)) == NULL) {
				up(&seq_sem);
				PDEBUG(D_SPECIAL, "capfsdev: couldn't find kernelbuf!\n");
				return -EINVAL;
			}

			ret = copy_from_user(my_seq->kernelbuf.ptr, in_req.xfer.ptr, 
				in_req.xfer.size);
			if (ret != 0) {
				up(&seq_sem);
				PDEBUG(D_SPECIAL, 
						 "capfsdev: copy_from_user to capfs_userbuf failure.\n");
				return -ENOMEM;
			}
			break;
		case READLINK_OP:
			PDEBUG(D_SPECIAL, "capfsdev: performing memory xfer (readlink).\n");
			if ((my_seq = match_seq_entry(&seq_pending, in_req.seq)) == NULL) {
				up(&seq_sem);
				PDEBUG(D_SPECIAL, "capfsdev: couldn't find kernelbuf!\n");
				return -EINVAL;
			}
			PDEBUG(D_SPECIAL, "capfsdev: copying %ld bytes from userspace %p to kernelspace %p\n", (long)in_req.xfer.size, my_seq->kernelbuf.ptr, in_req.xfer.ptr);

			ret = copy_from_user(my_seq->kernelbuf.ptr, in_req.xfer.ptr, 
				in_req.xfer.size + 1);
			if (ret != 0) {
				up(&seq_sem);
				PDEBUG(D_SPECIAL, 
						 "capfsdev: copy_from_user to capfs_userbuf failure.\n");
				return -ENOMEM;
			}
			break;
		default:
			/* no memory transfer required for this type of request. */
			break;
	}
	
	ret = capfsdev_down_enqueue(&in_req);
	if (ret < 0) {
		up(&seq_sem);
		return ret;
	}
	
	up(&seq_sem);
	return req_sz;
}


/* capfsdev_read_in_data()
 *
 * Parameters:
 * kptr - kernel space destination, could be an address or a user-space address's descriptor
 * uptr - user space (source) address pointer
 * size - size of data to move
 */
static int capfsdev_read_in_data(void *kptr, char __user *uptr, 
													capfs_size_t size)
{
	int ret;
	if (capfs_buffer == CAPFS_BUFFER_MAPPED) {
      /* in this case we are copying from one userspace to another */
		ret = capfs_copy_to_userbuf(kptr, uptr, size);
		return ret;
	}
   ret = copy_from_user((char *)kptr, uptr, size);
   return ret;
}

/* capfsdev_write_out_data()
 *
 * Parameters:
 * uptr - user space (destination) address pointer
 * kptr - kernel space source, could be an address  or the user-space daemon's mapping descriptor
 * size - size of data to move
 */
static int capfsdev_write_out_data(char __user *uptr, void *kptr, 
													  capfs_size_t size)
{
	int ret;
	if (capfs_buffer == CAPFS_BUFFER_MAPPED) {
      /* in this case we are copying from one userspace to another */
		ret = capfs_copy_from_userbuf(uptr, kptr, size);
		return ret;
	}
   ret = copy_to_user(uptr, (char *)kptr, size);
   return ret;
}



/* capfsdev_read()
 *
 * Called when capfsd reads from the CAPFS device.
 *
 * Returns 0 on success, -errno on failure.
 */
static ssize_t capfsdev_read(struct file *my_file, char __user *buf,
		size_t buf_sz, loff_t *offset)
{

	struct capfsdev_upq_entry *out_entry;
	int retval = 0;
	DECLARE_WAITQUEUE(wait, current);

	PDEBUG(D_UPCALL, "capfsdev: someone is reading from me.\n");
	
	if (buf_sz != sizeof(struct capfs_upcall)) {
		return -EINVAL;
	}

	/* put ourselves on a wait queue while we check to see if there is
	 * really an upcall available to read.  If it isn't there, we will
	 * schedule ourselves to wait on it.
	 */
	wait.task = current;
	add_wait_queue(&capfsdev_waitq, &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	while(up_pending.prev == &up_pending){
		if(my_file->f_flags & O_NONBLOCK){
			retval = -EAGAIN;
			break;
		}
		if(signal_pending(current)){
			retval = -ERESTARTSYS;
			break;
		}
		schedule();
	}

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&capfsdev_waitq, &wait);

	if(retval){
		/* we were interrupted or are in non-blocking mode w/ nothing to do */
		return(retval);
	}

	/* if we reach this point in the code, there should be an upcall to
	 * read, unless the other side somehow manages to abort before we
	 * actually read it out
	 */

	/* get the upcall off of the queue and copy it to the user (capfsd) */
	spin_lock(&capfsdev_up_lock);
	if (up_pending.prev != &up_pending) {
		out_entry = list_entry(up_pending.prev, struct capfsdev_upq_entry, 
									  req_list);
		PDEBUG(D_TIMING, "capfsdev: reading upcall number %d.\n",
			out_entry->pending_up.seq);
		list_del(up_pending.prev);
		spin_unlock(&capfsdev_up_lock);
		retval = copy_to_user(buf, &(out_entry->pending_up), sizeof(struct capfs_upcall));
		kfree(out_entry);
		return(sizeof(struct capfs_upcall));
	}
	else {
		/* nothing in the queue */
		spin_unlock(&capfsdev_up_lock);
		return 0;
	}
}

/* poll (was select in 2.0 kernel) */
static unsigned int capfsdev_poll(
	struct file *my_file,
	struct poll_table_struct *wait)
{
	unsigned int mask = POLLOUT | POLLWRNORM;

	if (nr_daemons == 1)
	{
		poll_wait(my_file, &capfsdev_waitq, wait);
		if(!(list_empty(&up_pending))){
			mask |= POLLIN | POLLRDNORM;
		}
	}
		
	return mask;
}

/* ioctl is mostly used for debugging right now. */
static int capfsdev_ioctl (struct inode * inode, 
		struct file * filp, unsigned int cmd, unsigned long arg)
{
	struct capfs_upcall *loop_up = NULL;
	struct capfs_downcall *test_req = NULL;
	int ret = 0;
	
	struct ioctl_mem_reg {
		void* ptr;
		capfs_size_t size;
	} rd_xfer_reg;

	PDEBUG(D_PIOCTL, "capfsdev: ioctl.\n");

	switch(cmd)
	{
		case(TEST_UPWAIT):
			/* this will exercise the communication mechanism that
			 * sends an upcall and sleeps until the matching downcall
			 * arrives - for testing purposes during development
			 */
			 
			PDEBUG(D_PIOCTL, "capfsdev: initialized test_upwait\n");

			loop_up = kmalloc(sizeof(struct capfs_upcall), GFP_KERNEL);
			if(!loop_up){
				kfree(loop_up);
				return(-ENOMEM);
			}
			loop_up->magic = CAPFS_UPCALL_MAGIC;
			loop_up->type = TEST_UPWAIT_COMM;

			test_req = kmalloc(sizeof(struct capfs_downcall), GFP_KERNEL);
			if(!test_req){
				kfree(test_req);
				kfree(loop_up);
				return(-ENOMEM);
			}
			ret = capfsdev_enqueue(loop_up, test_req);
			if(ret < 0){
				kfree(test_req);
				kfree(loop_up);
				return(ret);
			}
			kfree(test_req);
			kfree(loop_up);
			return(0);
		case(TEST_RD_XFER):
			/* this will exercise the communication mechanism that
			 * can transfer a variable length memory region from one
			 * user program to another.
			 */
			 
			PDEBUG(D_PIOCTL, "capfsdev: initialized test_rd_xfer\n");
			ret = copy_from_user(&rd_xfer_reg, (struct ioctl_mem_reg*)arg, sizeof(struct
				ioctl_mem_reg));

			loop_up = kmalloc(sizeof(struct capfs_upcall), GFP_KERNEL);
			if(!loop_up){
				kfree(loop_up);
				return(-ENOMEM);
			}

			loop_up->magic = CAPFS_UPCALL_MAGIC;
			loop_up->type = TEST_RD_XFER;
			loop_up->xfer.ptr = rd_xfer_reg.ptr;
			loop_up->xfer.size = rd_xfer_reg.size;

			test_req = kmalloc(sizeof(struct capfs_downcall), GFP_KERNEL);
			if(!test_req){
				kfree(test_req);
				kfree(loop_up);
				return(-ENOMEM);
			}
			ret = capfsdev_enqueue(loop_up, test_req);
			if(ret < 0){
				kfree(test_req);
				kfree(loop_up);
				return(ret);
			}
			kfree(test_req);
			kfree(loop_up);
			return(0);

		case(TEST_WR_XFER):
			/* this will exercise the communication mechanism that
			 * can transfer a variable length memory region from one
			 * user program to another.
			 */
			 
			PDEBUG(D_PIOCTL, "capfsdev: initialized test_wr_xfer\n");
			ret = copy_from_user(&rd_xfer_reg, (struct ioctl_mem_reg*)arg, sizeof(struct
				ioctl_mem_reg));

			loop_up = kmalloc(sizeof(struct capfs_upcall), GFP_KERNEL);
			if(!loop_up){
				kfree(loop_up);
				return -ENOMEM;
			}

			loop_up->magic = CAPFS_UPCALL_MAGIC;
			loop_up->type = TEST_WR_XFER;
			loop_up->xfer.ptr = rd_xfer_reg.ptr;
			loop_up->xfer.size = rd_xfer_reg.size;

			test_req = kmalloc(sizeof(struct capfs_downcall), GFP_KERNEL);
			if(!test_req){
				kfree(test_req);
				kfree(loop_up);
				return -ENOMEM;
			}
			ret = capfsdev_enqueue(loop_up, test_req);
			if(ret < 0){
				kfree(test_req);
				kfree(loop_up);
				return ret;
			}
			kfree(test_req);
			kfree(loop_up);
			return 0;


		case(TEST_CHEAT_DEC):
			/* this is for development purposes...
			 * manually decrements the module use count */
			PDEBUG(D_PIOCTL, "capfsdev: manually decrementing module count.\n");
			return 0;
		case INVALIDATE_CACHE:
		{
			struct capfs_invalidate_ioctl *args = (struct capfs_invalidate_ioctl *) arg, karg;
			
			if ((ret = copy_from_user(&karg, args, sizeof(struct capfs_invalidate_ioctl))) != 0) {
				return ret;
			}
			if (karg.type != 0 && karg.type != 1) {
				PERROR("capfsdev: invalid value of type %d\n", karg.type);
				return -EINVAL;
			}
			/* FIXME: Need to invalidate the cache here */
			return 0;
		}
		default:
			return -ENOSYS;
	}
}




/*******************************************************************
 * support functions:
 */


#if 0
/* queue_len()
 *
 * for debugging, (and also capfsdev_poll) see how long the outgoing
 * queue is..
 *
 * As of 7-14-2001 this is no longer being used anywhere.  We are just
 * leaving it in here in case it is needed for debugging purposes.
 */
static int queue_len(struct list_head *my_list){
	struct list_head *check;
	int count = 0;

	spin_lock(&capfsdev_up_lock);
	check = my_list->next;
	while(check != my_list){
		count++;
		check = check->next;
	}
	spin_unlock(&capfsdev_up_lock);
	return count;
}
#endif

/*
 *	Export the signal mask handling for code that
 *	may sleep for the operation to complete.
 *	Only if intr is 0, do we change the process's
 *	signal dispositions, 
 *	save the old dispostion in oldset and return.
 *	Otherwise we don't do anything.
 */
 
static void sig_setmask(sigset_t *oldset, int intr)
{
	/* Always allow SIGKILL */
	unsigned long	sigallow = sigmask(SIGKILL);
	unsigned long	irqflags;
	struct k_sigaction *action;

	/* If it was intr-mounted, then this function is a nop */
	if(intr) {
		return;
	}
	action = current->sighand->action;
	if (action[SIGINT-1].sa.sa_handler == SIG_DFL)
		sigallow |= sigmask(SIGINT);
	if (action[SIGQUIT-1].sa.sa_handler == SIG_DFL)
		sigallow |= sigmask(SIGQUIT);
	spin_lock_irqsave(&current->sighand->siglock, irqflags);
	*oldset = current->blocked;
	siginitsetinv(&current->blocked, sigallow & ~oldset->sig[0]);
	recalc_sigpending();
	spin_unlock_irqrestore(&current->sighand->siglock, irqflags);
	return;
}

/* restore the old signal handling disposition only if intr was not set */

static void sig_setunmask(sigset_t *oldset, int intr)
{
	unsigned long	irqflags;

	/* If it was intr-mounted, then this function is a nop */
	if(intr) {
		return;
	}
	spin_lock_irqsave(&current->sighand->siglock, irqflags);
	current->blocked = *oldset;
	recalc_sigpending();
	spin_unlock_irqrestore(&current->sighand->siglock, irqflags);
	return;
}

/* capfsdev_enqueue()
 *
 * this adds upcalls to the outgoing queue.  it will then wait on a matching
 * downcall before returning.
 *
 * TODO: Optimize better for the no-patch case when we feel like it.
 * Returns 0 on success, -errno on failure.
 *
 * NOTE: We must call sig_setunmask() at all the exit points of this function,
 * else all hell shall break loose for this process.
 * 
 */
int capfsdev_enqueue(struct capfs_upcall *in_upcall, 
						  struct capfs_downcall *out_downcall)
{

	struct capfsdev_upq_entry *q_upcall = NULL;
	struct capfsdev_seq_entry *q_seq = NULL;
	struct capfsdev_seq_entry *q_seq_probe = NULL;
	DECLARE_WAITQUEUE(my_wait, current);
	struct task_struct *tsk = current;
	capfs_seq_t my_seq = 0;	
	int err = 0;
	int mem_xfer = 0;
	void *copybuf = NULL;
	int mem_read = 0;
	struct timeval begin = {0,0}, end = {0,0};
	int invalid_flag = 0, what_signal = 0;
	int intr = 0;
	sigset_t oldset;

	/* capfs_map_buf and rw are only used in the CAPFS_BUFFER_MAPPED case */
	void *capfs_map_buf = NULL;
	int rw = -1;
	capfs_callstat.total++;
	intr = (in_upcall->flags & CAPFS_MOUNT_INTR) ? 1 : 0;

	/* sig_setunmask() MUST BE CALLED AT ALL EXIT POINTS IN THIS FUNCTION */
	sig_setmask(&oldset, intr); 


	/* nr_daemons tells us if there is a user-space capfsd listening for 
	 * upcalls.  If there isn't, we bail out.
	 */
	if (nr_daemons < 1) {
		PERROR("capfsdev: capfsd does not appear to be running.\n");
		sig_setunmask(&oldset, intr);
		return -ENODEV;
	}
	
	q_seq = kmalloc(sizeof(struct capfsdev_seq_entry), GFP_KERNEL);
	if (!q_seq) {
		PERROR("capfsdev: out of memory.\n");
		sig_setunmask(&oldset, intr);
		return -ENOMEM;
	}
	q_seq->invalid = 0;

	/* here we are going to check to see if the upcall involves a
	 * memory transfer- if so we are going to go ahead and set up the
	 * capfs io mapping, if needed.
	 */
	switch (in_upcall->type) {
	case TEST_RD_XFER:
	case READ_OP:
	case WRITE_OP:
	case TEST_WR_XFER:
		/* note that writes are a two step operation.  When we
		 * submit this upcall, the sequence structure needs to be
		 * set so that the step is zero.  
		 */
		if ((in_upcall->xfer.to_kmem != 0) || (is_fake_useraddr())) {
			/* not a user-space buffer; don't map */
			q_seq->kernelbuf.ptr = in_upcall->xfer.ptr;
			q_seq->kernelbuf.size = in_upcall->xfer.size;
			q_seq->kernel_read = 1;
			q_seq->step = 0;
			break;
		}

		/* wait on buffer resources and allocate them.  Mapping of user space
       * buffers into kernel space (in the CAPFS_BUFFER_MAPPED case) is handled
       * a little further down 
		 */
		err = setup_buffer(in_upcall, &copybuf, &capfs_map_buf);
		if (err) {
			PERROR("capfsdev: setup_buffer() failure.\n");
			kfree(q_seq);
			sig_setunmask(&oldset, intr);
			return err;
		}


		/* buffers are all set; let's go. */
		if ((in_upcall->type == WRITE_OP) || (in_upcall->type == TEST_WR_XFER))
		{
			/* file system write operation */
			if (capfs_buffer == CAPFS_BUFFER_MAPPED) {
				rw = CAPFS_BUF_READ; /* only used by the mapped technique */
			}
			else {
				PDEBUG(D_PSDEV, "capfsdev_enqueue: [write] copy %ld bytes data from %p -> %p\n",
						(unsigned long) in_upcall->xfer.size,
						in_upcall->xfer.ptr, copybuf);
				err = copy_from_user(copybuf, in_upcall->xfer.ptr, 
											in_upcall->xfer.size);
				if (err) 
				{
					PERROR("capfsdev: capfs memory transfer failure.\n");
					cleanup_buffer(in_upcall, &copybuf, &capfs_map_buf);
					kfree(q_seq);
					sig_setunmask(&oldset, intr);
					return err;
				}
			}
		}
		else {
			/* file system read operation */
			if (capfs_buffer == CAPFS_BUFFER_MAPPED) {
				rw = CAPFS_BUF_WRITE;
			}
			else {
				/* this is a read xfer we need to handle later */
				mem_read = 1;
			}
		}

		if (capfs_buffer == CAPFS_BUFFER_MAPPED) {
			/* map the user buffer into kernel space and point the queue entry
			 * to the right buffer
			 */
			err = capfs_map_userbuf(rw, in_upcall->xfer.ptr,
										  in_upcall->xfer.size, &capfs_map_buf);
			if (err) {
				PERROR("capfsdev: capfs_map_userbuf() failure.\n");
				cleanup_buffer(in_upcall, &copybuf, &capfs_map_buf);
				kfree(q_seq);
				sig_setunmask(&oldset, intr);
				return err;
			}
			q_seq->iobuf = capfs_map_buf;
		}
		else {
			/* point the queue entry to the right buffer */
			q_seq->iobuf = copybuf;
		}
		q_seq->step = 0;
		q_seq->kernel_read = 0;
		mem_xfer = 1;
		
		break;
	case GETDENTS_OP:
		if (in_upcall->xfer.to_kmem == 0) {
			/* not supported at this time */
			PERROR("capfsdev: user-space address to GETDENTS?\n");
			kfree(q_seq);
			sig_setunmask(&oldset, intr);
			return -EINVAL;
		}
		q_seq->kernelbuf.ptr = in_upcall->xfer.ptr;
		q_seq->kernelbuf.size = in_upcall->xfer.size;
		q_seq->kernel_read = 1;
		
		break;
	case READLINK_OP:
		if (in_upcall->xfer.to_kmem == 0) {
			/* not supported at this time */
			PERROR("capfsdev: user-space address to READLINK?\n");
			kfree(q_seq);
			return -EINVAL;
		}
		q_seq->kernelbuf.ptr = in_upcall->xfer.ptr;
		q_seq->kernelbuf.size = in_upcall->xfer.size;
		q_seq->kernel_read = 1;
		break;
	default:
		/* not a memory transfer operation */
		break;
	}
	

	/* creating the structure to put on the upcall queue */
	q_upcall = kmalloc(sizeof(struct capfsdev_upq_entry), GFP_KERNEL);
	if (!q_upcall) {
		/* TODO */
		PERROR("capfsdev: out of memory.\n");
		if (mem_xfer) cleanup_buffer(in_upcall, &copybuf, &capfs_map_buf);
		kfree(q_seq);
		sig_setunmask(&oldset, intr);
		return -ENOMEM;
	}
	q_upcall->pending_up = *in_upcall;

	/* grab a sequence number atomically */
	my_seq = inc_locked_seq();
	q_upcall->pending_up.seq = my_seq;
	q_seq->seq = my_seq;

	/* atomically modify the sequence number list.  this is done to let us keep
    * sequence numbers around in case an upcall is aborted for some reason 
	 *
	 * (there might be some other reason also???)
    */
	down(&seq_sem);
	/* make sure that we don't have a duplicate of this sequence
	 * number (from an old request) still in the list
	 */
	err = seq_is_pending(&seq_pending, my_seq, &invalid_flag);
	if(err){
		if(invalid_flag) {
			PERROR("capfsdev: stale request purged from seq_pending.\n");
			remove_seq_pending(&seq_pending, my_seq);
		}
		else {
			up(&seq_sem);
			PERROR("capfsdev: seq_pending list overrun - bailing out!\n");
			sig_setunmask(&oldset, intr);
			return(-EINVAL);
		}
	}
	list_add(&(q_seq->seq_list), &seq_pending);
	up(&seq_sem);
	
	/* atomically add ourselves to the upcall queue */
	spin_lock(&capfsdev_up_lock);
	list_add(&(q_upcall->req_list), &up_pending);
	spin_unlock(&capfsdev_up_lock);

	/* wake up processes listening on capfsdev for requests (capfsd) */
	wake_up_interruptible(&capfsdev_waitq);
	
	/* now I want to wait here until a downcall comes in 
	 * that matches the sequence number.  i stay on the waitqueue until 
	 * I explicitly remove myself.
	 */
	my_wait.task = tsk;
	add_wait_queue(&capfsdev_seq_wait, &my_wait);
	/* this variable is set thru the sysctl interface */
	if(capfs_upcall_timestamping) {
		 do_gettimeofday(&begin);
	}
seq_repeat:
	set_current_state(TASK_INTERRUPTIBLE);

	/* this lets this mechanism be interrupted if needed */
	/* but don't interrupt a hint operation!  This is necessary
	 * to correctly close sockets and such on program exit.
	 */
	/* check for bad signals.  if we got one, bail out gracefully */
	if ((check_for_bad_signals(&what_signal))) {
		PDEBUG(D_PSDEV, "Process %d inside capfsdev (capfsdev_enqueue): recieved signal %d- bailing out.\n", current->pid, what_signal);
		/* need to do something reasonable here... */
		/* so subsequent calls don't freak out :) */
		
		/* at least need to dequeue above stuff and make sure the
		 * module use count is correct
		 */
		remove_wait_queue(&capfsdev_seq_wait, &my_wait);
		set_current_state(TASK_RUNNING);

		/* how far along did the capfsd get on this operation?  Three
		 * scenarios could occur:
		 *
		 * 1) The capfsd has not read the upcall yet.  We need to dequeue
		 * the upcall and then dequeue and destroy the sequence entry that
		 * matches it.
		 *
		 * 2) The capfsd has read the upcall, but has not yet produced a
		 * downcall in response.  That means  that we should invalidate 
		 * the sequence number.  The capfsdev_write function
		 * will detect that the sequence was invalidated and silently eat
		 * the matching downcall.
		 *
		 * 3) The capfsd has read the upcall, and has already completed the
		 * downcall response. In this case, we need to get the
		 * downcall out of the queue and destroy it before we return.
		 */

		/* see if we can find the upcall and remove it */
		err = up_dequeue(&up_pending, my_seq);
		if(err == 0) {
			/* the upcall has not been read */
			/* scenario 1. */
			down(&seq_sem);
			remove_seq_pending(&seq_pending, my_seq);
			up(&seq_sem);
		}
		else {
			/* the upcall has been read */
			down(&seq_sem);
			/* has the downcall completed? */
			q_seq_probe = match_seq_entry(&seq_pending, my_seq);
			if(q_seq_probe){
				/* downcall not completed */
				/* scenario 2 */
				invalidate_seq_pending(&seq_pending, my_seq);
			}
			else{
				/* downcall has completed */
				/* scenario 3 */
				down_dequeue(&down_pending, my_seq, out_downcall); 
			}

			up(&seq_sem);
		}

		if (mem_xfer) cleanup_buffer(in_upcall, &copybuf, &capfs_map_buf);
		if(capfs_upcall_timestamping) {
			 do_gettimeofday(&end);
			 if (end.tv_usec < begin.tv_usec) {
				 end.tv_usec += 1000000; end.tv_sec--;
			 }
			 end.tv_sec  -= begin.tv_sec;
			 end.tv_usec -= begin.tv_usec;
			 /* update the upcall statistics */
			 capfs_upcall_stats(in_upcall->type, (end.tv_sec * 1000000 + end.tv_usec));
		}
		sig_setunmask(&oldset, intr);
		return -EINTR;
	}
	
	/* here I need to check the seq_pending list and see if my sequence
	 * number has been removed, indicating a matching downcall.  if not we call
	 * schedule and go back to waiting.
	 */
	down(&seq_sem);
	err = seq_is_pending(&seq_pending, my_seq, NULL);
	up(&seq_sem);
	if (err) {
		schedule();
		goto seq_repeat;
	}
	remove_wait_queue(&capfsdev_seq_wait, &my_wait);
	set_current_state(TASK_RUNNING);
	if(capfs_upcall_timestamping) {
		 do_gettimeofday(&end);
		 if (end.tv_usec < begin.tv_usec) {
			 end.tv_usec += 1000000; end.tv_sec--;
		 }
		 end.tv_sec  -= begin.tv_sec;
		 end.tv_usec -= begin.tv_usec;
		 /* update the upcall statistics */
		 capfs_upcall_stats(in_upcall->type, (end.tv_sec * 1000000 + end.tv_usec));
	 }


	/* now I need to pull my matched downcall off of the downcall
	 * list and return
	 */
	err = down_dequeue(&down_pending, my_seq, out_downcall); 
	if (err < 0) {
		if (mem_xfer) cleanup_buffer(in_upcall, &copybuf, &capfs_map_buf);
		sig_setunmask(&oldset, intr);
		return err;
	}

	/* if the operation was a read, copy the data received from the daemon
	 * into the user buffer if necessary (i.e. not mapped)
	 */
   if (mem_read && (capfs_buffer == CAPFS_BUFFER_DYNAMIC 
						  || capfs_buffer == CAPFS_BUFFER_STATIC)) 
	{
		PDEBUG(D_PSDEV, "capfsdev_enqueue: [read] copy %ld bytes data from %p -> %p\n",
				(unsigned long) in_upcall->xfer.size,
				copybuf, in_upcall->xfer.ptr);
		err = copy_to_user(in_upcall->xfer.ptr, copybuf, in_upcall->xfer.size);
	}

   /* if we did a transfer, clean up the buffer and wake people up */
	if (mem_xfer) {
		PDEBUG(D_PSDEV, "capfsdev_enqueue: cleaning up mem xfer buffers %d\n", err);
		cleanup_buffer(in_upcall, &copybuf, &capfs_map_buf);
	}
	sig_setunmask(&oldset, intr);
	return err;
}

/* setup_buffer()
 *
 * Used to allocate buffer space in the dynamic case, or to grab the
 * static_buf_sem semaphore in the static case.  We don't map the user buffer
 * for the mapped case yet, as there are some additional checks that must
 * first be made (SHOULD WE MOVE THE MAPPING INTO THIS FUNCTION?)
 *
 * The first half of this function handles checking resource limitations in
 * the mapped and dynamic case.  
 *
 * NOTE: capfs_map_bufp is currently unused, as we're not doing the mapping in
 * here yet.
 */
static int setup_buffer(struct capfs_upcall *in_upcall,
								void **copybufp, void **capfs_map_bufp)
{
	int what_signal = 0;
	struct task_struct *tsk = current;
	DECLARE_WAITQUEUE(my_wait, current);

	/* This following bit of code makes sure that the sum of 
	 * all of the buffers used for the MAPPED or DYNAMIC buffers
	 * doesn't exceed the maxsz defined at module load time.
	 */
	if (capfs_buffer == CAPFS_BUFFER_DYNAMIC 
		 || capfs_buffer == CAPFS_BUFFER_MAPPED) 
	{
		
		spin_lock(&capfsdev_buffer_pool_lock);
		if ((capfsdev_buffer_size + in_upcall->xfer.size) <= capfs_maxsz) {
			capfsdev_buffer_size += in_upcall->xfer.size;
			spin_unlock(&capfsdev_buffer_pool_lock);
		}
		else {
			spin_unlock(&capfsdev_buffer_pool_lock);
			/* uh oh, we have exceeded the max buffer size available */
			PDEBUG(D_SPECIAL, "exceeded buffer size: sleeping.\n");	
			my_wait.task = tsk;
			add_wait_queue(&capfsdev_buffer_pool_wait, &my_wait);
		dynamic_repeat:
			set_current_state(TASK_INTERRUPTIBLE);
			
			/* check for bad signals and bail out if we got one */
			if ((check_for_bad_signals(&what_signal))) {
				PDEBUG(D_PSDEV, "Process %d inside capfsdev (setup_buffer): recieved signal %d- bailing out.\n", current->pid, what_signal);
				remove_wait_queue(&capfsdev_buffer_pool_wait, &my_wait);
				set_current_state(TASK_RUNNING);
				return -EINTR; 
			}
				
			/* now we can look again to see if we can allocate a buffer */
			spin_lock(&capfsdev_buffer_pool_lock);
			if ((capfsdev_buffer_size + in_upcall->xfer.size) > capfs_maxsz) {
				spin_unlock(&capfsdev_buffer_pool_lock);
				schedule();
				/* go back to sleep if we can't */
				goto dynamic_repeat;
			}
			
			/* the memory is available- let's rock! */
			capfsdev_buffer_size += in_upcall->xfer.size;
			spin_unlock(&capfsdev_buffer_pool_lock);
			
			remove_wait_queue(&capfsdev_buffer_pool_wait, &my_wait);
			set_current_state(TASK_RUNNING);
		}
	}
	
	/* Here we allocate our buffer for the dynamic copy, or simply
	 * set the pointer to point to the static buffer in the static
	 * case.  This value isn't used by the mapped buffer code.
	 */
	if (capfs_buffer == CAPFS_BUFFER_DYNAMIC) 
	{
		*copybufp = (void*)vmalloc(in_upcall->xfer.size);

		if (*copybufp == NULL) 
		{
			PERROR("capfsdev: could not allocate memory!\n");
			spin_lock(&capfsdev_buffer_pool_lock);
			capfsdev_buffer_size -= in_upcall->xfer.size;
			spin_unlock(&capfsdev_buffer_pool_lock);
			return -ENOMEM;
		}
		PDEBUG(D_PSDEV, "setup_buffer (dynamic) returning %p\n", *copybufp);
	}
	else if (capfs_buffer == CAPFS_BUFFER_STATIC) {
		/* go ahead and decrement the semaphore while we are at it */
		down(&static_buf_sem);
		*copybufp = static_buf;
		PDEBUG(D_PSDEV, "setup_buffer (static) returning %p\n", *copybufp);
	}

	/* this would be the place to map the user buffer if we wanted to
	 * do it in this function.
	 */

	return 0;
}


/* cleanup_buffer()
 *
 * Used to deallocate buffer space in the dynamic case, or to unmap user
 * buffers in the mapped case, or to release the static_buf_sem semaphore in
 * the static case.
 *
 * This function also handles waking up other processes waiting on these
 * resources.  Both the mapped and dynamic schemes use the
 * capfsdev_buffer_pool_wait wait queue to wait on resources to be freed, so we
 * wake up the guys on that queue in here.  
 */
static void cleanup_buffer(struct capfs_upcall *in_upcall,
											 void **copybufp, void **capfs_map_bufp)
{
	if (capfs_buffer == CAPFS_BUFFER_MAPPED) {
		if (*capfs_map_bufp) {
			capfs_unmap_userbuf(*capfs_map_bufp);
			*capfs_map_bufp = NULL;
		}
		spin_lock(&capfsdev_buffer_pool_lock);
		capfsdev_buffer_size -= in_upcall->xfer.size;
		spin_unlock(&capfsdev_buffer_pool_lock);
		wake_up_interruptible(&capfsdev_buffer_pool_wait);
	}
	else if (capfs_buffer == CAPFS_BUFFER_DYNAMIC) {
		if (*copybufp) 
		{
			PDEBUG(D_PSDEV, "cleanup_buffer (dynamic) %p\n", *copybufp);
			vfree(*copybufp);
			*copybufp = NULL;
		}
		spin_lock(&capfsdev_buffer_pool_lock);
		capfsdev_buffer_size -= in_upcall->xfer.size;
		spin_unlock(&capfsdev_buffer_pool_lock);
		wake_up_interruptible(&capfsdev_buffer_pool_wait);
	}
	else if (capfs_buffer == CAPFS_BUFFER_STATIC) {
		up(&static_buf_sem);
	}
	return;
}

/* check_for_bad_signals()
 *
 * This function checks for pending signals after a process has woken up.
 *
 * Returns 0 if everything is ok, -EINTR otherwise and the signal number in sig_nr.
 */
static int check_for_bad_signals(int *sig_nr)
{
	int i;
	if (signal_pending(current)) {
		/* 
		 *  This is really ugly. I dont understand what to substitute instead of 32.
		 *  In fact 32 is correct only on x86 and ia64 as far as user-space is concerned
		 */
		for(i = 1; i <= 32; i++) {
			if(sigismember(&(current->pending.signal), i)) {
				*sig_nr = i;
				return -EINTR;
			}
		}
		/* give up if you cannot find out */
		*sig_nr = -1;
		/* Is this possible at all?? */
		PERROR("check_signal says signal_pending, but could not find out which signal!!\n");
		return -EINTR; 
	}
	return 0;
}


/* capfsdev_enqueue_noresponse()
 *
 * This function is used to queue an upcall for which there will be no
 * matching downcall.  This is indicated to the daemon by a -1 sequence
 * number, which is stored in the queue structure (q_upcall). 
 */
int capfsdev_enqueue_noresponse(struct capfs_upcall *in_upcall)
{	
	struct capfsdev_upq_entry *q_upcall;

   /* nr_daemons tells us if there is a user-space capfsd listening for 
	 * upcalls.  If there isn't, we bail out.
	 */
	if (nr_daemons < 1) {
		return -ENODEV;
	}

	q_upcall = kmalloc(sizeof(struct capfsdev_upq_entry), GFP_KERNEL);
	if(!q_upcall){
		return -ENOMEM;
	}

	/* this q_upcall structure is the element that sits in the queue */
	q_upcall->pending_up = *in_upcall;
	q_upcall->pending_up.seq = -1;
	
	/* add this upcall to the queue */
	spin_lock(&capfsdev_up_lock);
	list_add(&(q_upcall->req_list), &up_pending);
	spin_unlock(&capfsdev_up_lock);
	
	/* wake up processes reading or selecting on our device */
	wake_up_interruptible(&capfsdev_waitq);
	
	return 0;
}


/* seq_is_pending()
 *
 * use this to find out if a sequence number is in the seq list
 *
 * now the invalid integer will be set to alert the caller that they
 * have matched a sequence number that has been declared invalid.
 * if they caller does not want to know about that they should set
 * that pointer to NULL
 *
 * IMPORTANT: this function assumes that the caller holds a semaphore
 * protecting the sequence list!
 */
static int seq_is_pending(struct list_head *my_list, capfs_seq_t my_seq,
								  int *invalid)
{
	struct list_head *check;
	struct capfsdev_seq_entry *entry;
	
	for (check = my_list->next; check != my_list; check = check->next)
   {
		entry = list_entry(check, struct capfsdev_seq_entry, seq_list);

		if (entry->seq == my_seq) {
			if (invalid != NULL) {
				*invalid = entry->invalid;
			}
			return 1;
		}
	}
	return 0;
}


/* down_dequeue()
 *
 * use this to figure out which downcall to dequeue based on which
 * one matches your sequence number-return null on error...
 *
 * now also returns the fs from the downcall side :)
 *
 * Returns 0 on success, -errno on failure.
 */
static int down_dequeue(struct list_head *my_list, capfs_seq_t my_seq,
								struct capfs_downcall *out_downcall)
{ 
	struct list_head *check;
	struct capfsdev_downq_entry *entry;

	spin_lock(&capfsdev_down_lock);

	/* iterate through list of entries looking for sequence number;
	 * we expect it to be near the head of the list 
    */
	for (check = my_list->next; check != my_list; check = check->next) 
	{
		entry = list_entry(check, struct capfsdev_downq_entry, req_list);

		if (entry->pending_down.seq == my_seq) {
			/* found it.  return copy of downcall, remove entry from list, 
			 * free downcall entry structure, unlock list, and return.
			 */
			*out_downcall = entry->pending_down;
			list_del(check);
			kfree(entry);
			/* we don't have to kfree() pending_down because it is a part of the
			 * capfsdev_downq_entry structure
			 */
			spin_unlock(&capfsdev_down_lock);
			return 0;
		}
	}

	/* looked through the whole list and didn't find a match */
	spin_unlock(&capfsdev_down_lock);

	PERROR("capfsdev: didn't find matching downcall in queue!\n");
	return -EINVAL;
}
		

/* capfsdev_down_enqueue()
 *
 * this adds downcalls to the incoming queue
 *
 * IMPORTANT!  This function assumes that the caller holds a semaphore
 * protecting the sequence list before it is called!
 *
 * Returns 0 on success, -errno on failure.
 */
static int capfsdev_down_enqueue(struct capfs_downcall *in_downcall)
{
	struct capfsdev_downq_entry *q_downcall;
	int ret = -1;

	/* we also need to handle removing the sequence number from the
	 * list of pending sequence numbers for upcall/downcall pairs
	 */

	q_downcall = kmalloc(sizeof(struct capfsdev_downq_entry), GFP_KERNEL);
	if(!q_downcall){
		return -ENOMEM;
	}
	
	/* make a copy of the downcall */
	q_downcall->pending_down = *in_downcall;

	spin_lock(&capfsdev_down_lock);
	list_add(&(q_downcall->req_list), &down_pending);
	spin_unlock(&capfsdev_down_lock);

	/* take the sequence number out of the sequence number list */
	ret = remove_seq_pending(&seq_pending, in_downcall->seq);
	if(ret < 0){
		PDEBUG(D_DOWNCALL, "capfsdev: remove of seq_entry failed... \n");
	}

	/* wake stuff up - see poll function */
	wake_up_interruptible(&capfsdev_seq_wait);
	
	return 0;
}


/* remove_seq_pending()
 *
 * use this to remove a sequence number from the pending sequence
 * list
 *
 * IMPORTANT!  This function assumes that the caller already holds a
 * semaphore to protect the sequence list!
 *
 * Returns 0 on success, -errno on failure.
 */
static int remove_seq_pending(struct list_head *my_list, capfs_seq_t my_seq)
{
	struct list_head *check;
	struct capfsdev_seq_entry *entry;

	for (check = my_list->next; check != my_list; check = check->next) 
	{
		entry = list_entry(check, struct capfsdev_seq_entry, seq_list);

		if (entry->seq == my_seq) {
			list_del(check);
			kfree(entry);
			return 0;
		}
	}

	return -EINVAL;
}


/* inc_locked_seq()
 *
 * this simply increments the communication sequence number and
 * returns its new value.
 */
static capfs_seq_t inc_locked_seq()
{
	capfs_seq_t ret = 0;

	spin_lock(&capfsdev_current_seq_lock);
	if (current_seq < CAPFSDEV_MAX_SEQ) {
		current_seq++;
	}
	else {
		current_seq = 1;
	}
	ret = current_seq;
	spin_unlock(&capfsdev_current_seq_lock);

	return ret;
}


/* up_dequeue()
 *
 * this function dequeues specific pending upcalls- mostly used for
 * cleanup mechanisms
 *
 * Returns 0 on success, -errno on failure.
 */
static int up_dequeue(struct list_head *my_list, capfs_seq_t my_seq)
{ 
	struct list_head *check;
	struct capfsdev_upq_entry *entry;
	
	spin_lock(&capfsdev_up_lock);

	/* iterate through list of entries looking for matching sequence number.
	 * we go through the list from oldest to newest, because we expect we're 
	 * looking for an old entry.
	 */
	for (check = up_pending.prev; check != &up_pending; check = check->prev) 
	{
		entry = list_entry(up_pending.prev, struct capfsdev_upq_entry, req_list);

		if (entry->pending_up.seq == my_seq) {
			list_del(up_pending.prev);
			kfree(entry);
			spin_unlock(&capfsdev_up_lock);
			return 0;
		}
	}

	spin_unlock(&capfsdev_up_lock);
	return -EINVAL;
}


/* match_seq_entry()
 *
 * returns a pointer to the seq_entry with a matching sequence number,
 * or NULL if not found in the list.
 *
 * IMPORTANT! This function assumes that the caller holds a sequence
 * list semaphore when calling this function.
 */
static struct capfsdev_seq_entry *match_seq_entry(struct list_head *my_list, 
																 capfs_seq_t my_seq)
{

	struct list_head *check;
	struct capfsdev_seq_entry *entry;

	for (check = my_list->next; check != my_list; check = check->next) 
	{
		entry = list_entry(check, struct capfsdev_seq_entry, seq_list);

		if (entry->seq == my_seq) {
			return entry;
		}
	}

	return NULL;
}

/* invalidate_seq_pending()
 *
 * this function will be used to set the invalid flag in a sequence
 * list entry. future operations on that sequence will be
 * aware that a previous step in the transfer has failed.
 *
 * IMPORTANT!  This function assumes that the caller is holding a
 * semaphore to protect the sequence list.
 *
 * Returns 0 on success, -errno on failure.
 */
static int invalidate_seq_pending(struct list_head *my_list, capfs_seq_t my_seq)
{

	struct list_head *check;
	struct capfsdev_seq_entry *entry;

	for (check = my_list->next; check != my_list; check = check->next) 
	{
		entry = list_entry(check, struct capfsdev_seq_entry, seq_list);

		if (entry->seq == my_seq) {
			entry->invalid = 1;
			return 0;
		}
	}

	return -EINVAL;
}

/* is_fake_useraddr()
 *
 * This function tests for the case where someone has used the set_fs()
 * trick to pass a kernel address to a function that normally takes a
 * user address.
 *
 * Returns 0 if this appears to be a real user address, non-zero if not.
 */
static int is_fake_useraddr(void)
{
	mm_segment_t check_fs;
	mm_segment_t check_ds;

	check_fs = get_fs();
	check_ds = get_ds();
	
	if (check_fs.seg == check_ds.seg) {
		PDEBUG(D_SPECIAL, "is_fake_useraddr detected fake address\n");
		return 1;
	}
	return 0;
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



