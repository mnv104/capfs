/*
 * (C) 2005 Penn State University 
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */



#ifndef IODTAB_H
#define IODTAB_H

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <capfs_config.h>

/* DEFINES */

#define TABFNAME "/.iodtab"


/* STRUCTURES AND TYPEDEFS */

typedef struct iodtabinfo iodtabinfo, *iodtabinfo_p;

struct iodtabinfo
{
	int nodecount;
	struct sockaddr_in iod[CAPFS_MAXIODS];
};

/* PROTOTYPES */

iodtabinfo_p parse_iodtab(char *);

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
