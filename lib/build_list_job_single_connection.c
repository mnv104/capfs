/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */

#include <capfs-header.h>
#include <lib.h>

/* UNIX INCLUDES */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>

/* CAPFS INCLUDES */
#include <build_job.h>
#include <build_list_job.h>
#include <desc.h>
#include <req.h>
#include <jlist.h>
#include <alist.h>
#include <minmax.h>
#include <sockset.h>
#include <sockio.h>
#include <dfd_set.h>
#include <log.h>
/* GLOBALS */
extern jlist_p active_p;
extern sockset socks;

#define PCOUNT ((int64_t)(f_p->fd.meta.p_stat.pcount))

/* validate no negative list values */
int list_check(int     mem_list_count,
					int     mem_lengths[],
					int     file_list_count,
					int64_t file_offsets[],
					int32_t file_lengths[])
{
	int i;

	if (mem_list_count <= 0) {
		errno = EINVAL;
		return -1;
	}
	if (file_list_count <= 0) {
		errno = EINVAL;
		return -1;
	}
	for (i = 0; i < mem_list_count; i++) {
      if (mem_lengths[i] < 0) {
			errno = EINVAL;
			return -1;
		}
	}
	for (i = 0; i < file_list_count; i++) {
      if (file_offsets[i] < 0) {
			errno = EINVAL;
			return -1;
		}
      if (file_lengths[i] < 0) {
			errno = EINVAL;
			return -1;
		}     
	}
	return 0;
}

/* get_mem_end_offset - tell build_list_jobs where the final
 * memory access should really start at (begin at broken 
 * section of last list_job
 * returns the amount of data that was accesses in the current
 * segment of memory, -1 for error
 */
int get_mem_end_offset(int     mem_start,
							  int     mem_begin_offset,
							  int     *tmp_mem_list_count,
							  int32_t mem_lengths[],
							  int     file_start,
							  int     file_end,
							  int32_t file_lengths[],
							  int     *total_size)
{
	int i, mem_size, file_size;
	
	/* find the total length of the desired number of file accesses */
	file_size = 0;
	for (i = file_start; i <= file_end; i++)
		{
			file_size += file_lengths[i];
		}
	*total_size = file_size;

	/* determine how much memory needs to be accessed */
	mem_size = 0;
	for (i = mem_start; mem_size < file_size; i++)
		{
			/* for the first mem access only add the part of memory
			 * that has not been taken care of */
			if ((mem_start == i) && (mem_begin_offset != 0))
				{
					mem_size += mem_lengths[i] - mem_begin_offset;
				}
			else
				{
					mem_size += mem_lengths[i];
				}
		}

	/* the sizes equal up, just return 0 */
	if (mem_size == file_size)
		{
			*tmp_mem_list_count = i - mem_start;
		}
	/* find out how far into the memory block we went */
	else
		{
			*tmp_mem_list_count = i - mem_start;
			mem_size -= mem_lengths[i - 1];
		}
	/* return how far we went into the last memory segment */
	return (file_size - mem_size);

}

/* check to make sure that the total memory size and the total file size 
 * are equal 
 */
int check_list_size(int mem_list_count, 
						  int mem_lengths[],
						  int file_list_count, 
						  int32_t file_lengths[])
{
	int i, mem_temp, file_temp;
  
	mem_temp = 0;
	file_temp = 0;

	/* add up the total memory size */
	for (i = 0; i < mem_list_count; i++)
		{
			mem_temp += mem_lengths[i];
		}

	/* add up the total file size */
	for (i = 0; i < file_list_count; i++)
		{
			file_temp += file_lengths[i];
		}

	if (mem_temp != file_temp)
		{
			errno = EINVAL;
			return -1;
		}

	/* return the total size */
	return mem_temp;
}

/* multiple_build_list_jobs 
 * do a sequence of capfs_read_list for up to a "cutoff" number
 * of file accesses
 * returns the size of data access if completed, -1 for errors
 */
