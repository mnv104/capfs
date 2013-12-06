/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#ifndef BUILD_JOB_H
#define BUILD_JOB_H

/* CAPFS INCLUDES */
#include <desc.h>
#include <req.h>

int build_rw_jobs(fdesc_p f_p, char *buf_p, int64_t size, int type);
void *add_accesses(fdesc_p f_p, int64_t rl, int64_t rs, char *buf_p, int type);
int build_rw_req(fdesc_p f_p, int64_t size, ireq_p r_p, int type, int num);
int build_simple_jobs(fdesc_p f_p, ireq_p r_p);

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
