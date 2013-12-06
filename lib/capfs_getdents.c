/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */

#include <lib.h>
#include <sockio.h>
#include <string.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <syscall.h>
#include <sys/syscall.h>

#include "mgr.h"

extern fdesc_p pfds[];
extern jlist_p active_p;
extern sockset socks;

static int do_getdents(int fd, struct capfs_dirent *buf, int size);

/* capfs_getdents()
 *
 * This call hooks into the getdents syscall.  It returns structures in
 * the format passed back by the kernel.  This is NOT the format that
 * the libc calls pass back, so you shouldn't call this directly.
 *
 * unlike getdents(2), repeated calls to capfs_getdents will pass pack the 
 * next diretory entries.
 *
 * d_ino and d_off, and d_rclen  are undefined in the returned struct dirent
 * ( i.e. only d_name is useful )
 */
int capfs_getdents(int fd, struct dirent *userp, unsigned int count)
{
	int requested_dirs= 0, received_dirs = 0, nbytes;
	int i, newoffset=0;
	struct capfs_dirent *pde;
	fdesc_p pfd_p = pfds[fd];

	if (fd < 0 || fd >= CAPFS_NR_OPEN 
		 || (pfds[fd] && pfds[fd]->fs == FS_RESV)) 
	{
		errno = EBADF;
		return(-1);
	}  

	if (!pfd_p || pfd_p->fs == FS_UNIX) {
		if ( (nbytes=syscall(SYS_getdents, fd, userp, count)) == -1 ) {
			return errno;
		}
		if ( nbytes == 0 ) return nbytes;
		/* linux getdent(2) doesn't update the file descriptor, but
		 * capfs_getdent will update the fd if it's a capfs directory.
		 * so we'll make capfs_getdent do the same thing for both types
		 * of files, even if that makes it differ from getdents(2) */
		
		/* the trick is in finding the offset of the last directory*/
		received_dirs = nbytes/sizeof(struct dirent);
		/* now forget the syscall's nbytes and return our own */
		i=0, nbytes=0;
		do {
			/* getdents system call packs in all the dirent 
			 * structures.  have to rely on d_reclen to find 
			 * next structure.  hence the fun pointer games */
			struct dirent *new_de;
			new_de = (struct dirent*)((char *)userp + nbytes);
			newoffset = new_de->d_off;
			nbytes+=new_de->d_reclen;
			++i;
		} while ( i < received_dirs);

		if ( lseek(fd, newoffset, SEEK_SET) == -1) {
			return errno;
		}
		return(nbytes);
	}

	if (pfd_p->fs == FS_CAPFS) {
		errno = ENOTDIR;
		return(-1);
	}
	requested_dirs = count / sizeof(struct dirent);
	pde = calloc( 1, requested_dirs * sizeof(struct capfs_dirent) );
	/* mgr speaks in "struct capfs_dirent"s, while client code speaks 
	 * in "struct dirent" */
	nbytes = do_getdents(fd, pde, requested_dirs);
	if (nbytes < 0) {
		free(pde);
		return -1;
	}

	received_dirs = nbytes / sizeof(struct capfs_dirent);
	for(i=0; i< received_dirs; i++) {
		strcpy( (userp+i)->d_name, (pde+i)->name);
		(userp+i)->d_reclen = sizeof(struct dirent);
	}
	free(pde);

	return received_dirs*sizeof(struct dirent);
}

/* DO_GETDENTS() - handles actually getting the getdents data for a CAPFS
 * directory.
 *
 * size is "number of capfs_dirent things", not bytes
 *
 * Returns -1 on failure, the amount (in bytes) read on success.
 */
static int do_getdents(int fd, struct capfs_dirent *buf, int size)
{
	int myeno;
	mreq req;
	mack ack;
	struct sockaddr saddr;
	struct ackdata_c ackdata;
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;
	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	ackdata.type = MGR_GETDENTS;
	ackdata.u.getdents.nentries = size;
	ackdata.u.getdents.pdir = buf;

	if (!pfds[fd]->fn_p) {
		/* we need the directory name in terms the manager can understand */
		errno = EINVAL;
		return(-1);
	}

	req.req.getdents.offset = pfds[fd]->fd.off;
	req.req.getdents.length = size*sizeof(struct capfs_dirent);
	req.uid = getuid();
	req.gid = getgid();
	req.type = MGR_GETDENTS;
	req.dsize = strlen(pfds[fd]->fn_p);

	saddr = pfds[fd]->fd.meta.mgr;
	if (send_mreq_saddr(&opt, &saddr, &req, pfds[fd]->fn_p, &ack, &ackdata) < 0) {
		myeno = errno;
		PERROR(SUBSYS_LIB,"send_mreq_saddr error ");
		errno = myeno;
		return(-1);
	}
	if (ack.status) /* error */ {
		errno = ack.eno;
		return(-1);
	}

	if (!ack.dsize) /* hit EOF */ {
		return(0);
	}
	pfds[fd]->fd.off = ack.ack.getdents.offset;
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
