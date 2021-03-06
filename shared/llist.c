/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#include <llist.h>

/* llist_new() - returns a pointer to an empty list
 */
llist_p llist_new(void)
{
	llist_p l_p;

	if (!(l_p = (llist_p) malloc(sizeof(llist)))) return(NULL);
	l_p->next = l_p->item = NULL;
	return(l_p);
}

/* llist_empty() - determines if a list is empty
 *
 * Returns 0 if not empty, 1 if empty
 */
int llist_empty(llist_p l_p)
{
	if (l_p->next == NULL) return(1);
	return(0);
}

/* llist_add_to_tail() - adds an item to a list
 *
 * Requires that a list have already been created
 * Puts item at tail of list
 * Returns 0 on success, -1 on failure
 */
int llist_add_to_tail(llist_p l_p, void *item)
{
	llist_p new_p;

	if (!l_p) /* not a list */ return(-1);

	/* NOTE: first "item" pointer in list is _always_ NULL */

	if ((new_p = (llist_p) malloc(sizeof(llist))) == NULL) return -1;
	new_p->next = NULL;
	new_p->item = item;
	while (l_p->next) l_p = l_p->next;
	l_p->next   = new_p;
	return(0);
}

/* llist_add_to_head() - adds an item to a list
 *
 * Requires that a list have already been created
 * Puts item at head of list
 * Returns 0 on success, -1 on failure
 */
int llist_add_to_head(llist_p l_p, void *item)
{
	llist_p new_p;

	if (!l_p) /* not a list */ return(-1);

	/* NOTE: first "item" pointer in list is _always_ NULL */

	if ((new_p = (llist_p) malloc(sizeof(llist))) == NULL) return -1;
	new_p->next = l_p->next;
	new_p->item = item;
	l_p->next   = new_p;
	return(0);
}

/* llist_head() - returns a pointer to the item at the head of the
 * list
 *
 * Returns NULL on error or if no items are in list
 */
void *llist_head(llist_p l_p)
{
	if (!l_p || !l_p->next) return(NULL);
	return(l_p->next->item);
}
/* llist_tail() - returns pointer to the item at the tail of the list
 * 
 * Returns NULL on error or if no items are in list
 */
void * llist_tail(llist_p l_p) {
	if ( !l_p || !l_p->next) return(NULL);
	while (l_p->next) l_p = l_p->next;
	return(l_p->item);
}

/* llist_search() - finds first match from list and returns pointer
 *
 * Returns NULL on error or if no match made
 * Returns pointer to item if found
 */
void *llist_search(llist_p l_p, void *key, int (*comp)(void *, void *))
{
	if (!l_p || !l_p->next || !comp) /* no or empty list */ return(NULL);
	
	for (l_p = l_p->next; l_p; l_p = l_p->next) {
		/* NOTE: "comp" function must return _0_ if a match is made */
		if (!(*comp)(key, l_p->item)) return(l_p->item);
	}
	return(NULL);
}

/* llist_rem() - removes first match from list
 *
 * Returns NULL on error or not found, or a pointer to item if found
 * Removes item from list, but does not attempt to free memory
 *   allocated for item
 */
void *llist_rem(llist_p l_p, void *key, int (*comp)(void *, void *))
{
	if (!l_p || !l_p->next || !comp) /* no or empty list */ return(NULL);
	
	for (; l_p->next; l_p = l_p->next) {
		/* NOTE: "comp" function must return _0_ if a match is made */
		if (!(*comp)(key, l_p->next->item)) {
			void *i_p = l_p->next->item;
			llist_p rem_p = l_p->next;

			l_p->next = l_p->next->next;
			free(rem_p);
			return(i_p);
		}
	}
	return(NULL);
}

/* llist_doall() - passes through list calling function "fn" on all 
 *    items in the list
 *
 * Returns -1 on error, 0 on success
 */
int llist_doall(llist_p l_p, int (*fn)(void *))
{
	llist_p tmp_p;

	if (!l_p || !l_p->next || !fn) return(-1);
	for (l_p = l_p->next; l_p;) {
		tmp_p = l_p->next; /* save pointer to next element in case the
		                    * function destroys the element pointed to
								  * by l_p...
								  */
		(*fn)(l_p->item);
		l_p = tmp_p;
	}
	return(0);
}

/* llist_doall_arg() - passes through list calling function "fn" on all 
 *    items in the list; passes through an argument to the function
 *
 * Returns -1 on error, 0 on success
 */
int llist_doall_arg(llist_p l_p, int (*fn)(void *item, void *arg), void *arg)
{
	llist_p tmp_p;

	if (!l_p || !l_p->next || !fn) return(-1);
	for (l_p = l_p->next; l_p;) {
		tmp_p = l_p->next; /* save pointer to next element in case the
		                    * function destroys the element pointed to
								  * by l_p...
								  */
		(*fn)(l_p->item, arg);
		l_p = tmp_p;
	}
	return(0);
}

/* llist_free() - frees all memory associated with a list
 *
 * Relies on passed function to free memory for an item
 */
void llist_free(llist_p l_p, void (*fn)(void*))
{
	llist_p tmp_p;

	if (!l_p || !fn) return;

	/* There is never an item in first entry */
	tmp_p = l_p;
	l_p = l_p->next;
	free(tmp_p);
	while(l_p) {
		(*fn)(l_p->item);
		tmp_p = l_p;
		l_p = l_p->next;
		free(tmp_p);
	}
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