int multiple_build_list_jobs(fdesc_p f_p, 
									  int     mem_list_count, 
									  char   *mem_offsets[],
									  int     mem_lengths[],
									  int     file_list_count,
									  int64_t file_offsets[],
									  int32_t file_lengths[],
									  int     type,
									  int     cutoff)
{
	int i, total_size, size;

	/* cur_file_list_count is the sum of all the tmp_file_list_counts
	 * tmp_file_list_count is the coumt for only one iteration */
	int tmp_mem_list_count, tmp_file_list_count, cur_file_list_count;
	int mem_start, mem_begin_offset, mem_end_offset, file_start, file_end;

	/* initialize some values */
	mem_start = file_start = 0;
	size = 0;
	tmp_mem_list_count = 0;
	file_end = cutoff - 1;
	tmp_file_list_count = cur_file_list_count = cutoff;
	mem_begin_offset = mem_end_offset = 0;

	/* build multiple list jobs */
	while (1)
		{
			/* if there is an offset, find the beginning */
			if (mem_end_offset > 0)
				{
					mem_begin_offset = mem_end_offset;
				}
			/* beginning is 0 */
			else
				{
					mem_begin_offset = 0;
				}
			/* get tmp_mem_list_count and also mem_end_offset */
			mem_end_offset = get_mem_end_offset(mem_start,
																 mem_begin_offset,
																 &tmp_mem_list_count,
																 mem_lengths,
																 file_start,
																 file_end,
																 file_lengths,
																 &total_size);
	
			/* build jobs, including requests and acks */
			if (build_list_jobs(f_p, 
									  tmp_mem_list_count,
									  &mem_offsets[mem_start],
									  &mem_lengths[mem_start],
									  tmp_file_list_count,
									  &file_offsets[file_start],
									  &file_lengths[file_start],
									  type,
									  mem_begin_offset,
									  total_size) < 0) 
				{
					LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " build_list_jobs failed in multiple_build_list_jobs\n");
					return(-1);
				}

			/* send requests; receive data and acks */
			while (!jlist_empty(active_p)) {
				if (do_jobs(active_p, &socks, -1) < 0) {
					PERROR(SUBSYS_LIB,"do_jobs");
					return(-1);
				}
			}
			
			/* check acks to make sure things worked right */
			for (i=0; i < PCOUNT; i++) {
				if (f_p->fd.iod[i].ack.status) {
					LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " multiple_build_list_jobs: non-zero status returned from iod %d\n", i);
					errno = f_p->fd.iod[i].ack.eno;
					return(-1);
				}
				size += f_p->fd.iod[i].ack.dsize;
			}

			/* must have been the final list_job */
			if (file_list_count ==  cur_file_list_count) 
				{
					return size;
				}
			/* still more list_jobs to be processed */
			else
				{
					/* reset the starting places in the offsets 
					 * if we ended nice, move to next memory segment
					 * otherwise, stay on the last memory segment */
					if (mem_end_offset == 0)
						{
							mem_start += tmp_mem_list_count;
						}
					else
						{
							mem_start += tmp_mem_list_count - 1;
						}

					/* Always start on the next file segment */
					file_start = file_end + 1;

					/* at least two list_jobs left */
					if ((file_list_count - cur_file_list_count) > cutoff)
						{
							file_end += cutoff;
							tmp_file_list_count = cutoff;
							cur_file_list_count += cutoff;
						}
					/* only one list_job left */
					else
						{
							file_end += file_list_count - cur_file_list_count;
							tmp_file_list_count = file_list_count - cur_file_list_count;
							cur_file_list_count = file_list_count;
						}
				}
		}
}

/* BUILD_LIST_JOBS() - construct jobs to fulfill an I/O request
 * PARAMETERS:
 *    f_p   - pointer to file descriptor
 *    type  - type of jlist request
 *
 * Returns -1 on error, 0 on success?
 */
