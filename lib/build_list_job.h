/*
 * (C) 2005 Penn State University 
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#ifndef BUILD_LIST_JOB_H
#define BUILD_LIST_JOB_H

/* CAPFS INCLUDES */
#include <desc.h>
#include <req.h>

#ifdef __cplusplus
extern "C" {
#endif
	
	int list_check(int     mem_list_count,
						int     mem_lengths[],
						int     file_list_count,
						int64_t file_offsets[],
						int32_t file_lengths[]);
	int get_mem_end_offset(int     mem_start,
									 int     mem_begin_offset,
									 int     *tmp_mem_list_count,
									 int32_t mem_lengths[],
									 int     file_start,
									 int     file_end,
									 int32_t file_lengths[],
									 int     *total_size);
	int check_list_size(int     mem_list_count, 
							  int     mem_lengths[], 
							  int     file_list_count, 
							  int32_t file_lengths[]);
	int multiple_build_list_jobs(fdesc_p f_p, 
										  int     mem_list_count, 
										  char   *mem_offsets[],
										  int     mem_lengths[],
										  int     file_list_count,
										  int64_t file_offsets[],
										  int32_t file_lengths[],
										  int     type,
										  int     cutoff);
	int build_list_jobs(fdesc_p f_p, 
							  int     mem_list_count, 
							  char   *mem_offsets[],
							  int     mem_lengths[],
							  int     file_list_count,
							  int64_t file_offsets[],
							  int32_t file_lengths[],
							  int     type,
							  int     first_mem_offset,
							  int     total_size);
	int build_list_req(fdesc_p f_p, 
							 ireq_p  r_p,
							 int     file_list_count,
							 int     type);

#ifdef __cplusplus
}
#endif


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
