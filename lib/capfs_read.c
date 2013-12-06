/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/*
 * CAPFS_READ.C - function to perform read access to files
 *
 */

#include <lib.h>
#include <time.h>
#include <log.h>

extern fdesc_p pfds[];
extern jlist_p active_p;
extern sockset socks;
extern int capfs_mode;

static int unix_read(int fd, char *buf, size_t count);
static int64_t fsize_to_file_size(int64_t fsize, 
											 int iod_nr, 
											 struct fdesc *pfd_p);

#define PCOUNT pfd_p->fd.meta.p_stat.pcount

int capfs_read(int fd, char *buf, size_t count)
{
	int i;
	int64_t size = 0;
	fdesc_p pfd_p = pfds[fd];

	/* variables added for correct handling of EOF */
	char *contacted;
	int64_t exp_next_off, act_last_off, known_file_sz, calc_file_sz, start_off;

	if (fd < 0 || fd >= CAPFS_NR_OPEN 
	    || (pfds[fd] && pfds[fd]->fs == FS_RESV)) {
		errno = EBADF;
		return(-1);
	} 
	if (capfs_mode == 1) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIB, "capfs_read is not yet implemented for capfs! Please use"
				"the VFS interface for accessing such files\n");
		errno = ENOSYS;
		return -1;
	}

	if (!pfd_p || pfd_p->fs == FS_UNIX) return(unix_read(fd, buf, count));
	if (pfd_p->fs == FS_PDIR) return(unix_read(fd, buf, count));

#ifdef STRICT_FDESC_CHECK
	if (do_fdesc_check(fd) < 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIB,  "check failed at start of capfs_read()\n");
	}
#endif
	start_off = pfd_p->fd.off;
	exp_next_off = pfd_p->fd.off + count;
	known_file_sz = 0;
	contacted = malloc(PCOUNT * sizeof(char));

	for (i = 0; i < PCOUNT; i++) {
		contacted[i] = 0;
		pfd_p->fd.iod[i].ack.status = 0;
		pfd_p->fd.iod[i].ack.dsize  = 0;
	}	

	/* build jobs, including requests and acks */
	if (build_rw_jobs(pfd_p, buf, count, J_READ) < 0) {
		LOG(stderr, WARNING_MSG, SUBSYS_LIB,  "build_rw_jobs failed in capfs_read\n");
		return(-1);
	}

	/* determine what iods we will contact */
	for (i = 0; i < PCOUNT; i++) {
		/* this isn't so efficient, but i don't want to muck with jlist imp. */
		if (j_search(active_p, pfd_p->fd.iod[i].sock) != NULL) {
			contacted[i] = 1;
		}
	}
	
	/* send requests; receive data and acks */
	while (!jlist_empty(active_p)) {
		if (do_jobs(active_p, &socks, -1) < 0) {
			PERROR(SUBSYS_LIB,"do_jobs");
			free(contacted);
			return(-1);
		}
	}

	/* pass through responses, checking for errors, sizes */
	for (i = 0; i < PCOUNT; i++) {
		if (contacted[i]) {
			if (pfd_p->fd.iod[i].ack.status) {
				errno = pfd_p->fd.iod[i].ack.eno;
				free(contacted);
				return -1;
			}

			/* update known file size */
			calc_file_sz = fsize_to_file_size(pfd_p->fd.iod[i].ack.ack.rw.fsize, 
														 i, pfd_p); 
			size += pfd_p->fd.iod[i].ack.dsize;

			if (calc_file_sz > known_file_sz) {
				known_file_sz = calc_file_sz;
			}

		}
	}

	/* check for short read */
	if (exp_next_off <= known_file_sz) {
		/* we definitely did not hit EOF */
		if (size < count) {
			LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " capfs_read: hit hole, read %Ld of %d (everything should be ok)\n",
				 size, count);
		}
		pfd_p->fd.off = exp_next_off;
		size = count; /* ensure correct return value (size might have hole) */
		errno = 0;
	}
	else {
		mack ack;
		mreq req;
		struct sockaddr saddr;
		struct capfs_options opt;
		
		opt.tcp = MGR_USE_TCP;
		opt.use_hcache = 0;

		/* we know we got a short read.  it MIGHT be EOF, or it might just
		 * be that we hit a hole that happened to extend to the end of our
		 * region to read.  we need to figure out which one happened.
		 */
		act_last_off = known_file_sz;

		/* the quick and dirty way to figure out what happened is to
		 * stat() the file.  a better solution would be to collect the
		 * remaining sizes from the iods we didn't already talk to.
		 */

		req.uid = getuid();
		req.gid = getgid();
		req.type = MGR_FSTAT;
		req.dsize = 0;
		req.req.fstat.meta = pfd_p->fd.meta;
		saddr = pfd_p->fd.meta.mgr;

		if (send_mreq_saddr(&opt, &saddr, &req, NULL, &ack, NULL) < 0 || ack.status != 0) {
			PERROR(SUBSYS_LIB,"capfs_read: send_mreq_saddr - ");
			/* error talking to mgr, but not really critical.
			 * assume we hit EOF, return what we know.
			 */
			pfd_p->fd.off = act_last_off;
			size = act_last_off - start_off;
		}
		else {
			/* got a response; determine if we hit EOF */
			if (ack.ack.fstat.meta.u_stat.st_size > known_file_sz) {
				/* the file is in fact bigger than we were told by the
				 * other iods
				 */
				known_file_sz = ack.ack.fstat.meta.u_stat.st_size;
			}
			if (exp_next_off <= known_file_sz) {
				/* didn't really hit EOF */
				if (size < count) {
					LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " capfs_read: hit hole, read %Ld of %d (everything should be ok)\n",
				  		size, count);
				}
				pfd_p->fd.off = exp_next_off;
				size = count; /* ensure correct return value */
				errno = 0;
			}
			else {
				/* we really did hit EOF; return short read */
				if(known_file_sz > start_off)
				{
					pfd_p->fd.off = known_file_sz;
					size = known_file_sz - start_off;
					errno = 0;
				}
				else
				{
					/* apparently tried to read after seeking beyond EOF;
					 * keep current offset but do not return any data
					 */
					pfd_p->fd.off = start_off;
					size = 0;
					errno = 0;
				}
			}
		}
	}

	pfd_p->fd.meta.u_stat.atime = time(NULL);
	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "capfs_read: completed %Ld bytes; new offset = %Ld\n", size,
		pfd_p->fd.off);