int build_list_jobs(fdesc_p f_p, 
						  int     mem_list_count, 
						  char   *mem_offsets[],
						  int     mem_lengths[],
						  int     file_list_count,
						  int64_t file_offsets[],
						  int32_t file_lengths[],
						  int     type,
						  int     mem_begin_offset,
						  int     total_size)
{
	ireq    req;
	ireq_p  r_p = &req; /* nice way to use the same defines throughout */
	ainfo_p a_p;
	jinfo_p j_p;
	int i, atype, sock, myerr;

	int64_t *file_offsets_p; /* pointers for arrays*/
	int *file_lengths_p;

	int tmp_size; /* variables for add_accesses*/
	int mem_num, file_num, mem_used, file_used, mem_remain, file_remain;
	char *tmp_mem_ptr;
	int64_t tmp_file_off;

	/* build the requests */
	if (!active_p) active_p = jlist_new();

	build_list_req(f_p, &req, file_list_count, type);
	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "  build_list_jobs: done building request\n");

	/* sooner or later this will matter... */
	switch (type) {
	case J_READ:
		atype = A_READ; 
		break;
	case J_WRITE:
	default:
		atype = A_WRITE;
		break;
	}

	/* add the accesses */ /* initialize the ptrs */
	/* add a beginning offset to the mem ptr for a partial first access */
	tmp_mem_ptr = (char *) (mem_offsets[0] + mem_begin_offset*sizeof(char));
	tmp_file_off = file_offsets[0];
	
	tmp_size = 0;
	mem_used = mem_begin_offset*sizeof(char);
	file_used = 0;
	mem_num = 0;
	file_num = 0;

	while (tmp_size < total_size)
	{
		/* remaining number of bytes for current contiguous regions */
		mem_remain = mem_lengths[mem_num] - mem_used;
		file_remain = file_lengths[file_num] - file_used;

		/* case: mem_length = file_length */
		if (mem_remain == file_remain)
		{
			add_accesses(f_p, tmp_file_off, file_remain, tmp_mem_ptr, atype);
			tmp_size += file_remain;
			mem_num++;
			file_num++;
			mem_used = 0;
			file_used = 0;
			tmp_mem_ptr = (char *) mem_offsets[mem_num];
			tmp_file_off = file_offsets[file_num];
		}
		/* case: mem_length < file_length */
		else if (mem_remain < file_remain)
		{
			add_accesses(f_p, tmp_file_off, mem_remain, tmp_mem_ptr, atype);
			tmp_size += mem_remain;
			file_used += mem_remain;
			tmp_file_off += mem_remain;
			mem_num++;
			mem_used = 0;	
			tmp_mem_ptr = (char *) mem_offsets[mem_num];
		}
		/* case: mem_length > file_length */
		/* in the case of multiple_build_list_job this turns out well
		 * since if the memory segment we are reading into is bigger
		 * than the file segnment, we partially read the memory segment
		 * and leave the rest for the next build_list_job call to take
		 * care of */
		else
		{
			add_accesses(f_p, tmp_file_off, file_remain, tmp_mem_ptr, atype);
			tmp_size += file_remain;
			mem_used += file_remain;
			tmp_mem_ptr += file_remain;
			file_num++;
			file_used = 0;
			tmp_file_off = file_offsets[file_num];
		}
	}
	
	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "build_list_jobs: done calling add_accesses\n");
	  
	initset(&socks); /* clear any other jobs out */
	for (i = 0; i < PCOUNT; i++) {
		int slot;
		/* we know the slot into the iod-table */
		slot = f_p->fd.iod[i].sock;
		sock = getfd_iodtable(slot);
		if (sock >= 0 && (j_p = j_search(active_p, sock)) != NULL) {
	      /* add reception of ack */
	      if ((a_p = (ainfo_p) malloc(sizeof(ainfo))) == NULL) {
				PERROR(SUBSYS_LIB,"build_list_jobs: malloc (ainfo1)");
				return(-1);
	      }
	      a_p->type = A_ACK;
	      a_p->sock = slot;/* we store the index to the iod table as sock field member */
	      a_p->u.ack.size = sizeof(iack);
	      a_p->u.ack.ack_p = a_p->u.ack.cur_p = (char *)&(f_p->fd.iod[i].ack);
	      a_p->u.ack.iod_nr = i;
	      
	      switch(type) {
	      case J_READ:
				if (a_add_start(j_p->al_p, a_p) < 0) {
					myerr = errno;
					LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " build_list_jobs: a_add_start failed (ack)\n");
					errno = myerr;
					return(-1);
				}
				break;
	      case J_WRITE:
	      default:
				if (a_add_end(j_p->al_p, a_p) < 0) {
					myerr = errno;
					LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " build_list_jobs: a_add_end failed\n");
					errno = myerr;
					return(-1);
				}
				break;
	      }
	      LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "build_list_jobs: added ack recv for iod %d\n", i);
			r_p = (ireq_p) malloc(sizeof(ireq) + 
										 file_list_count*sizeof(int64_t) +
										 file_list_count*sizeof(int32_t));
			if (r_p == NULL) {
				myerr = errno;
				PERROR(SUBSYS_LIB,"build_list_jobs: malloc (ireq)");
				errno = myerr;
				return(-1);
			}
			file_offsets_p = (int64_t *) ((char *)r_p + sizeof(ireq));
			file_lengths_p = (int32_t *) ((char *)file_offsets_p + 
													file_list_count*sizeof(int64_t));

			*r_p = req;
			memcpy(file_offsets_p, file_offsets, file_list_count*sizeof(int64_t));
			memcpy(file_lengths_p, file_lengths, file_list_count*sizeof(int32_t));
		
			if ((a_p = (ainfo_p) malloc(sizeof(ainfo))) == NULL) {
				myerr = errno;
				PERROR(SUBSYS_LIB,"build_list_jobs: malloc (ainfo2)");
				errno = myerr;
				return(-1);
			}
			a_p->type = A_REQ;
			a_p->sock = slot;/* index into table */
			/* includes ireq and 2 arrays */
			a_p->u.req.size = sizeof(ireq) + file_list_count*sizeof(int64_t) 
				+ file_list_count*sizeof(int32_t);
			a_p->u.req.cur_p = a_p->u.req.req_p = (char *) r_p;
			if (a_add_start(j_p->al_p, a_p) < 0) {
				myerr = errno;
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " build_list_jobs: a_add_start failed (req)\n");
				errno = myerr;
				return(-1);
			}
			addsockwr(sock, &socks);
			LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "build_list_jobs: added req send for iod %d\n", i);
		} /* end of requesting process code */
		/* else there's no job for this socket */
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "build_list_jobs: done.\n");
	return 0;
}

