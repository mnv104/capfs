#ifndef _CAPFS_MOUNT_H
#define _CAPFS_MOUNT_H

/*
 * new mount options for capfs copyright (c) 2005 Murali Vilayannur. 
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

#include <linux/types.h>

#ifndef __KERNEL__
#include <netinet/in.h>
#endif

/* data structures */

enum {
	CAPFS_MGRLEN = 1024,
	CAPFS_DIRLEN = 1024,
	CAPFS_CONSLEN = 256,
};

/* capfs_mount - capfs-specific data passed in during mount process */
struct capfs_mount {
	uint32_t info_magic;
	uint32_t flags;
	uint32_t port;
	uint32_t tcp;
	/* What consistency semantics do we want? */
	char cons[CAPFS_CONSLEN];
	char mgr[CAPFS_MGRLEN];
	char dir[CAPFS_DIRLEN];
};

#define MTAB "/etc/mtab"
#define TMP_MTAB "/etc/mtab.capfs"

/* Bits in the flags field */
#define CAPFS_MOUNT_INTR   0x0001 /* 1 */
#define CAPFS_MOUNT_HCACHE 0x0002 /* 2 */
#define CAPFS_MOUNT_DCACHE 0x0004 /* 3 */
#define CAPFS_MOUNT_TCP 	0x0008 /* 4 */


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
