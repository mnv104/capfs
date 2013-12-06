/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */

#ifndef _METAIO_H
#define _METAIO_H

int meta_open(char *pathname, int flags);
int meta_creat(char *pathname, int flags);
int meta_read(int fd, struct fmeta *meta_p);
int meta_write(int fd, struct fmeta *meta_p);
int meta_close(int fd);
int meta_unlink(char *pathname);
int meta_access(int fd, char *pathname, uid_t uid, gid_t gid, int mode);
int meta_check_ownership(int fd, char *pathname, uid_t uid, gid_t gid);
int get_parent(char *pathname); /* this should go somewhere else */
int in_group(uid_t uid, gid_t gid);

int md_mkdir(char *dirpath, dmeta_p dir);
int get_dmeta(char * fname, dmeta_p dir);
int put_dmeta(char * fname, dmeta_p dir);
int dmeta_open(char *pathname, int flags);
int dmeta_read(int fd, void *buf, size_t len);
int dmeta_close(int fd);

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
