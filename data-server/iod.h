/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#ifndef IOD_H
#define IOD_H

#include <minmax.h>
#include <capfs_config.h>

#define STATNUM 5000

/* PROTOTYPES */
char *fname(ino_t f_ino);

/* mmap defines */
#define PAGENO(x) ((x) & (~(__iod_config.access_size - 1)))
#define PAGEOFF(x) ((x) & (__iod_config.access_size  - 1))

/* for IRIX */
#ifndef MAP_FILE
#define MAP_FILE 0
#endif

#define IOD_MMAP_FLAGS MAP_FILE | MAP_SHARED | MAP_VARIABLE
#define IOD_MMAP_PROT PROT_READ

#ifndef MAP_VARIABLE
#define MAP_VARIABLE 0
#endif


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
