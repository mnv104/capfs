/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/*
 * CAPFS_READ_LIST.C - function to perform noncontiguous read access to files
 *
 */

#include <lib.h>
#include <build_list_job.h>
#include <time.h>
#include <log.h>

extern fdesc_p pfds[];
extern jlist_p active_p;
extern sockset socks;
extern int capfs_mode;

static int unix_read_list(int     fd,
								  int     mem_list_count,
								  char   *mem_offsets[],
								  int     mem_lengths[],
								  int     file_list_count,
								  int64_t file_offsets[],
								  int32_t file_lengths[]);

#define PCOUNT pfd_p->fd.meta.p_stat.pcount

int capfs_read_list(int     fd,
						 int     mem_list_count,
						 char   *mem_offsets[],
						 int     mem_lengths[],
						 int     file_list_count,
						 int64_t file_offsets[],
						 int32_t file_lengths[])
{
	int i, total_size, cutoff;
	int64_t size = 0;
	fdesc_p pfd_p = pfds[fd];

	if (fd < 0 || fd >= CAPFS_NR_OPEN 
	    || (pfds[fd] && pfds[fd]->fs == FS_RESV)) 
	{
		errno = EBADF;
		return(-1);
	}
	if (capfs_mode == 1) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIB, "Unimplemented capfs_read_list call in SCCAPFS!\n");
		errno = ENOSYS;
		return -1;
	}
	
	/* look for negative values */
	if (list_check(mem_list_count,
						mem_lengths,
						file_list_count,
						file_offsets,
						file_lengths) < 0) 
	{
		/* errno set in list_check */
		return -1;
	}

	/* find the total length of all the accesses and check them */
	total_size = check_list_size(mem_list_count, mem_lengths, 
										  file_list_count, file_lengths);
	if (total_size < 0) {
		/* errno set by check_list_size */
		return -1;
	}

	if (!pfd_p || pfd_p->fs == FS_UNIX) {
		return(unix_read_list(fd,
									 mem_list_count,
									 mem_offsets,
									 mem_lengths,
									 file_list_count,
									 file_offsets,
									 file_lengths));
	}
	if (pfd_p->fs == FS_PDIR) {
		/* just return -1 and be done w/this for directories */
		errno = EBADF;
		return -1;
	}

	for (i = 0; i < PCOUNT; i++) {
		pfd_p->fd.iod[i].ack.status = 0;
		pfd_p->fd.iod[i].ack.dsize  = 0;
	}	

	/* cutoff tells us how to break up list I/O that is too big */
	cutoff = CAPFS_LIST_IO_CUTOFF;
	/* just make one request if there are less than "cutoff" file accesses */
	if (cutoff > file_list_count)
	{
		/* build jobs, including requests and acks */
		if (build_list_jobs(pfd_p, 
								  mem_list_count,
								  mem_offsets,
								  mem_lengths,
								  file_list_count,
								  file_offsets,
								  file_lengths,
								  IOD_RW_READ,
								  0,
								  total_size) < 0) 
		{
			LOG(stderr, WARNING_MSG, SUBSYS_LIB,  "build_list_jobs failed in capfs_read_list\n");
			return(-1);
		}
			
		/* send requests; receive data and acks */
		while (!jlist_empty(active_p)) {
			if (do_jobs(active_p, &socks, -1) < 0) {
				PERROR(SUBSYS_LIB,"capfs_read_list: do_jobs");
				return(-1);
			}
		}

		LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "capfs_read_list: finished with read jobs\n");
		/* check acks to make sure things worked right */
		for (i=0; i < PCOUNT; i++) {
			if (pfd_p->fd.iod[i].ack.status) {
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  "capfs_read_list: non-zero status returned from iod %d\n", i);
				errno = pfd_p->fd.iod[i].ack.eno;
				return(-1);
			}
			size += pfd_p->fd.iod[i].ack.dsize;
		}
	}

	/* break up the requests in blocks of 64 file accesses per request */
	else 
	{
		size = multiple_build_list_jobs(pfd_p,
												  mem_list_count,
												  mem_offsets,
												  mem_lengths,
												  file_list_count,
												  file_offsets,
												  file_lengths,
												  IOD_RW_READ,
												  cutoff);
		if (size < 0)
		{
			LOG(stderr, WARNING_MSG, SUBSYS_LIB,  "multiple_build_list_jobs failed in capfs_read_list\n");
			return(-1);
		}
	}
			
	/* don't update file pointer position on read list calls */
	pfd_p->fd.meta.u_stat.atime = time(NULL);
	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "capfs_read_list: completed %Ld bytes\n", size);
			
	return(size);
}

/* reads a contiguous region of a file and places it in memory with offsets 
 * and lengths
 */
static int unix_read_list(int     fd,
								  int     mem_list_count,
								  char   *mem_offsets[],
								  int     mem_lengths[],
								  int     file_list_count,
								  int64_t file_offsets[],
								  int32_t file_lengths[])
{
	int i, size, check, partial_size;
	char *buf;
	char *buf_ptr;

	/* check that the regions are equal and get the total size */
	size = check_list_size(mem_list_count, mem_lengths, file_list_count, 
								  file_lengths);
	if (size < 0) {
		/* errno set in check_list_size */
		return -1;
	}

	/* check for special case (contiguous to contiguous) */
	if ((mem_list_count == 1) && (file_list_count == 1))
	{
      /* seek to the file_offsets[0] */
special_case_lseek_restart:
      check = lseek(fd, (int) file_offsets[0], SEEK_SET);
      if (check < 0)
		{
			if (errno == EINTR) goto special_case_lseek_restart;
			return -1;
		}

      /* read in data to memory directly (ignoring buffer) */
special_case_read_restart:
      check = read(fd, mem_offsets[0], file_lengths[0]);
      if (check < 0)
		{
			if (errno == EINTR) goto special_case_read_restart;
			return -1;
		}

      return check;
	}

	/* allocate the correct size of the temporary buffer */
	/* TODO: NEED TO LIMIT BUFFER SIZE */
	buf = (char *) malloc(size*sizeof(char));
	if (buf == NULL) {
		errno = ENOMEM;
		return -1;
	}

	buf_ptr = buf;
	partial_size = 0; /* used to keep up with progress */

	/* copy the all of the file data into a temporary array */
	for (i = 0; i < file_list_count; i++)
	{
      /* seek to the file_offsets[i] */
lseek_restart:
      check = lseek(fd, (int) file_offsets[i], SEEK_SET);
      if (check < 0)
		{
			if (errno == EINTR) goto lseek_restart;
			else if (partial_size) return partial_size;
			else return -1;
		}
      
      /* read in data to the (temp) buf */
read_restart:
      check = read(fd, buf_ptr, file_lengths[i]);
      if (check == -1)
		{
			if (errno == EINTR) goto read_restart;
			else if (partial_size) {
				/* got an error, but we got some data first.  break and copy */
				break;
			}
			else return -1;
		}

		partial_size += check;

      if (check != file_lengths[i])
		{
			/* got a short read; quit trying to read */
			break;
		}
      
      buf_ptr += file_lengths[i];
	}

	buf_ptr = buf;

	/* distribute the data from the buffer into memory offsets with 
	 * specified lengths 
	 */
	for (i = 0; i < mem_list_count; i++)
	{
		/* TODO: only copy the partial_size bytes that we received */
      memcpy(mem_offsets[i], buf_ptr, mem_lengths[i]);
      buf_ptr += mem_lengths[i];
	} 

	/* free memory */
	free(buf);

	return partial_size;
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

