/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/* This file contains CAPFS library call for unlink.   	*/
/* It will determine if file is UNIX or CAPFS and then 	*/
/* make the proper request.										*/

#include <lib.h>
#include <sys/param.h>
#include <meta.h>
#include <errno.h>
#include <sockio.h>

/* This file contains CAPFS library call for symlink and hard links.   	*/
/* It will determine if file is UNIX or CAPFS and then 						*/
/* make the proper request.															*/


extern int capfs_checks_disabled;

int capfs_symlink(const char* target_name, const char *link_name)
{
	int i;
	mreq req;
	mack ack;
	struct sockaddr *saddr;
	char *fn;
	int64_t fs_ino;
	char bothname[MAXPATHLEN + MAXPATHLEN + 2];
	int len = 0, bothlen = 0;
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;

	memset(&req, 0, sizeof(req));
	if (!target_name || !link_name) {
		errno = EFAULT;
		return(-1);
	}
	if (capfs_checks_disabled) return symlink(target_name, link_name);
	/* Check to see if .capfsdir exists in directory of link_name	*/
	if ((i = capfs_detect(link_name, &fn, &saddr, &fs_ino, NULL, NOFOLLOW_LINK)) < 1) {
		if (i < 0) {
			PERROR(SUBSYS_LIB,"Error finding file");
			return(-1);
		}	
		return symlink(target_name, link_name);
	}
	len = snprintf(bothname, MAXPATHLEN, "%s", fn);
	if(len <= 0 || len >= MAXPATHLEN) {
		errno = EINVAL;
		PERROR(SUBSYS_LIB,"Could not append file names to string\n");
		return (-1);
	}
	bothlen += (len + 1);
	bothlen += snprintf(bothname + len + 1, MAXPATHLEN, "%s", target_name);
	/* Prepare request for file system  */
	req.dsize = bothlen;
	req.uid = getuid();
	req.gid = getgid();
	req.type = MGR_LINK;
	req.req.link.soft = 1;
	req.req.link.meta.fs_ino = fs_ino;
	req.req.link.meta.u_stat.st_uid = req.uid;
	req.req.link.meta.u_stat.st_gid = req.gid;
	req.req.link.meta.u_stat.st_mode = S_IFLNK | S_IRWXU | S_IRWXG | S_IRWXO;

	/* Send request to mgr */	
	if (send_mreq_saddr(&opt, saddr, &req, bothname, &ack, NULL) < 0) {
		PERROR(SUBSYS_LIB,"capfs_symlink: send_mreq_saddr -");
		return(-1);
	}
	else if (ack.status) {
		errno = ack.eno;
		PERROR(SUBSYS_LIB,"capfs_symlink:");
	}
	return ack.status;
}

int capfs_link(const char* target_name, const char *link_name)
{
	int i;
	mreq req;
	mack ack;
	struct sockaddr *saddr;
	char *fn = NULL;
	char bothname[MAXPATHLEN + MAXPATHLEN + 2];
	int len = 0, bothlen = 0;
	int64_t oldfs_ino, newfs_ino;
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;

	memset(&req, 0, sizeof(req));
	if (!target_name || !link_name) {
		errno = EFAULT;
		return(-1);
	}
	if (capfs_checks_disabled) return link(target_name, link_name);
	/* Check to see if .capfsdir exists in directory of link_name and target_name */
	if ((i = capfs_detect(link_name, &fn, &saddr, &newfs_ino, NULL, NOFOLLOW_LINK)) < 1) {
		if (i < 0) {
			PERROR(SUBSYS_LIB,"Error finding file");
			return(-1);
		}	
		return link(target_name, link_name);
	}
	len = snprintf(bothname, MAXPATHLEN, "%s", fn);
	if(len <= 0 || len >= MAXPATHLEN) {
		errno = EINVAL;
		PERROR(SUBSYS_LIB,"Could not append file names to string\n");
		return (-1);
	}
	bothlen += (len + 1);
	/* since link_name exists on a CAPFS volume, target_name should also be on a CAPFS volume */
	if ((i = capfs_detect(target_name, &fn, &saddr, &oldfs_ino, NULL, NOFOLLOW_LINK)) < 1) {
		if (i < 0) {
			PERROR(SUBSYS_LIB,"Error finding file");
			return(-1);
		}	
		errno = EXDEV; /* cross file system */
		return (-1);
	}
	bothlen += snprintf(bothname + len + 1, MAXPATHLEN, "%s", fn);
	if(oldfs_ino != newfs_ino) {
		errno = EXDEV; /* cross file system */
		return (-1);
	}
	/* Prepare request for file system  */
	req.dsize = bothlen;
	req.uid = getuid();
	req.gid = getgid();
	req.type = MGR_LINK;
	req.req.link.soft = 0;
	req.req.link.meta.fs_ino = newfs_ino;
	req.req.link.meta.u_stat.st_uid = req.uid;
	req.req.link.meta.u_stat.st_gid = req.gid;
	req.req.link.meta.u_stat.st_mode = S_IFLNK | S_IRWXU | S_IRWXG | S_IRWXO;

	/* Send request to mgr */	
	if (send_mreq_saddr(&opt, saddr, &req, bothname, &ack, NULL) < 0) {
		PERROR(SUBSYS_LIB,"capfs_link: send_mreq_saddr -");
		return(-1);
	}
	else if (ack.status) {
		errno = ack.eno;
		PERROR(SUBSYS_LIB,"capfs_link:");
	}
	return ack.status;
}

int capfs_readlink(const char *path, char *buf, size_t bufsiz)
{
	int i;
	mreq req;
	mack ack;
	struct sockaddr *saddr;
	char *fn = NULL;
	int64_t fs_ino;
	struct ackdata_c ackdata;
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;

	memset(&req, 0, sizeof(req));
	if(!path || !buf) {
		errno = EFAULT;
		return -1;
	}
	if(bufsiz <= 0) {
		errno = EINVAL;
		return -1;
	}
	if (capfs_checks_disabled) return readlink(path, buf, bufsiz);
	/* Check to see if .capfsdir exists in directory of link_name and target_name */
	if ((i = capfs_detect(path, &fn, &saddr, &fs_ino, NULL, NOFOLLOW_LINK)) < 1) {
		if (i < 0) {
			PERROR(SUBSYS_LIB,"Error finding file");
			return(-1);
		}	
		return readlink(path, buf, bufsiz);
	}
	/* Prepare request for file system  */
	req.dsize = strlen(fn);
	req.uid = getuid();
	req.gid = getgid();
	req.type = MGR_READLINK;
	ackdata.type = MGR_READLINK;
	ackdata.u.readlink.link_len = bufsiz;
	ackdata.u.readlink.link_name = buf;
	/* Send request to mgr */	
	if (send_mreq_saddr(&opt, saddr, &req, fn, &ack, &ackdata) < 0) {
		PERROR(SUBSYS_LIB,"capfs_readlink: send_mreq_saddr -");
		return(-1);
	}
	if (ack.status) /* error */ {
		errno = ack.eno;
		return(-1);
	}
	return ack.dsize;
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
