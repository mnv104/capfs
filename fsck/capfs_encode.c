/*
 * Copyright(C) 2004
 * Murali Vilayannur
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/unistd.h>
#include <limits.h>

#include <mpi.h>
#include <capfs_config.h>
#include <capfs.h>
#include <meta.h>
#include <metaio.h>
#include <iodtab.h>
#include "list.h"
#include "capfs.fsck.h"


// What are the maximum/minimum inode numbers that we (IODs) have ? 
static int64_t max_iod_inodenumber(void)
{
	struct files *filp;

	if (RANK == 0) {
		return -1;
	}
	if (list_empty(&(IOD_FLIST.head))) {
		return -1;
	}
	filp = list_entry(IOD_FLIST.head.prev, struct files, level);
	return filp->inode;
}

static int64_t min_iod_inodenumber(void)
{
	struct files *filp;

	if (RANK == 0) {
		return -1;
	}
	if (list_empty(&(IOD_FLIST.head))) {
		return -1;
	}
	filp = list_entry(IOD_FLIST.head.next, struct files, level);
	return filp->inode;
}


static inline char *to_string(unsigned long *ptr, int64_t size)
{
	static char *str = NULL;
	static unsigned long count = 0;
	int i;
	if (!str) {
		count = ((size / sizeof(unsigned long))) * BITS_PER_LONG;
		str = (char *)calloc((count + 1), sizeof(char));
		if (!str) {
			return "";
		}
	}
	for (i=0; i < count; i++) {
		*(str + i) = (test_bit(i, ptr) == 0) ? '0':'1';
	}
	return str;
}


/*
 * Our bitmap encoding is a little bit weird.
 * We indicate presence of a file with a 0 and
 * absence with a 1, unlike usual bitmap
 * representations. This helps us in using
 * the assembly routines which can be quite
 * fast, instead of checking every bit manually.
 */

