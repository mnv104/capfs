/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#include <lib.h>
#include <meta.h>
#include <capfs_config.h>
#include <errno.h>
#include <sys/statfs.h>

extern fdesc_p pfds[];
extern int capfs_checks_disabled;

int capfs_fstatfs(int fd, struct statfs *buf)
{
	/* check for badness */
	if (fd < 0 || fd >= CAPFS_NR_OPEN 
	    || (pfds[fd] && pfds[fd]->fs == FS_RESV))
	{
		errno = EBADF;
		return(-1);
	}  
	/* check for UNIX */
	if (!pfds[fd] || pfds[fd]->fs==FS_UNIX) {
		return fstatfs(fd, buf);
	}

	if (pfds[fd]->fs == FS_PDIR) {
		buf->f_bsize = DEFAULT_SSIZE;
		buf->f_blocks = -1;
		buf->f_bfree = -1;
		buf->f_bavail = -1;
		buf->f_files = -1;
		buf->f_ffree = -1;
		/* don't know what to do with fsid...leaving it be... */
		buf->f_type = CAPFS_SUPER_MAGIC;
		buf->f_spare[0] = -1;
		buf->f_namelen = 255; /* some random namelen */
	}
	if (pfds[fd]->fs == FS_CAPFS) {
		buf->f_bsize = pfds[fd]->fd.meta.p_stat.ssize; /* return stripe size */
		buf->f_blocks = -1;
		buf->f_bfree = -1;
		buf->f_bavail = -1;
		buf->f_files = -1;
		buf->f_ffree = -1;
		/* don't know what to do with fsid...leaving it be... */
		buf->f_type = CAPFS_SUPER_MAGIC;
		buf->f_namelen = 255;
		buf->f_spare[0] = -1;
	}

	return(0);
}

int capfs_statfs(char *path, struct statfs *buf)
{
	int myerr;
	mreq request;
	mack ack;
	char *fn;
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;

	struct sockaddr *saddr;
	int64_t fs_ino;

	memset(&request, 0, sizeof(request));
	if (capfs_checks_disabled || capfs_detect(path, &fn, &saddr, &fs_ino, NULL, FOLLOW_LINK) < 1)
	{
		/* not CAPFS */
		return statfs(path, buf);
	}

	request.dsize = strlen(fn);
	request.uid = getuid();
	request.gid = getgid();
	request.type = MGR_STATFS;

	if (send_mreq_saddr(&opt, saddr, &request, fn, &ack, NULL) < 0) {
		myerr = errno;
		PERROR(SUBSYS_LIB,"capfs_statfs: send_mreq_saddr -");
		errno = myerr;
		return -1;
	}
	if (ack.status != 0) {
		PERROR(SUBSYS_LIB,"capfs_statfs: ");
		return -1;
	}
	buf->f_bsize = DEFAULT_SSIZE; /* return stripe size */
	buf->f_blocks = (ack.ack.statfs.tot_bytes / (int64_t) DEFAULT_SSIZE);
	buf->f_bfree = (ack.ack.statfs.free_bytes / (int64_t) DEFAULT_SSIZE);
	buf->f_bavail = (ack.ack.statfs.free_bytes / (int64_t) DEFAULT_SSIZE);
	buf->f_files = ack.ack.statfs.tot_files;
	buf->f_ffree = ack.ack.statfs.free_files;
	/* don't know what to do with fsid...leaving it be... */
	buf->f_type = CAPFS_SUPER_MAGIC;
	buf->f_namelen = ack.ack.statfs.namelen;
	buf->f_spare[0] = -1;
	

	return(0);
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
