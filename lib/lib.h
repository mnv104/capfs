/*
 * (C) 2005 Penn State University 
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#ifndef  LIB_H
#define  LIB_H

/* we want to have the 64-bit functions available but not change the existing 
 * functions; _LARGEFILE64_SOURCE accomplishes this.
 */
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <meta.h>
#include <req.h>
#include <desc.h>
#include <linux/fs.h> /* NR_OPEN declaration */
#include <build_job.h>
#include <sockset.h>
#include <jlist.h>
#include <capfs_config.h>
#include <capfs_proto.h>
#include <log.h>
#include <mgr.h>

/*PROTOTYPES*/
void *capfs_exit(void);
int unix_lstat(const char *fn, struct stat *s_p);
int unix_stat(const char *fn, struct stat *s_p);
int unix_fstat(int fd, struct stat *s_p);
#define NOFOLLOW_LINK 0
#define FOLLOW_LINK   1
int capfs_detect(const char *fn, char **, struct sockaddr **, int64_t *, int64_t *, int to_follow);
int capfs_detect2(const char *fn, char **, struct sockaddr **, int64_t *, int64_t *, int to_follow);

extern int send_mreq_saddr(struct capfs_options*, struct sockaddr *saddr_p, mreq_p req_p,
	void *data_p, mack_p ack_p, struct ackdata_c *);

int init_iodtable(int _initial_entries, int _grow_amount);
void cleanup_iodtable(void);
int instantiate_iod_entry(struct sockaddr *saddr_p);
int add_iodtable(struct sockaddr *saddr_p);
int badiodfd(int fd);
int find_iodtable(struct sockaddr *saddr_p);
int getfd_iodtable(int slot);
int getslot_iodtable(int sockfd);
int shutdown_alliods(void);
int inc_ref_count(int slot);
int dec_ref_count(int slot);

int do_jobs_handle_error(jlist_p jl_p, sockset_p ss_p, int msec, int* badsock);
int do_jobs(jlist_p jl_p, sockset_p ss_p, int msec);
int do_job(int sock, jinfo_p j_p, sockset_p ss_p);

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