#undef PCOUNT

/* BUILD_LIST_REQ() - Builds a request for an application 
  *
 * Note: num parameter was only used in group I/O requests and corresponds
 * to the number of the process (0..n)
 *
 * NOTE: NUM IS NO LONGER USED.
 *
 * Also: type is in terms of a jlist type; it must be converted to an 
 * IOD_RW type
 */
int build_list_req(fdesc_p f_p, 
						 ireq_p  r_p,
						 int     file_list_count,
						 int     type)
{
	/* set up request */
	r_p->majik_nr   = IOD_MAJIK_NR;
	r_p->release_nr = CAPFS_RELEASE_NR;
	r_p->type       = IOD_LIST;
	/* dsize includes a list of offsets and one of lengths */
	r_p->dsize      = file_list_count*sizeof(int32_t) + 
		file_list_count*sizeof(int64_t);
	r_p->req.listio.f_ino = f_p->fd.meta.u_stat.st_ino;
	r_p->req.listio.cap   = f_p->fd.cap;
	r_p->req.listio.file_list_count = file_list_count; 

	switch (type) {
	case J_READ:
		r_p->req.listio.rw_type = IOD_RW_READ;
		break;
	case J_WRITE:
		r_p->req.listio.rw_type = IOD_RW_WRITE;
		break;
	default:
		r_p->req.listio.rw_type = IOD_RW_WRITE;
		break;
	}

   LOG(stderr, INFO_MSG, SUBSYS_LIB,  "file_list_count = %d; type = %d\n", file_list_count, type);
	return 0;
} /* end of BUILD_LIST_REQ() */

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
