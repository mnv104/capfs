/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/*
 * SOCKSET.H - defines and structures to translate generic socket selection
 *                routines into platform specific calls
 */

#ifndef SOCKSET_H
#define SOCKSET_H

/* TYPEDEFS */
typedef struct sockset sockset, *sockset_p;

/* INCLUDES */
#include <dfd_set.h>

/* STRUCTURES */
struct sockset {
	int max_index;
	dyn_fdset read; /* used to save set between selects */
	dyn_fdset write; /* used to save set between selects */
	dyn_fdset tmp_read; /* used to store results of last select */
	dyn_fdset tmp_write; /* used to store results of last select */
	struct timeval timeout;
};

/* PROTOTYPES */
void addsockrd(int, sockset_p);
void addsockwr(int, sockset_p);
void delsock(int, sockset_p);
int nextsock(sockset_p);
int dumpsocks(sockset_p);
void initset(sockset_p);
void finalize_set(sockset_p);
int check_socks(sockset_p, int);
int randomnextsock(int i);



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
