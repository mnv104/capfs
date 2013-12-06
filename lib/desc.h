/*
 * (C) 2005 Penn State University 
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#ifndef DESC_H
#define DESC_H

#include <meta.h>
#include <req.h>
#include <capfs_config.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STRICT_DESC_CHECK

#ifdef STRICT_DESC_CHECK
#define IOD_INFO_CHECKVAL 123456789
#define FDESC_CHECKVAL    234567890
#endif

#define O_PASS  O_GADD /* passive connection */
#define O_SCHED O_GSTART /* scheduling connection */

/* FS TYPE DEFINES -- USED IN FDESC STRUCTUES */
#define FS_UNIX 0 /* UNIX file/directory */
#define FS_CAPFS 1 /* CAPFS file */
#define FS_RESV 2 /* reserved (socket connected to CAPFS daemon) */
#define FS_PDIR 3 /* CAPFS directory */

typedef struct fpart fpart, *fpart_p;

struct fpart {
	int64_t offset;
	int64_t gsize;
	int64_t stride;
	int64_t gstride;
	int64_t ngroups;
};


typedef struct iod_info iod_info, *iod_info_p;

struct iod_info {
#ifdef STRICT_DESC_CHECK
	int32_t checkval;
	int32_t __pad1;  /* force alignment of rest */
#endif
	struct sockaddr_in addr;
	iack ack;
	int32_t sock;    /* with single connections to iod, this field now serves as an index 
							  into the iodinfo data structure which holds the socket file-descriptor.
							  Hence this is not the real socket fd in that case 
							*/
	int32_t __pad2;  /* struct is passed as arrays, must be 64-bit */
};

typedef struct fdesc fdesc, *fdesc_p;

struct fdesc {
#ifdef STRICT_DESC_CHECK
	int checkval;
#endif
	int8_t fs; /* no longer boolean -- SEE DEFINES ABOVE! */
	fpart_p part_p;
	char *fn_p; /* pointer to optionally available file name -- */
	            /* in terms of manager's view! */
	struct {
		int64_t off;
		fmeta meta;
		int32_t cap; /* capability */
		int32_t ref; /* number of pfds referencing this structure */
		int flag; /* flags used to open file */
		iod_info iod[1];
	        /* NOTE THAT WE ALLOCATE ENOUGH SPACE FOR AN IOD_INFO PER
	         * IOD WHEN WE ACTUALLY MALLOC() THIS */
	} fd;
};

#ifdef __cplusplus
}
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


