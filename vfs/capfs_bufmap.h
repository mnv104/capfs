#ifndef __CAPFS_BUFMAP_H
#define __CAPFS_BUFMAP_H
/*
 * capfs_bufmap.h copyright (c) 1999 Clemson University, all rights reserved.
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

/* Prototypes and defines for exported functions in capfs_bufmap.c
 */

int capfs_map_userbuf(int rw, char *buf, size_t size, void **bufp);
void capfs_unmap_userbuf(void *buf);
unsigned long capfs_copy_to_userbuf(void *buf, const void *user_from,
	unsigned long size);
unsigned long capfs_copy_from_userbuf(const void *user_to, void *buf,
	unsigned long size);

/* Kernighan and Pike say use the enum, and who am I to disagree? */
enum {
	CAPFS_BUF_READ = 0,
	CAPFS_BUF_WRITE = 1
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
