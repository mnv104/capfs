/*
 * (C) 2001 Pete Wyckoff  <pw@osc.edu>
 *
 * See LIBRARY_COPYING in top-level directory.
 */

/* 
 * Common types shared by both capfs and capfs-kernel.
 */

#ifndef __capfs_types_h
#define __capfs_types_h

typedef uint64_t capfs_handle_t;
typedef uint64_t capfs_off_t;
typedef uint8_t capfs_char_t;

/* arch-identical struct dirent */
enum {
	CAPFSNAMELEN = 1023
};
struct capfs_dirent {
	capfs_handle_t handle;
	capfs_off_t off; /* offset to THIS entry, opaque for mgr */
	capfs_char_t name[CAPFSNAMELEN+1]; /* null terminated */
};


#endif  /* __capfs_types_h */
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
