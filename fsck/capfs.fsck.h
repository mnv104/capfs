/*
 * copyright (c) 2004 Murali Vilayannur
 *
 * Written by Murali Vilayannur
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Contact:  Murali Vilayannur (vilayann@cse.psu.edu)
 */

#ifndef _CAPFS_FSCK_H
#define _CAPFS_FSCK_H

#include <stdio.h>
#ifdef HAVE_SYS_STAT_H
#include <sys/types.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#include <linux/limits.h>
#include "list.h"


#ifndef ERR_MAX
#define ERR_MAX   					256
#endif

#ifndef HOST_MAX
#define HOST_MAX 						1024
#endif

#ifdef DEBUG
#define gprintf(fmt,args...)  \
do {\
	if ((1 << RANK) & node_mask) {\
		fprintf(stdout, "capfs.fsck: %s(%d) -> "fmt, get_host_name(), RANK, ##args);\
		fflush(stdout);\
	}\
} while(0);
#else
#define gprintf(fmt,args...) 
#endif

#define nodeprintf(fmt, args...) \
do {\
	if ((1 << RANK) & node_mask) {\
		fprintf(stdout, "capfs.fsck: %s(%d) -> "fmt, get_host_name(), RANK, ##args);\
		fflush(stdout);\
	}\
} while (0);

#define panic(fmt, args...)   	\
do {\
	fprintf(stderr, "capfs.fsck: %s(%d) -> "fmt, get_host_name(), RANK, ##args);\
	fflush(stderr);\
}while(0);

#define verbatim(fmt, args...)   \
do {\
	fprintf(stdout, fmt, ##args);\
	fflush(stdout);\
}while(0);

/* Minimum and maximum values a `signed long long int' can hold.  */
#ifndef LLONG_MAX
#define LLONG_MAX						9223372036854775807LL
#endif

#ifndef LLONG_MIN
#define LLONG_MIN						(-LLONG_MAX - 1LL)
#endif

/*
 * NOTE: The routines below are non-atomic!
 * But we dont need any atomic versions of these
 * routines!
 */
static inline int test_bit(int nr, void *addr)
{
	unsigned int *p = (unsigned int *) addr;

	return ((p[nr >> 5] >> (nr & 0x1f)) & 1) != 0;
}

static inline void clear_bit(int nr, void *addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);

	*p &= ~mask;
	return;
}

static inline void set_bit(int nr, void *addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);

	*p |= mask;
	return;
}

static inline int find_next_zero_bit(void *addr, int size, int offset)
{
	int i;

	for (i = offset; i < size; i++) {
	  if (!test_bit(i, addr)) {
		  return i;
	  }
  }
	return size;
}

/* Need this to keep track of the mean */
struct LIST_HEAD {
	int64_t total;
	int     count;
	struct list_head head;
};

/* Information about each file in the hierarchy */
struct files {
	/* Full path name of the file */
	char 				  					name[PATH_MAX];
	/* inode number of the file   */
	int64_t 			  					inode;
	/* The level field is used to link struct files both in the tree and in the flist */
	struct list_head 					level; 
	/* fsize makes sense only for files not directories */
	int64_t			  					fsize;
	/* Usually we store the fmeta pointer here for files */
	void									*ptr;
};

/* Phase1 : Request structures to other tasks running on the IOD nodes of the fsck program */

struct iod_phase_args {
	/* Indicate which phase of fsck we are in */
	int 									phase; 
	/* The minimum inode number of a file on the MGR file system from which the encoding begins */
	int64_t 								min_inode;
	/* bitmap of the file system hierarchy */
	int64_t 								bitmap_size;
	unsigned long 						*bitmap;
};

/* Phase1 : Request structures to the task on the MGR node of the fsck program */

struct mgr_phase_args {
	/* Indicates phase response to MGR node */
	int									phase;
	/* Note that the min_inode could be lower than what the MGR sent to the IOD in phase 1*/
	int64_t 								min_inode;
	int64_t 								bitmap_size;
	unsigned long 						*bitmap;
	int64_t 								fsize_size;
	int64_t       						*fsize;
};

/* Parameters to the fsck program */

struct p_args {
	char 									*meta_dir;
	char 									*iod_conf_file;
	int									prompt;
	int									simulate;
};

/* Group the node-specific information into relevant structure */

struct node_info {
	int 									rank, nprocs;
	/* Build a breadth-first traversal of the filesystem (common to iod and mgr nodes) */
	struct list_head 					tree;
	/* Parameters to the program */
	struct p_args    					args;
	union {
		struct {
			/* Relevant information about the PVFS file system */
			int    						niods;
			struct iodtabinfo* 		iodinfo_p;
			/* List of all files (linearization of the filesystem hierarchy) */
			/* mgr linearizes it for each IOD */
			struct LIST_HEAD 			*flist; 
			/* gflist is the list of all files that we have on the MGR */
			struct LIST_HEAD        gflist; 
			/* keep track of the minimum and maximum inode number for each IOD */
			int64_t 						*min_inode, *max_inode;
			/* keep track of the request structures sent to each IOD in phase 1,2 */
			struct iod_phase_args 	*ipargs;
			/* keep track of the request structures received from each IOD in phase 1,2 */
			struct mgr_phase_args 	*mpargs;
		}mgr;
		struct {
			/* List of all files (linearization of the filesystem hierarchy) */
			struct LIST_HEAD 			flist;
			/* keep track of the minimum and maximum inode numbers on the IOD fs */
			int64_t 						min_inode, max_inode;
			/* keep track of the request structure received from the MGR in phase 1,2 */
			struct iod_phase_args 	ipargs;
			/* keep track of the request structure sent to the MGR in phase 1,2 */
			struct mgr_phase_args 	mpargs;
		}iod;
	}node;
};

extern int								node_mask;
extern struct node_info 			info;

#define RANK							(info.rank)
#define NPROCS							(info.nprocs)
#define TREE 							(info.tree)
#define META_DIR 						(info.args.meta_dir)
#define IOD_CONF_FILE				(info.args.iod_conf_file)
#define PROMPT							(info.args.prompt)
#define SIMULATE						(info.args.simulate)

#define MGR_NIODS						(info.node.mgr.niods)
#define MGR_IODINFO_P    			(info.node.mgr.iodinfo_p)
#define MGR_FLIST						(info.node.mgr.flist)
#define MGR_GFLIST					(info.node.mgr.gflist)
#define MGR_MIN_INODE 				(info.node.mgr.min_inode)
#define MGR_MAX_INODE 				(info.node.mgr.max_inode)
#define MGR_IPARGS					(info.node.mgr.ipargs)
#define MGR_MPARGS					(info.node.mgr.mpargs)

#define IOD_FLIST						(info.node.iod.flist)
#define IOD_MIN_INODE				(info.node.iod.min_inode)
#define IOD_MAX_INODE				(info.node.iod.max_inode)
#define IOD_IPARGS         		(info.node.iod.ipargs)
#define IOD_MPARGS					(info.node.iod.mpargs)

static __inline__ char *get_host_name(void)
{
	static char hostname[HOST_MAX];
	if (gethostname(hostname, HOST_MAX) < 0) {
		return NULL;
	}
	return hostname;
}

/* free the struct files queued on the MGR_FLIST */
static __inline__ void dealloc_mgrflist(void)
{
	int i;
	/* walk through the MGR_FLIST and free stuff */
	for (i = 0; i < MGR_NIODS; i++) {
		while (MGR_FLIST[i].head.next != &MGR_FLIST[i].head) {
			struct files *filp;

			filp = list_entry(MGR_FLIST[i].head.next, struct files, level);
			list_del(MGR_FLIST[i].head.next);
			free(filp);
		}
	}
	return;
}

/* free the struct files queued on the MGR_GFLIST.head */
static __inline__ void dealloc_mgrgflist(void)
{
	/* walk through the MGR_GFLIST.head and free stuff */
	while (MGR_GFLIST.head.next != &MGR_GFLIST.head) {
		struct files *filp;

		filp = list_entry(MGR_GFLIST.head.next, struct files, level);
		list_del(MGR_GFLIST.head.next);
		if (filp->ptr) free(filp->ptr);
		free(filp);
	}
	return;
}


/* free the struct files queued on the IOD_FLIST.head */
static __inline__ void dealloc_iodflist(void)
{
	/* walk through the IOD_FLIST.head and free stuff */
	while (IOD_FLIST.head.next != &IOD_FLIST.head) {
		struct files *filp;

		filp = list_entry(IOD_FLIST.head.next, struct files, level);
		list_del(IOD_FLIST.head.next);
		free(filp);
	}
	return;
}

/* capfs_misc.c */
extern char* 	fname(ino_t);
extern int 		create_capfs_dir(char *dir_path);
/* capfs_parse.c */
extern int 		parse_args(int argc, char *argv[]);
extern char* 	check_validity(void);
/* capfs_path.c */
extern int 		do_flatten_hierarchy(char *path);
/* capfs_encode.c */
extern int 		encode_phase1_args(void);
extern int 		send_phase1_args(void);
extern int		recv_phase1_args(void);
extern int64_t decode_phase1_args(unsigned long *offset, int node);
extern int 		encode_phase2_args(int64_t min_inode, int64_t max_inode);
extern int 		send_phase2_args(void);
extern int 		recv_phase2_args(void);
extern int64_t decode_phase2_args(unsigned long *offset);
extern int 		delete_file(int64_t inode_number);
extern int 		valid(int64_t inode_number, int iod_mask, int *pcount);
extern int64_t compute_fsize(int64_t inode_number, int);

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
