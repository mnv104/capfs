/*
 * (C) 2005 Penn State University 
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/*
 * JLIST.C - LIBRARY VERSION - header file for jlist.c
 *
 */

#ifndef JLIST_H
#define JLIST_H

#include <llist.h>
#include <alist.h>

#include <dfd_set.h>

typedef llist_p jlist_p;

typedef struct jinfo jinfo, *jinfo_p;

#define J_READ  0
#define J_WRITE 1
#define J_OTHER 2

struct jinfo {
	int8_t type;			/* IOD_RW_READ, IOD_RW_WRITE, etc. */
	dyn_fdset socks;     /* list of sockets that are (or have been) in job */
	int max_nr;       /* max socket # + 1 (for use w/socks) */
	int64_t size;	/* amount of data to be moved by this job */
	alist_p al_p;		/* list of accesses */
	int64_t psize[1];	/* array of partial sizes from iods - variable size! */
};

jlist_p jlist_new(void);
int j_add(jlist_p, jinfo_p);
jinfo_p j_search(jlist_p, int sock);
int j_rem(jlist_p, int sock);
void jlist_cleanup(jlist_p);
int jlist_dump(jlist_p);
int j_dump(void *);
jinfo_p j_new(int pcount);
int jlist_empty(jlist_p jl_p);
void j_free(void *j_p);

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


