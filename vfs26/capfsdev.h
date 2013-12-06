#ifndef __CAPFSDEV_H
#define __CAPFSDEV_H

/*
 * copyright (c) 2005 Murali Vilayannur, all rights reserved.
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
 * Contact:  Murali Vilayannur    vilayann@cse.psu.edu
 */


/******************************************
 * These are the capfsdev functions exported to the kernel
 * to enable messaging with the user level daemon.
 ******************************************/

extern int64_t total_service_time;
int capfsdev_enqueue(struct capfs_upcall *in_upcall, 
						  struct capfs_downcall *out_downcall);

int capfsdev_enqueue_noresponse(struct capfs_upcall *in_upcall);

#endif /* __CAPFSDEV_H */

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 */




