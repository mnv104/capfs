/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/*
 * JLIST.C - functions to handle the creation and modification of 
 *
 * This is built on top of the llist.[ch] files
 * Note: this file is used both in the iod and in library functions
 *
 */

#include <stdlib.h>
#include <string.h>
#include <jlist.h>
#include <log.h>

/* prototypes for internal functions */
static int j_sock_cmp(void *key, void *j_p);

jlist_p jlist_new(void)
{
	return(llist_new());
}

int jlist_empty(jlist_p jl_p)
{
	return(llist_empty(jl_p));
}

int j_add(jlist_p jl_p, jinfo_p j_p)
{
	return(llist_add(jl_p, (void *) j_p));
}

jinfo_p j_search(jlist_p jl_p, int sock)
{
	return((jinfo_p) llist_search(jl_p, (void *) &sock, j_sock_cmp));
}

int j_rem(jlist_p jl_p, int sock)
{
	jinfo_p j_p;
	
	j_p = (jinfo_p) llist_rem(jl_p, (void *) &sock, j_sock_cmp);
	if (j_p) {
		j_free(j_p);
		return(0);
	}
	return(-1);
}

static int j_sock_cmp(void *key, void *j_p)
{
	if (dfd_isset((int)(*(int *) key), &((jinfo_p) j_p)->socks)) return(0);
	else return(1);
}

void jlist_cleanup(jlist_p jl_p)
{
	llist_free(jl_p, j_free);
}

/* j_free() - frees the memory allocated to store a job */
void j_free(void *j_p)
{
	alist_cleanup(((jinfo_p) j_p)->al_p);
	dfd_finalize(&((jinfo_p)j_p)->socks);
	free((jinfo_p) j_p);
}

int jlist_dump(jlist_p jl_p)
{
	return(llist_doall(jl_p, j_dump));
}

int j_dump(void *v_p)
{
	jinfo_p j_p = (jinfo_p) v_p;
	
	LOG(stderr, DEBUG_MSG, SUBSYS_NONE,  "type: %d\nsize: %Ld\n", j_p->type, j_p->size);
	alist_dump(j_p->al_p);
	return(0);
}

jinfo_p j_new(int pcount) 
{
	jinfo_p j_p;
	int i;

	if ((j_p = (jinfo_p) malloc(sizeof(jinfo)
		+ sizeof(int64_t)*(pcount-1))) == NULL) {
		return(0);
	}
	memset(j_p, 0, sizeof(jinfo)+sizeof(int64_t)*(pcount-1));
	if (!(j_p->al_p = alist_new())) /* error creating access list */ {
		free(j_p);
		return(0);
	}

	j_p->type = 0;
	j_p->max_nr = 0;
	dfd_init(&j_p->socks, 1);
	j_p->size = 0; /* new job - no accesses */
	for (i=0; i < pcount; i++) j_p->psize[i] = 0;
	return(j_p);
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