#ifdef STRICT_FDESC_CHECK
	if (do_fdesc_check(fd) < 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIB,  "check failed at end of capfs_read()\n");
	}
#endif

	free(contacted);
	return(size);
}

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

#define OFFSET req.req.rw.off
#define FSIZE  req.req.rw.fsize
#define GSIZE  req.req.rw.gsize
#define LSIZE  req.req.rw.lsize
#define GCOUNT req.req.rw.gcount
#define STRIDE req.req.rw.stride

static int unix_read(int fd, char *buf, size_t count)
{
	fdesc_p fd_p = pfds[fd];
	ireq req;
	int off, ret;
	char *orig_buf = buf;

	if (!fd_p || !fd_p->part_p)
		return read(fd, buf, count);

	build_rw_req(fd_p, count, &req, J_READ, 0);

	/* Notes: request parameters are in terms of the WHOLE file,
	 * whereas the offset stored in fd_p->fd.off is in terms of the
	 * PARTITIONED file.
	 *
	 * Because of this, we're just adding to the partition offset
	 * however much data gets read, while the offset into the whole
	 * file has to skip around a lot.
	 *
	 * We should be able to add a couple of checks to avoid some of
	 * these lseek()s.
	 */
	off = OFFSET;

	if (FSIZE) {
fsize_lseek_restart:
		if (lseek(fd, off, SEEK_SET) < 0) {
			if (errno == EINTR) goto fsize_lseek_restart;
			return(-1);
		}
fsize_read_restart:
		if ((ret = read(fd, buf, FSIZE)) < 0) {
			if (errno == EINTR) goto fsize_read_restart;
			return(-1);
		}
		buf += ret;
		if (ret < FSIZE) /* this is all we're getting */ {
			fd_p->fd.off += buf-orig_buf;
			return(buf-orig_buf);
		}
		off += FSIZE + STRIDE - GSIZE;
	}
	while (GCOUNT-- > 0) {
gcount_lseek_restart:
		if (lseek(fd, off, SEEK_SET) < 0) {
			if (errno == EINTR) goto gcount_lseek_restart;
			return(-1);
		}
gcount_read_restart:
		if ((ret = read(fd, buf, GSIZE)) < 0) {
			if (errno == EINTR) goto gcount_read_restart;
			return(-1);
		}
		buf += ret;
		if (ret < GSIZE) /* this is all we're getting */ {
			fd_p->fd.off += buf-orig_buf;
			return(buf-orig_buf);
		}
		off += STRIDE;
	}
	if (LSIZE) {
lsize_lseek_restart:
		if (lseek(fd, off, SEEK_SET) < 0) {
			if (errno == EINTR) goto lsize_lseek_restart;
			return(-1);
		}
lsize_read_restart:
		if ((ret = read(fd, buf, LSIZE)) < 0) {
			if (errno == EINTR) goto lsize_read_restart;
			return(-1);
		}
		buf += ret;
	}
	fd_p->fd.off += buf-orig_buf;
	return(buf-orig_buf);
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