int encode_phase1_args(void)
{
	if (RANK == 0) {
		int i;

		for (i=0; i < MGR_NIODS; i++) {
			MGR_IPARGS[i].phase 		  =	1;
			MGR_IPARGS[i].min_inode   =   (MGR_MIN_INODE[i] == LLONG_MAX) ?    0 : MGR_MIN_INODE[i];
			MGR_IPARGS[i].bitmap_size =   (MGR_MIN_INODE[i] == LLONG_MAX) ?    0 :
				(((MGR_MAX_INODE[i] - MGR_MIN_INODE[i]) / BITS_PER_LONG) + 1) * sizeof(unsigned long);
			MGR_IPARGS[i].bitmap 	  =   (MGR_MIN_INODE[i] == LLONG_MAX) ? NULL : 
											(unsigned long *) calloc(1, MGR_IPARGS[i].bitmap_size);
			gprintf("Encode phase 1 request to rank %d bitmap_size : %lld,\
					min_inode : %lld, max_inode : %lld\n", (i + 1), MGR_IPARGS[i].bitmap_size,
					MGR_IPARGS[i].min_inode, MGR_MAX_INODE[i]);
			if (MGR_IPARGS[i].bitmap) {
				struct list_head *tmp  = NULL;
				
				/* bitmap is filled with 1's */
				memset(MGR_IPARGS[i].bitmap, 255, MGR_IPARGS[i].bitmap_size);
				/* Clear the bits for necessary files alone */
				list_for_each(tmp, &(MGR_FLIST[i].head)) {
					struct files *filp = list_entry(tmp, struct files, level);
					clear_bit((int)(filp->inode - MGR_MIN_INODE[i]), MGR_IPARGS[i].bitmap);
				}
			}
			else {
				panic("Could not calloc MGR_IPARGS[%d].bitmap of size %lld\n",
						i, MGR_IPARGS[i].bitmap_size);
				return -1;
			}
		}
	}
	else { /* Non-zero rank nodes */
		int64_t my_min_inode, my_max_inode, mgr_min_inode, mgr_max_inode;

		IOD_MPARGS.phase = 1;
		/* This means that our FS did not have any entries at all */
		if ((my_min_inode = min_iod_inodenumber()) < 0) {
			/* Send back what we got after making all bitmaps 1 */
			IOD_MPARGS.min_inode = IOD_IPARGS.min_inode;
			IOD_MPARGS.bitmap_size = IOD_IPARGS.bitmap_size;
			if (IOD_MPARGS.bitmap_size != 0) {
				IOD_MPARGS.bitmap = (unsigned long *)calloc(1, IOD_MPARGS.bitmap_size);
				if (IOD_MPARGS.bitmap == NULL) {
					panic("Could not allocate IOD_MPARGS.bitmap of size %lld\n", IOD_MPARGS.bitmap_size);
					return -1;
				}
				/* Declare that you dont have any of the files that the MGR thinks youhave */
				memset(IOD_MPARGS.bitmap, 255, IOD_MPARGS.bitmap_size);
			}
			IOD_MPARGS.fsize_size = 0;
			IOD_MPARGS.fsize = NULL;
			gprintf("Encode phase1 response min_inode: %lld, bitmap_size: %lld,\
					fsize_size: %lld\n", IOD_MPARGS.min_inode, IOD_MPARGS.bitmap_size,
					IOD_MPARGS.fsize_size);
			return 0;
		}
		/* this means that the MGR thinks, that we should not have any file */
		else if (IOD_IPARGS.bitmap_size == 0) {
			my_max_inode = max_iod_inodenumber();
		}
		else { /* Normal */
			mgr_min_inode = IOD_IPARGS.min_inode;
			mgr_max_inode = IOD_IPARGS.min_inode + 
				(IOD_IPARGS.bitmap_size * BITS_PER_LONG) / sizeof(unsigned long);
			my_min_inode  = (my_min_inode < mgr_min_inode) ? my_min_inode : mgr_min_inode;
			my_max_inode  = max_iod_inodenumber();
			my_max_inode  = (my_max_inode > mgr_max_inode) ? my_max_inode : mgr_max_inode;
		}
		IOD_MPARGS.min_inode = my_min_inode;
		IOD_MPARGS.bitmap_size = 
				(((my_max_inode - my_min_inode)/ BITS_PER_LONG) + 1) * sizeof(unsigned long);
		if (IOD_MPARGS.bitmap_size != 0) {
			IOD_MPARGS.bitmap = (unsigned long *)calloc(1, IOD_MPARGS.bitmap_size);
			if (IOD_MPARGS.bitmap == NULL) {
				panic("Could not allocate IOD_MPARGS.bitmap of size %lld\n", IOD_MPARGS.bitmap_size);
				return -1;
			}
		}
		/* Declare that nothing is available. Will be adjusted later if necessary */
		memset(IOD_MPARGS.bitmap, 255, IOD_MPARGS.bitmap_size);
		IOD_MPARGS.fsize_size = (my_max_inode - my_min_inode + 1) * sizeof(int64_t);
		if (IOD_MPARGS.fsize_size != 0) {
			/* set all file sizes to 0 */
			IOD_MPARGS.fsize = (int64_t *)calloc(1, IOD_MPARGS.fsize_size);
			if (IOD_MPARGS.fsize == NULL) {
				panic("Could not allocate IOD_MPARGS.fsize of size %lld\n", IOD_MPARGS.fsize_size);
				return -1;
			}
		}
		gprintf("Encode phase1 response min_inode: %lld,\
				bitmap_size: %lld, fsize_size: %lld, max_inode: %lld\n", IOD_MPARGS.min_inode, IOD_MPARGS.bitmap_size,
				IOD_MPARGS.fsize_size, my_max_inode);
	}
	return 0;
}

