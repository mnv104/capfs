#ifndef __CAPFS_KERNEL_CONFIG_H
#define __CAPFS_KERNEL_CONFIG_H

/*
 * capfs_kernel_config.h copyright (c) 2005 Murali Vilayannur
 * all rights reserved.
 *
 * Written by Murali Vilayannur
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
 * Contact:  Murali Vilayannur vilayann@cse.psu.edu
 */

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/moduleparam.h>
#include <linux/vermagic.h>
#include <linux/statfs.h>
#include <linux/buffer_head.h>
#include <linux/backing-dev.h>
#include <linux/mpage.h>
#include <linux/namei.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/uio.h>
#include <linux/stat.h>
#include <linux/ctype.h>
#include <linux/bitops.h>
#include <linux/sched.h>
#include <linux/utsname.h>
#include <linux/mm.h>
#include <asm/atomic.h>
#include <linux/smp_lock.h>
#include <linux/wait.h>
#include <linux/dcache.h>
#include <linux/pagemap.h>
#include <linux/poll.h>
#include <linux/smp_lock.h>
#include <linux/seq_file.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/signal.h>
#include <linux/sysctl.h>
#include <linux/time.h>
#include <linux/dcache.h>
#include <linux/proc_fs.h>
#include <linux/mount.h>

/*
  this attempts to disable the annotations used by the 'sparse' kernel
  source utility on systems that can't understand it by defining the
  used annotations away
*/
#ifndef __user
#define __user
#endif

/* taken from include/linux/fs.h from 2.4.19 or later kernels */
#ifndef MAX_LFS_FILESIZE
#if BITS_PER_LONG == 32
#define MAX_LFS_FILESIZE     (((u64)PAGE_CACHE_SIZE << (BITS_PER_LONG))-1)
#elif BITS_PER_LONG == 64
#define MAX_LFS_FILESIZE     0x7fffffffffffffff
#endif
#endif /* MAX_LFS_FILESIZE */

/* ugly hack needed for gcc 3.4.X and broken kernel headers */
#if __GNUC__ > 3
#ifndef __attribute_used__
#define __attribute_used__ __attribute((__used__))
#endif
#elif __GNUC__ == 3
#if  __GNUC_MINOR__ >= 3
#ifndef __attribute_used__
# define __attribute_used__   __attribute__((__used__))
#endif
#else
#ifndef __attribute_used__
# define __attribute_used__   __attribute__((__unused__))
#endif
#endif /* __GNUC_MINOR__ >= 3 */
#elif __GNUC__ == 2
#ifndef __attribute_used__
#define __attribute_used__ __attribute__((__unused__))
#endif
#else
#ifndef __attribute_used__
#define __attribute_used__ /* not implemented */
#endif
#endif /* __GNUC__ */


#endif /* __KERNEL__ */

/* DEBUGGING VALUES */
enum {
	D_SUPER = 1,  /* superblock calls */
	D_INODE = 2,  /* inode calls */
	D_FILE = 4,  /* file calls */
	D_DIR = 8,  /* directory calls */
	D_LLEV = 16, /* low level calls */
	D_LIB = 32,
	D_UPCALL = 64,
	D_PSDEV = 128,
	D_PIOCTL = 256,
	D_SPECIAL = 512,
	D_TIMING = 1024,
	D_DOWNCALL = 2048,
	D_ENTRY = 4096,
	D_CAPFSD = 8192,
	D_MOD = 16384,
	D_ALLOC = 32768,
	D_DEBUG = 65536,
};
 
#ifdef __KERNEL__
#define PDEBUG(mask, format...)                                \
  do {                                                            \
    if (capfs_debug & mask) {                                      \
      printk("(%s, %d): ",  __FILE__, __LINE__);                  \
      printk(format);                                       \
	 }                                                             \
  } while (0) ;
#define PERROR(format...)                                      \
  do {                                                            \
    printk("(%s, %d): ",  __FILE__, __LINE__);                    \
    printk(format);                                         \
  } while (0) ;
#define PENTRY                                                    \
  if(capfs_debug & D_ENTRY)                                        \
	 printk("Process %d entered %s\n",current->pid,__FUNCTION__)
#define PEXIT                                                     \
  if(capfs_debug & D_ENTRY)                                        \
    printk("Process %d leaving %s\n",current->pid,__FUNCTION__) 

#else
#define PDEBUG(mask, format...)                                \
  do {                                                            \
    if (capfs_debug & mask) {                                      \
      printf("(%s, %d): ",  __FILE__, __LINE__);                  \
      printf(format);                                       \
    }                                                             \
  } while (0) ;
#define PERROR(format...)                                      \
  do {                                                            \
    printf("(%s, %d): ",  __FILE__, __LINE__);                    \
    printf(format);                                         \
	 fflush(stdout);                                               \
  } while (0) ;
#endif

/* Buffer allocation values:
 * CAPFS_BUFFER_STATIC - allocates a static buffer and holds it for the
 *   duration the module is loaded
 * CAPFS_BUFFER_DYNAMIC - allocates a memory region in the kernel for the
 *   duration of each request
 * CAPFS_BUFFER_MAPPED - maps the user's buffer into kernel space,
 *   eliminating additional memory use and a copy
 */
enum {
	CAPFS_BUFFER_STATIC = 1,
	CAPFS_BUFFER_DYNAMIC = 2,
	CAPFS_BUFFER_MAPPED = 3
};

/* Values:
 * CAPFS_DEFAULT_IO_SIZE - default buffer size to service at one time.  This
 *   is used to prevent very large transfers from grabbing all available
 *   physical memory.
 * CAPFS_DEFAULT_BUFFER - default buffering technique in the kernel.
 * CAPFS_OPT_IO_SIZE - value returned on statfs; determines the transfer
 *   size used by many common utilities?  Well, maybe not...we still
 *   seem to be getting 64K transfers.
 * CAPFS_DEFAULT_DEBUG_MASK - used in capfs_mod.c and capfsd.c as the
 *   default mask for debugging.  This can be changed for the module with
 *   the "debug" parameter on insmod.
 * CAPFSD_NOFILE - not currently used; will eventually be the number of
 *   file descriptors the capfsd allows itself to use
 * CAPFSD_MAJOR - major number for capfsd device file
 */
enum {
	CAPFS_DEFAULT_IO_SIZE = 16*1024*1024,
	CAPFS_MIN_IO_SIZE = 64*1024,
	CAPFS_DEFAULT_BUFFER = CAPFS_BUFFER_DYNAMIC,
	CAPFS_OPT_IO_SIZE = 128*1024, 
	CAPFS_DEFAULT_DEBUG_MASK = 0,
	CAPFSD_NOFILE = 65536,
	CAPFSD_MAJOR = 61 /* experimental number */
};

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
