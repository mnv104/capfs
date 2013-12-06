/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */

/*
 * CAPFS_WRITE_LIST.C - function to perform noncontiguous write access to files
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

static int unix_write_list(int     fd,
									int     mem_list_count,
									char   *mem_offsets[],
									int     mem_lengths[],
									int     file_list_count,
									int64_t file_offsets[],
									int32_t file_lengths[]);

#define PCOUNT pfd_p->fd.meta.p_stat.pcount

int capfs_write_list(int     fd,
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
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIB, "capfs_write_list is not implemented for CAPFS\n");
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
		return(unix_write_list(fd,
									  mem_list_count,
									  mem_offsets,
									  mem_lengths,
									  file_list_count,
									  file_offsets,
									  file_lengths));
	}

	if (pfd_p->fs == FS_PDIR) {
		/* let's just return -1 and be done with this for directories */
		errno = EBADF;
		return -1;
	}

	for (i = 0; i < PCOUNT; i++) {
		pfd_p->fd.iod[i].ack.dsize  = 0;
		pfd_p->fd.iod[i].ack.status = 0;
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
								  IOD_RW_WRITE,
								  0,
								  total_size) < 0) 
		{
			LOG(stderr, WARNING_MSG, SUBSYS_LIB,  "build_list_jobs failed in capfs_write_list\n");
			return(-1);
		}

	
		/* send requests; receive data and acks */
		while (!jlist_empty(active_p)) {
			if (do_jobs(active_p, &socks, -1) < 0) {
				PERROR(SUBSYS_LIB,"capfs_write_list: do_jobs");
				return(-1);
			}
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "capfs_write_list: finished with write jobs\n");
		for (i=0; i < PCOUNT; i++) {
			if (pfd_p->fd.iod[i].ack.status) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_LIB,  " capfs_write_list: non-zero status returned from iod %d\n", i);
				/* this is likely to be a ENOSPC on one node */
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
												  IOD_RW_WRITE,
												  cutoff);
		if (size < 0)
		{
			LOG(stderr, WARNING_MSG, SUBSYS_LIB,  "multiple_build_list_jobs failed in capfs_write_list\n");
			return(-1);
		}
	}

	/* update modification time meta data */
	pfd_p->fd.meta.u_stat.mtime = time(NULL);
	/* don't update file offset */
	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "capfs_write_list: completed %Ld bytes\n", size);

	return(size);
}

/* reads a contiguous region of memory and places it into file with 
 * offsets and lengths 
 */
static int unix_write_list(int     fd,
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
	
	/* check for special cases (contiguous to contiguous) */
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
		 
		/* write data to memory directly (ignoring buffer) */
special_case_write_restart:
		check = write(fd, mem_offsets[0], file_lengths[0]);
		if (check == -1)
		{
			if (errno == EINTR) goto special_case_write_restart;
			return -1;
		}
		if (check != file_lengths[0])
		{
			return 0;
		}  
	  
		return 0;
	}

	/* allocate the correct size of the temporary buffer */
	/* TODO: PLACE A LIMIT ON THE SIZE OF THIS BUFFER! */
	buf = (char *) malloc(size*sizeof(char));
	if (buf == NULL) {
		errno = ENOMEM;
		return -1;
	}

	buf_ptr = buf;

	/* copy the data to a temporary buffer from the memory offsets 
	 * with specified lengths 
	 */
	for (i = 0; i < mem_list_count; i++)
	{
      memcpy(buf_ptr, mem_offsets[i], mem_lengths[i]);
      buf_ptr += mem_lengths[i];
	}

	buf_ptr = buf;
	partial_size = 0; /* used to keep up with progress */

	/* distribute all of the file data from the temporary array */
	for (i = 0; i < file_list_count; i++)
	{
      /* seek to the file_offsets[i] */
lseek_restart:
      check = lseek(fd, (int) file_offsets[i], SEEK_SET);
      if (check < 0)
		{
			if (errno == EINTR) goto lseek_restart;
			/* ??? */
			return -1;
		}
      /* write data from (temp) buf to file */
write_restart:
      check = write(fd, buf_ptr, file_lengths[i]*sizeof(char));
      if (check == -1)
		{
			if (errno == EINTR) goto write_restart;
			/* ??? */
			return -1;
		}

		partial_size += check;

      if (check != file_lengths[i])
		{
			/* got a short write; quit trying to write */
			break;
		}
      
      buf_ptr += file_lengths[i];
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
 *
 * vim: ts=3
 * End:
 */ 