// Send it across to your peers 
int send_phase1_args(void)
{
	if (RANK == 0) {
		int i;

		/* Send the bitmap across to other iod's */
		for (i=0; i < MGR_NIODS; i++) {
			/* Send the iod encoded arguments */
			MPI_Send(&MGR_IPARGS[i], sizeof(MGR_IPARGS[i]), 
					MPI_BYTE, i + 1, 0, MPI_COMM_WORLD);
			gprintf("Sent phase1 request to rank %d\n", i + 1);
			/* Send the bitmaps */
			if (MGR_IPARGS[i].bitmap_size != 0) {
				MPI_Send(MGR_IPARGS[i].bitmap, MGR_IPARGS[i].bitmap_size,
						MPI_BYTE, i + 1, 1, MPI_COMM_WORLD);
				gprintf("Sent phase 1 trailer to rank %d of size %lld\n", i + 1,
						MGR_IPARGS[i].bitmap_size);
			}
		}
	}
	else {
		MPI_Send(&IOD_MPARGS, sizeof(IOD_MPARGS),
				MPI_BYTE, 0, 0, MPI_COMM_WORLD);
		gprintf("Sent phase1 response to rank 0\n");
		/* Send the bitmaps */
		if (IOD_MPARGS.bitmap_size != 0) {
			MPI_Send(IOD_MPARGS.bitmap, IOD_MPARGS.bitmap_size,
					MPI_BYTE, 0, 1, MPI_COMM_WORLD);
			gprintf("Sent phase 1 trailer1 of size %lld to rank 0\n", IOD_MPARGS.bitmap_size);
		}
		/* Send the file sizes */
		if (IOD_MPARGS.fsize_size != 0) {
			MPI_Send(IOD_MPARGS.fsize, IOD_MPARGS.fsize_size,
					MPI_BYTE, 0, 2, MPI_COMM_WORLD);
			gprintf("Sent phase 1 trailer2 of size %lld to rank 0\n", IOD_MPARGS.fsize_size);
		}
	}
	return 0;
}

