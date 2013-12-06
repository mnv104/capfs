#ifndef __CAPFS_KERNEL_CONFIG_H
#define __CAPFS_KERNEL_CONFIG_H

/*
 * capfs_kernel_config.h copyright (c) 1999 Rob Ross and Phil Carns.
 * all rights reserved.
 *
 * Written by Rob Ross and Phil Carns.
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

/* Kernel configuration issues */
#ifdef __KERNEL__

#include "capfs-header.h"
/* Some of these conditional statements were borrowed from Don Becker's
 * 3c509.c module code.
 */
#include <linux/config.h>
#include <linux/version.h>

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

#ifdef CONFIG_MODVERSIONS
#include <linux/modversions.h>
#endif

/* This include moved down to ensure that it follows the inclusion of
 * modversions.h if it is used.  Otherwise we run into symbol problems
 * on new 2.4 kernels
 */
#include <linux/module.h>

#endif /* __KERNEL__ */

/* Decide if we are going to be able to support mapping user buffers
 * into kernel space or not.  This should always work on 2.4 kernels
 * unless HIGHMEM is enabled */

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
	CAPFS_DEFAULT_IO_SIZE = 64*1024*1024,
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
