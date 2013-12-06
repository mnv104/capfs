/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/* INCLUDES */
#include <alist.h>
#include <jlist.h>
#include <string.h>
#include <log.h>

/* PRUNE_ALIST() - Searches an access list for accesses matching
 * the provided socket and type, and removes any accesses that would
 * result in more than n_size bytes of data transfer of the given type
 * on the given socket
 *
 * PARAMETERS:
 * al_p   - pointer to access list to prune
 * t      - type of access (defined in alist.h)
 * s      - socket # for accesses
 * o_size - old job size for this socket
 * n_size - new job size for this socket (desired size)
 *
 * NOTE: This function is dependent on the access list implementation!!!
 *
 * Returns 0 on success, -1 on failure.
 */
int prune_alist(alist_p al_p, int t, int s, int o_size, int n_size)
{
	int t_size=0;
	llist_p l_p;
	if (n_size < 0 || o_size < n_size || !al_p || !al_p->next) return(-1);
	if (o_size == n_size) return(0);

	l_p = (llist_p)al_p->next; /* first item in list */
	while (l_p && t_size < n_size) {
		ainfo_p a_p = (ainfo_p)l_p->item; /* only for clarity */
		if (a_p->type == t && a_p->sock == s) {
			switch(t) {
				case A_READ:
				case A_WRITE:
					t_size += a_p->u.rw.size;
					if (t_size > n_size) a_p->u.rw.size -= t_size-n_size;
					break;
			}
		}
		l_p = l_p->next;
	}
	while (l_p) {
		llist_p tmpl_p = l_p;
		l_p = l_p->next;
		if (((ainfo_p)tmpl_p->item)->type == t
		&&  ((ainfo_p)tmpl_p->item)->sock == s)
			a_ptr_rem(al_p, (ainfo_p)tmpl_p->item);
	}
	return(0);
}

/* PRUNE_ALIST_AND_ZERO_MEM() - Searches an access list for accesses matching
 * the provided socket and type, and removes any accesses that would
 * result in more than n_size bytes of data transfer of the given type
 * on the given socket.  All memory regions that would have received
 * data are zero'd.
 *
 * PARAMETERS:
 * al_p   - pointer to access list to prune
 * t      - type of access (defined in alist.h)
 * s      - socket # for accesses
 * o_size - old job size for this socket
 * n_size - new job size for this socket (desired size)
 *
 * NOTE: This function is dependent on the access list implementation!!!
 *
 * Returns 0 on success, -1 on failure.
 */
int prune_alist_and_zero_mem(alist_p al_p, int t, int s, int o_size, int n_size)
{
	int t_size=0, zero_size;
	llist_p l_p;
	ainfo_p a_p; /* for clarity */

	if (n_size < 0 || o_size < n_size || !al_p || !al_p->next) return(-1);
	if (o_size == n_size) return(0);

	l_p = (llist_p)al_p->next; /* first item in list */

	/* search through list and shorten the access that first exceeds the
	 * new size (if there is one)
	 */
	while (l_p && t_size < n_size) {
		a_p = (ainfo_p) l_p->item;

		if (a_p->type == t && a_p->sock == s) {
			switch(t) {
				case A_READ:
				case A_WRITE:
					t_size += a_p->u.rw.size;
					if (t_size > n_size) {
						if (t == A_READ) {
							zero_size = t_size - n_size;
							memset(a_p->u.rw.loc + (a_p->u.rw.size - zero_size), 0,
									 zero_size);
						}
						a_p->u.rw.size -= t_size-n_size;
					}
					break;
			}
		}
		l_p = l_p->next;
	}
	/* remove all the rest of the matching accesses */
	while (l_p) {
		llist_p tmpl_p = l_p;
		l_p = l_p->next;
	
		a_p = (ainfo_p) tmpl_p->item;

		if (a_p->type == t && a_p->sock == s)
		{
			if (t == A_READ) {
				memset(a_p->u.rw.loc, 0, a_p->u.rw.size);
			}
			
			a_ptr_rem(al_p, (ainfo_p)tmpl_p->item);
		}
	}
	return(0);
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

