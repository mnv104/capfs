/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/*
 * ALIST.H - access list header file FOR LIBRARY CODE
 *
 */

#ifndef ALIST_H
#define ALIST_H

#include <llist.h>

typedef llist_p alist_p;

typedef struct ainfo ainfo, *ainfo_p;

#define A_READ  0
#define A_WRITE 1
#define A_REQ   2
#define A_ACK   3

struct ainfo {
	int8_t type;
	int sock;
	union {
		struct {
			char *loc;
			int64_t size;
		} rw;
		struct {
			char *req_p;
			char *cur_p;
			int64_t size;
		} req;
		struct {
			int iod_nr;
			char *ack_p;
			char *cur_p;
			int64_t size;
		} ack;
		struct {
			alist_p al_p;
		} list;
	} u;
};

alist_p alist_new(void);
int a_add_start(alist_p, ainfo_p);
int a_add_end(alist_p, ainfo_p);
ainfo_p a_get_start(alist_p);
ainfo_p a_get_end(alist_p);
ainfo_p a_search(alist_p, int sock);
int a_sock_rem(alist_p, int sock);
int a_ptr_rem(alist_p, ainfo_p);
void a_cleanup(alist_p);
int a_dump(void *);
int alist_dump(alist_p al_p);
ainfo_p ainfo_new(void);
void alist_cleanup(alist_p al_p);
int alist_empty(alist_p al_p);

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
