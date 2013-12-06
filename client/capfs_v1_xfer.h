#ifndef __CAPFS_V1_XFER_H
#define __CAPFS_V1_XFER_H

/*
 * capfs_v1_xfer.h
 *
 * copyright (c) 2005 Murali Vilayannur
 *
 * copyright (c) 1999 Rob Ross and Phil Carns,
 * all rights reserved.
 *
 * Modified heavily for capfs by Murali Vilayannur (vilayann@cse.psu.edu) 
 *
 * Originally written by Rob Ross and Phil Carns.
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
 * Contact:  Rob Ross   rbross@parl.clemson.edu
 *           Phil Carns pcarns@parl.clemson.edu
 */

#include "ll_capfs.h"

/* capfs_comm_init()
 *
 * Sets up any necessary data structures for use in CAPFS data transfer.
 * Should be called once before any transfers take place.
 *
 * Returns 0 on success, or -errno on failure.
 */
int capfs_comm_init(void);

/* do_capfs_op(op, resp)
 *
 * Performs a CAPFS operation.  See ll_capfs.h for the valid operation
 * types and the structures involved.
 *
 * Returns 0 on success, -errno on failure.
 */
int do_capfs_op(struct capfs_upcall *op, struct capfs_downcall *resp);

/* capfs_comm_shutdown()
 *
 * Performs any operations necessary to cleanly shut down CAPFS
 * communication.  Should be called once after all desired communication
 * is complete.
 */
void capfs_comm_shutdown(void);

/* capfs_comm_idle()
 *
 * Performs operations associated with closing idle connections and open
 * files that are no longer in use.
 */
void capfs_comm_idle(void);

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 */

#endif
