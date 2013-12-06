#ifndef __CAPFSDEV_H
#define __CAPFSDEV_H

/*
 * copyright (c) 1999 Rob Ross and Phil Carns, all rights reserved.
 *
 * Written by Rob Ross and Phil Carns.
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


/******************************************
 * These are the capfsdev functions exported to the kernel
 * to enable messaging with the user level daemon.
 ******************************************/

/* this allows the kernel to send a single upcall through the capfsdev
 * device- will wait for a returning downcall only if the flag is set */
int capfsdev_enqueue(struct capfs_upcall *in_upcall, 
						  struct capfs_downcall *out_upcall);

int capfsdev_enqueue_noresponse(struct capfs_upcall *in_upcall);
#ifdef __KCAPFSD__
extern unsigned int capfsdev_poll(void);
extern int capfsdev_ioctl(unsigned int cmd, unsigned long arg);
extern int capfsdev_open(void);
extern int capfsdev_release(void);
extern ssize_t capfsdev_read(struct capfs_upcall *upcall, size_t size,long timeout);
extern ssize_t capfsdev_write(struct capfs_downcall *downcall, size_t size,long timeout);
extern void *static_buf;
extern atomic_t stop_new_requests;
#endif    
extern int64_t total_service_time;

#endif /* __CAPFSDEV_H */

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 */


