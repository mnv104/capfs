/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/* CAPFS_LLSEEK.C - parallel seek call, 64-bit
 *
 * Upon successful completion, capfs_lseek() returns the current offset, 
 * measured in bytes from the beginning of the file.  If it fails, 
 * -1 is returned and the offset is not changed.
 */

#include <capfs-header.h>
/* CAPFS INCLUDE FILES */
#include <lib.h>

/* UNIX INCLUDE FILES */
#include <errno.h>
#include <unistd.h>

/* GLOBALS */
extern fdesc_p pfds[];
extern sockset socks;
extern jlist_p active_p;
extern int capfs_mode;

int64_t capfs_lseek64(int fd, int64_t off, int whence);
static int64_t fsize_to_file_size(int64_t fsize, 
											 int iod_nr, 
											 struct fdesc *pfd_p);


/* FUNCTIONS */
int64_t capfs_llseek(int fd, int64_t off, int whence)
{
	return(capfs_lseek64(fd, off, whence));
}

int64_t capfs_lseek64(int fd, int64_t off, int whence)
{
	int i, myeno;
	int64_t act_file_sz = 0, est_file_sz;
	int64_t val;
	ireq iodreq;

	memset(&iodreq, 0, sizeof(iodreq));
	if (fd < 0 || fd >= CAPFS_NR_OPEN 
		 || (pfds[fd] && pfds[fd]->fs == FS_RESV)) 
	{
		errno = EBADF;
		return(-1);
	}  

	/* don't call llseek() for directories any more */
	if (!pfds[fd] || pfds[fd]->fs == FS_UNIX) {
		if (pfds[fd]) /* gotta keep our info up to date */ {
#if defined (__ALPHA__) || defined (__IA64__) || !defined (LARGE_FILE_SUPPORT)      || !defined (HAVE_LSEEK64)
			val = lseek(fd, off, whence);
#else
			val = lseek64(fd, off, whence);
#endif
			pfds[fd]->fd.off = val;
			return(pfds[fd]->fd.off);
		}
		else {
#if defined (__ALPHA__) || defined (__IA64__) || !defined (LARGE_FILE_SUPPORT)      || !defined (HAVE_LSEEK64)
			val = lseek(fd, off, whence);
#else
			val = lseek64(fd, off, whence);
#endif
			return(val);
		}
	}

	switch(whence) {
		case SEEK_SET:
			if (off >= 0) return(pfds[fd]->fd.off = off);
			errno = EINVAL;
			return(-1);
		case SEEK_CUR:
			if (pfds[fd]->fd.off + off >= 0) return(pfds[fd]->fd.off += off);
			errno = EINVAL;
			return(-1);
		case SEEK_END:
			/* this isn't implemented for directories as of yet */
			if (pfds[fd]->fs == FS_PDIR) {
				errno = EINVAL;
				return(-1);
			}
			if (capfs_mode == 0)
			{
				/* find the actual end of the file */
				/* HERE WE NEED TO TALK TO THE IODS AND GET THE FILE SIZE */
				iodreq.majik_nr   = IOD_MAJIK_NR;
				iodreq.release_nr = CAPFS_RELEASE_NR;
				iodreq.type       = IOD_STAT;
				iodreq.dsize      = 0;
				iodreq.req.stat.fs_ino = pfds[fd]->fd.meta.fs_ino;
				iodreq.req.stat.f_ino  = pfds[fd]->fd.meta.u_stat.st_ino;
				if (build_simple_jobs(pfds[fd], &iodreq) < 0) {
					PERROR(SUBSYS_LIB,"building job");
					return(-1);
				}

				while (!jlist_empty(active_p)) {
					if (do_jobs(active_p, &socks, -1) < 0) {
						myeno = errno;
						LOG(stderr, WARNING_MSG, SUBSYS_LIB,  "capfs_llseek: do_jobs failed\n");
						errno = myeno;
						return(-1);
					}
				}

				/* calculate the actual size of the file, using new algorithm */
				for (i=0; i < pfds[fd]->fd.meta.p_stat.pcount; i++) {
					if (pfds[fd]->fd.iod[i].ack.status) {
						errno = pfds[fd]->fd.iod[i].ack.eno;
						return(-1);
					}
					est_file_sz = fsize_to_file_size(pfds[fd]->fd.iod[i].ack.ack.stat.fsize,
															 i, pfds[fd]);
					if (est_file_sz > act_file_sz) {
						act_file_sz = est_file_sz;
					}
				}
				off += act_file_sz;
				if (off >= 0) return(pfds[fd]->fd.off = off);
			}
			else {
				struct stat filestat;
				/* find the actual end of the file */
				if ((i = capfs_fstat(fd, &filestat)) < 0) {
					PERROR(SUBSYS_LIB,"Getting file size");
					return(-1);
				}
				if (off + filestat.st_size >= 0)
					return(pfds[fd]->fd.off = off + filestat.st_size);
			}
			/* let error here and default drop through...behavior is the same */
		}
		errno = EINVAL;
		return(-1);
} /* end of CAPFS_LSEEK64() */

/* fsize_to_file_size()
 * fsize - size of iod's local file
 * iod_nr - number of iod in file distribution (base would be 0)
 * pfd_p - pointer to fdesc structure for the file
 */
static int64_t fsize_to_file_size(int64_t fsize, 
											 int iod_nr, 
											 struct fdesc *pfd_p)
{
	int64_t real_file_sz;
	int64_t strip_sz, stripe_sz, nr_strips, leftovers;

	strip_sz = pfd_p->fd.meta.p_stat.ssize;
	stripe_sz = strip_sz * pfd_p->fd.meta.p_stat.pcount;

	nr_strips = fsize / strip_sz;
	leftovers = fsize % strip_sz;

	if (leftovers == 0) {
		nr_strips--;
		leftovers += strip_sz;
	}


	real_file_sz = nr_strips * stripe_sz + iod_nr * strip_sz + leftovers;
	return(real_file_sz);
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 *
 * vim: ts=3
 */