// receive it from your peers 
int recv_phase1_args(void)
{
	if (RANK == 0) {
		int i;

		// Wait for the responses 
		for (i=0; i < MGR_NIODS; i++) {
			MPI_Status status;

			gprintf("Waiting for phase 1 response from rank %d\n", i + 1);
			MPI_Recv(&MGR_MPARGS[i], sizeof(MGR_MPARGS[i]),
					MPI_BYTE, i + 1, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			if (MGR_MPARGS[i].phase != 1) {
				panic("Received invalid phase %d\n", MGR_MPARGS[i].phase);
				return -1;
			}
			// receive any trailing data
			if (MGR_MPARGS[i].bitmap_size != 0) {
				MGR_MPARGS[i].bitmap = (unsigned long *)calloc(1, MGR_MPARGS[i].bitmap_size);
				if (MGR_MPARGS[i].bitmap == NULL) {
					panic("Could not calloc MGR_MPARGS[%d].bitmap of size %lld\n", i, MGR_MPARGS[i].bitmap_size);
					return -1;
				}
				gprintf("Waiting for phase 1 trailer1 of size %lld from rank %d\n",
						MGR_MPARGS[i].bitmap_size, (i + 1));
				MPI_Recv(MGR_MPARGS[i].bitmap, MGR_MPARGS[i].bitmap_size, 
						MPI_BYTE, i + 1, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			}
			if (MGR_MPARGS[i].fsize_size  != 0) {
				MGR_MPARGS[i].fsize = (int64_t *)calloc(1, MGR_MPARGS[i].fsize_size);
				if (MGR_MPARGS[i].fsize == NULL) {
					panic("Could not calloc MGR_MPARGS[%d].fsize of size %lld\n", i, MGR_MPARGS[i].fsize_size);
					return -1;
				}
				gprintf("Waiting for phase 1 trailer2 of size %lld from rank %d\n",
						MGR_MPARGS[i].bitmap_size, i + 1);
				MPI_Recv(MGR_MPARGS[i].fsize, MGR_MPARGS[i].fsize_size, 
						MPI_BYTE, i + 1, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			}
		}// end for()
	}
	else {
		/* receive stuff from rank 0 node */
		MPI_Status status;

		gprintf("Waiting for phase 1 request from rank 0\n");
		MPI_Recv(&IOD_IPARGS, sizeof(IOD_IPARGS),
				MPI_BYTE, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
		if (IOD_IPARGS.phase != 1) {
			panic("Received invalid phase %d\n", IOD_IPARGS.phase);
			return -1;
		}
		if (IOD_IPARGS.bitmap_size != 0) {
			IOD_IPARGS.bitmap = (unsigned long *) calloc(1, IOD_IPARGS.bitmap_size);
			if (IOD_IPARGS.bitmap == NULL) {
				panic("Could not calloc IOD_IPARGS.bitmap of size %lld\n", IOD_IPARGS.bitmap_size);
				return -1;
			}
			gprintf("Waiting for phase 1 trailer of size %lld from rank 0\n", IOD_IPARGS.bitmap_size);
			MPI_Recv(IOD_IPARGS.bitmap, IOD_IPARGS.bitmap_size, 
				MPI_BYTE, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
		}
	}
	return 0;
}

// returns next inode number from the bitmap. Start search at offset. node is used only for rank == 0 
int64_t decode_phase1_args(unsigned long *offset, int node)
{
	unsigned long max_inodes;
	long next_zero_bit; 

	if (RANK == 0) { 
		if (MGR_MPARGS[node].bitmap_size == 0) {
			return -1;
		}
		max_inodes = MGR_MPARGS[node].bitmap_size * BITS_PER_LONG / sizeof(unsigned long);
		if (*offset >= max_inodes) {
			return -1;
		}
		/* No more zero bits probably! */
		next_zero_bit = find_next_zero_bit(MGR_MPARGS[node].bitmap, max_inodes, *offset);
		if (next_zero_bit >= max_inodes) {
			return -1;
		}
		*offset = next_zero_bit + 1;
		return (MGR_MPARGS[node].min_inode + next_zero_bit);
	}
	else {
		if (IOD_IPARGS.bitmap_size == 0) {
			return -1;
		}
		max_inodes = IOD_IPARGS.bitmap_size * BITS_PER_LONG / sizeof(unsigned long);
		if (*offset >= max_inodes) {
			return -1;
		}
		/* No more zero bits probably! */
		next_zero_bit = find_next_zero_bit(IOD_IPARGS.bitmap, max_inodes, *offset);
		if (next_zero_bit >= max_inodes) {
			return -1;
		}
		*offset = next_zero_bit + 1;
		return (IOD_IPARGS.min_inode + next_zero_bit);
	}
}

// Encode phase2 request to your peers 
// NOTE: Encoding in phase 2 is different from that of phase 1
int encode_phase2_args(int64_t min_inode, int64_t max_inode)
{
	if (RANK == 0) {
		int i;

		for (i=0; i < MGR_NIODS; i++) {
			MGR_IPARGS[i].phase 		  =	2;
			MGR_IPARGS[i].min_inode   =   min_inode;
			MGR_IPARGS[i].bitmap_size =   
				(((max_inode - min_inode) / BITS_PER_LONG) + 1) * sizeof(unsigned long);
			/* free the previously allocated bitmap and reallocate new */
			if (MGR_IPARGS[i].bitmap) {
				free(MGR_IPARGS[i].bitmap);
			}
			MGR_IPARGS[i].bitmap 	  =   (unsigned long *)calloc(1, MGR_IPARGS[i].bitmap_size);
			if (MGR_IPARGS[i].bitmap == NULL) {
				panic("Could not allocate MGR_IPARGS[%d].bitmap of size %lld\n", i, MGR_IPARGS[i].bitmap_size);
				return -1;
			}
			/* In the phase2 encoding, we put a 1 to indicate that the file has to be deleted */
			memset(MGR_IPARGS[i].bitmap, 0, MGR_IPARGS[i].bitmap_size);
			gprintf("Encode phase 2 request to rank %d, min_inode: %lld,\
					bitmap_size: %lld, max_inode: %lld\n", i + 1, MGR_IPARGS[i].min_inode,
					MGR_IPARGS[i].bitmap_size, max_inode);
		}
	}
	else { /* Non-zero rank nodes */
		/* phase2 requests are actually dummy from the IOD's perspective */
		IOD_MPARGS.phase = 2;
		IOD_MPARGS.fsize_size = 0;
		if (IOD_MPARGS.fsize) {
			free(IOD_MPARGS.fsize);
		}
		IOD_MPARGS.fsize = NULL;
		IOD_MPARGS.min_inode = 0 ;
		IOD_MPARGS.bitmap_size = 0; 
		if (IOD_MPARGS.bitmap) {
			free(IOD_MPARGS.bitmap);
		}
		IOD_MPARGS.bitmap = NULL;
		gprintf("Encode phase 2 response to rank 0\n");
	}
	return 0;
}

// Send it across to your peers 
int send_phase2_args(void)
{
	if (RANK == 0) {
		int i;

		/* Send the action bitmap across to other iod's */
		for (i=0; i < MGR_NIODS; i++) {
			/* Send the iod encoded arguments */
			MPI_Send(&MGR_IPARGS[i], sizeof(MGR_IPARGS[i]), 
					MPI_BYTE, i + 1, 0, MPI_COMM_WORLD);
			gprintf("Sent phase 2 request to rank %d\n", i + 1);
			/* Send the bitmaps */
			if (MGR_IPARGS[i].bitmap_size != 0) {
				MPI_Send(MGR_IPARGS[i].bitmap, MGR_IPARGS[i].bitmap_size,
						MPI_BYTE, i + 1, 1, MPI_COMM_WORLD);
				gprintf("Sent phase 2 trailer to rank %d, bitmap_size %lld\n",
						i + 1, MGR_IPARGS[i].bitmap_size);
			}
		}
	}
	else {
		MPI_Send(&IOD_MPARGS, sizeof(IOD_MPARGS),
				MPI_BYTE, 0, 0, MPI_COMM_WORLD);
		gprintf("Send phase 2 response to rank 0\n");
		/* No bitmaps or file sizes now */
	}
	return 0;
}

// receive it from your peers 
int recv_phase2_args(void)
{
	if (RANK == 0) {
		int i;

		gprintf("Waiting for phase 2 responses from all IOD tasks\n");
		// Wait for the responses 
		for (i=0; i < MGR_NIODS; i++) {
			MPI_Status status;
			
			if (MGR_MPARGS[i].bitmap) {
				free(MGR_MPARGS[i].bitmap);
			}
			MGR_MPARGS[i].bitmap = NULL;
			if (MGR_MPARGS[i].fsize) {
				free(MGR_MPARGS[i].fsize);
			}
			MGR_MPARGS[i].fsize = NULL;
			gprintf("Waiting for phase 2 response from rank %d\n", i + 1);
			MPI_Recv(&MGR_MPARGS[i], sizeof(MGR_MPARGS[i]),
					MPI_BYTE, i + 1, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			if (MGR_MPARGS[i].phase != 2) {
				panic("Invalid phase received %d\n", MGR_MPARGS[i].phase);
				return -1;
			}
			/* No bitmaps and/or file sizes in phase 2 */
		}// end for()
	}
	else {
		/* receive stuff from rank 0 node */
		MPI_Status status;

		/* free the previously allocated bitmap and reallocate */
		if (IOD_IPARGS.bitmap) 
			free(IOD_IPARGS.bitmap);
		gprintf("Waiting for phase 2 request from rank 0\n");
		MPI_Recv(&IOD_IPARGS, sizeof(IOD_IPARGS),
				MPI_BYTE, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
		if (IOD_IPARGS.phase != 2) {
			panic("Received invalid phase in request %d\n", IOD_IPARGS.phase);
			return -1;
		}
		if (IOD_IPARGS.bitmap_size != 0) {
			IOD_IPARGS.bitmap = (unsigned long *)calloc(1, IOD_IPARGS.bitmap_size);
			if (IOD_IPARGS.bitmap == NULL) {
				panic("Could not allocate IOD_IPARGS.bitmap of size %lld\n", IOD_IPARGS.bitmap_size);
				return -1;
			}
			gprintf("Waiting for phase 2 trailer from rank 0 bitmap_size: %lld\n", IOD_IPARGS.bitmap_size);
			MPI_Recv(IOD_IPARGS.bitmap, IOD_IPARGS.bitmap_size, 
				MPI_BYTE, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
		}
	}
	return 0;
}

// returns next inode number from the bitmap. Start search at offset.
int64_t decode_phase2_args(unsigned long *offset)
{
	unsigned long max_inodes;

	if (RANK == 0) { 
		return -1;
	}
	else {
		if (IOD_IPARGS.bitmap_size == 0) {
			return -1;
		}
		max_inodes = IOD_IPARGS.bitmap_size * BITS_PER_LONG / sizeof(unsigned long);
		while (*offset < max_inodes) {
			if (test_bit(*offset, IOD_IPARGS.bitmap) != 0) {
				return (IOD_IPARGS.min_inode + *offset);
			}
			*offset++;
		}
		return -1;
	}
}

/*
 * Remember that a 1 as return value indicates that
 * the file is not present on the IOD and a 0 indicates
 * a presence
 */
int get_bit(int which_iod, int64_t inode_number)
{
	unsigned long which_bit = 0;

	/* if bitmap_size of the IOD was 0, then also we indicate an absence of the file */
	if (MGR_MPARGS[which_iod].bitmap_size == 0) {
		return 1;
	}
	/* Make sure inode_number is > min_inode of this particular IOD */
	if (MGR_MPARGS[which_iod].min_inode > inode_number) {
		return 1;
	}
	/* Make sure it fits within our bounds also */
	if ((which_bit = (inode_number - MGR_MPARGS[which_iod].min_inode)) 
			>= MGR_MPARGS[which_iod].bitmap_size * BITS_PER_LONG / sizeof(unsigned long)) {
		return 1;
	}
	/* whew. Now check if the bit is actually set or not */
	return test_bit(which_bit, MGR_MPARGS[which_iod].bitmap) == 0 ? 0 : 1;
}

/* File may not be present on all IOD's */
int64_t compute_fsize(int64_t inode_number, int iod_mask)
{
	unsigned long which_fsize = 0;
	int i;
	int64_t total_size = 0;
	
	for (i=0; i < MGR_NIODS; i++) {
		/* Do we take the size for IOD i? */
		if ((1 << i) & iod_mask) {
			/* if bitmap_size of the IOD was 0, then also we indicate an absence of the file */
			if (MGR_MPARGS[i].fsize_size == 0) {
				return -1;
			}
			/* Make sure inode_number is > min_inode of this particular IOD */
			if (MGR_MPARGS[i].min_inode > inode_number) {
				return -1;
			}
			/* Make sure it fits within our bounds also */
			if ((which_fsize = (inode_number - MGR_MPARGS[i].min_inode)) 
					>= MGR_MPARGS[i].fsize_size / sizeof(int64_t)) {
				return -1;
			}
			total_size += MGR_MPARGS[i].fsize[which_fsize];
		}
	}
	return total_size;
}

/*
 * By setting a bit for this file as a phase2 request to the IODs
 * we are asking them to delete that file.
 */
int delete_file(int64_t inode_number)
{
	unsigned long which_bit = 0;
	int i;

	if (RANK != 0) {
		return -1;
	}
	for (i=0; i < MGR_NIODS; i++) {
		/* havent we encoded_phase2_args() */
		if (MGR_IPARGS[i].bitmap_size == 0) {
			panic("We havent encoded phase2 args yet!\n");
			return -1;
		}
		/* Make sure inode_number is > min_inode of this particular IOD */
		if (MGR_IPARGS[i].min_inode > inode_number) {
			panic("min_inode error: We havent encoded phase2 args yet!\n");
			return -1;
		}
		/* Make sure it fits within our bounds also */
		if ((which_bit = (inode_number - MGR_IPARGS[i].min_inode)) 
			>= MGR_IPARGS[i].bitmap_size * BITS_PER_LONG / sizeof(unsigned long)) {
			continue;
		}
		/* whew. Now set the bit */
		set_bit(which_bit, MGR_IPARGS[i].bitmap);
	}
	return 0;
}

/*
 * Okay, what constitutes a valid bitmap in phase 2.
 * If length denotes the number of IOD's participating 
 * in this file system, then the valid bitmaps
 * are those in which there cannot be a discontinuity 
 * in bit occurrence or absence.
 * Suppose there are 4 iods and a 0 is used to indicate
 * presence of a file and 1 the absence
 * Lets say that for a particular file f1,
 * IOD1 bitmap is i1, IOD2 bitmap is i2, IOD3 bitmap is i3, IOD4 bitmap 
 * is i4.Now we have 2 ^ 4 combinations of (i1, i2, i3, i4).
 * out of which 1010, 0101 are the combinations which are not valid
 * because of the discontinuity.
 * If the bit pattern is invalid, we return -2
 * If the bit pattern had all zeros, then we return -1,
 * else we return the likely starting position.
 * iod_mask is a bitwise or of the IOD nodes on
 * which the file is expected to be striped on.
 * So if we want to make sure file is striped on IOD nodes
 * 0 and 1, then iod_mask = 2 ^ 0 + 2 ^ 1 = 3.
 * If iod_mask is set to -1, then we just check for
 * validity of the bit pattern.
 * Return values are as follows
 * -3 : Badness
 * -2 : Invalid/Inadmissible bit pattern
 * -1 : No IOD has this file.
 * 0,+ve: Possible starting IOD number(base) of the striped file. 
 * If base is valid, then pcount is also valid.
 */
int valid(int64_t inode_number, int iod_mask, int *pcount)
{
	int pos = -1, oldpos = -1, flag = 0, i, start = -1, startflag = 0, *iod_flags = NULL;

	if (RANK != 0) { /* Make sure it is called by 0 rank only */
		return -2;
	}
	if (pcount) 
		*pcount = 0;
	iod_flags = (int *)calloc(MGR_NIODS, sizeof(int));
	if (!iod_flags) {
		panic("Could not allocate flags of size %d\n", sizeof(int) * MGR_NIODS);
		return -3;
	}
	for(i = 0; i < MGR_NIODS; i++) {
		int bit;

		/* Rollover flag has been set */
		if (flag == 1) {
			/* We broke the sequence */
			if ((bit = get_bit(i, inode_number)) != 0) {
				free(iod_flags);
				panic("Invalid bitmask: Breaks the sequence!\n");
				return -2;
			}
		}
		/* ah. this inode is present on the IOD i */
		if ((bit = get_bit(i, inode_number)) == 0) {
			iod_flags[i] = 1;
			if (pcount)
				*pcount++;
			if (start < 0 || startflag == 1) {
				start = i;
				startflag = 0;
			}
			/* possible start of a sequence of 0's */
			pos = i;
			if (oldpos >= 0) {
				/* Since, the current striping scheme does not allow jumps,
				 * but only roll-arounds, the difference between 2 successive
				 * 0's in the bitmaps cannot be greater than 1 except in the 
				 * roll-over case 
				 */
				if (abs(pos - oldpos) > 1) {
					flag = 1;
				}
			}
			oldpos = pos;
		}
		else { /* this inode number is not present on iod i */
			if (start >= 0) {
				startflag = 1;
			}
		}
	}
	/* Rollover flag has been set */
	if (flag == 1) {
		/* Check if IOD 0 has this bit set */
		if (get_bit(0, inode_number) != 0) {
			free(iod_flags);
			panic("Invalid bitmask! Did not rollover\n");
			return -2;
		}
	}
	/* Now that we have verified that the bit pattern is itself valid,
	 * we now check if it matches the iod_mask specified by the caller
	 */
	if (iod_mask >= 0) {
		for (i=0; i < MGR_NIODS; i++) {
			if ((1 << i) & iod_mask) {
				/* IOD i is participating in the striping of this file? */
				if (iod_flags[i] != 1) {
					free(iod_flags);
					/* oops! IOD i does not have this file.. Ask everyone to delete file */
					panic("IOD %d does not have file %lld\n", i, inode_number);
					return -2;
				}
			}
			else {
				/* IOD i is not participating in the striping of this file */
				if (iod_flags[i] == 1) {
					/* then why is iod_flags set to 1? */
					panic("Why is IOD %d having this file %lld\n", i, inode_number);
					/* FIXME: Not doing anything for this currently */
				}
			}
		}
	}
	free(iod_flags);
	return start;
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
