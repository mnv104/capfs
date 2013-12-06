/*
 * copyright (c) 2004 Murali Vilayannur
 *
 * Written by Murali Vilayannur (vilayann@cse.psu.edu)
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

struct node_info	 	info;
/*
 * Node_mask is a bitwise OR of node numbers whose print statements we are interested in seeing 
 * For instance, if we wished to see all print statements of nodes with ranks 2,3 and 6
 * then node_mask would be (2 ^ 2 | 2 ^ 3 | 2 ^ 6 = 76)
 */
int     					node_mask = 31;

static void dealloc_mgr_args(void)
{
	if (RANK != 0) {
		return;
	}
	if (MGR_FLIST) {
		dealloc_mgrflist();
		free(MGR_FLIST);
	}
	MGR_FLIST = NULL;
	dealloc_mgrgflist();
	if (MGR_MIN_INODE) {
		free(MGR_MIN_INODE);
	}
	MGR_MIN_INODE = NULL;
	if (MGR_MAX_INODE) {
		free(MGR_MAX_INODE);
	}
	MGR_MAX_INODE = NULL;
	if (MGR_IPARGS)   {
		int i;
		for (i = 0; i < MGR_NIODS; i++) {
			if (MGR_IPARGS[i].bitmap) {
				free(MGR_IPARGS[i].bitmap);
			}
			MGR_IPARGS[i].bitmap = NULL;
			MGR_IPARGS[i].bitmap_size = 0;
		}
		free(MGR_IPARGS);
	}
	MGR_IPARGS = NULL;
	if (MGR_MPARGS)  {
		int i;
		for (i = 0; i < MGR_NIODS; i++) {
			if (MGR_MPARGS[i].bitmap) {
				free(MGR_MPARGS[i].bitmap);
			}
			MGR_MPARGS[i].bitmap = NULL;
			MGR_MPARGS[i].bitmap_size = 0;

			if (MGR_MPARGS[i].fsize) {
				free(MGR_MPARGS[i].fsize);
			}
			MGR_MPARGS[i].fsize = NULL;
			MGR_MPARGS[i].fsize_size = 0;
		}
		free(MGR_MPARGS);
	}
	MGR_MPARGS = NULL;
	return;
}

static void dealloc_iod_args(void)
{
	if (RANK == 0) {
		return;
	}
	dealloc_iodflist();
	if (IOD_IPARGS.bitmap) {
		free(IOD_IPARGS.bitmap);
	}
	IOD_IPARGS.bitmap = NULL;
	IOD_IPARGS.bitmap_size = 0;
	if (IOD_MPARGS.bitmap) {
		free(IOD_MPARGS.bitmap);
	}
	IOD_MPARGS.bitmap = NULL;
	IOD_MPARGS.bitmap_size = 0;
	if (IOD_MPARGS.fsize) {
		free(IOD_MPARGS.fsize);
	}
	IOD_MPARGS.fsize = NULL;
	IOD_MPARGS.fsize_size = 0;
	return;
}


/* Process the MGR sent bitmap and encode response */
static int do_aux_step1(void)
{
	struct list_head *tmp = NULL;
	int64_t bitmap_inode = 0;
	int check_bitmap;
	unsigned long offset;

	if (RANK == 0) { /* should not be invoked by rank 0 */
		return 0;
	}
	/* Allocate memory for the bitmaps and file sizes that is going to be sent back */
	if (encode_phase1_args() < 0) {
		return -1;
	}
	check_bitmap = 1;
	offset = 0;
	/* Did the MGR think, we should not have any files? */
	if (IOD_IPARGS.bitmap_size == 0) {
		check_bitmap = 0;
		return 0;
	}
	else {
		bitmap_inode = decode_phase1_args(&offset, 0);
	}
	/* 
	 * Begin Pass 1
	 * We make a note of all those files, which were missing
	 * to the MGR node. In addition, we make a list of all those
	 * files which we have as well and send it to the MGR.
	 * The MGR will make a decision on what to do with those
	 * files, and send back a request. Based on that we
	 * do some actions and that concludes the recovery.
	 * Essentially, it is a 2-phase recovery process.
	 */
	for (tmp = IOD_FLIST.head.next; tmp != &IOD_FLIST.head;) {
		struct files *filp = list_entry(tmp, struct files, level);

		if (check_bitmap == 0) {
			nodeprintf("(lost+found) file \"%s\"\
					is a candidate for recovery on MGR node\n", filp->name);
			/* Update the size and the bitmap */
			clear_bit(filp->inode - IOD_MPARGS.min_inode, IOD_MPARGS.bitmap);
			IOD_MPARGS.fsize[filp->inode - IOD_MPARGS.min_inode] = filp->fsize;
			tmp = tmp->next;
			continue;
		}
		else {
			if (filp->inode < bitmap_inode) {
				nodeprintf("(lost+found) file \"%s\"\
						is a candidate for recovery on MGR node\n", filp->name);
				/* Update the size and the bitmap */
				clear_bit(filp->inode - IOD_MPARGS.min_inode, IOD_MPARGS.bitmap);
				IOD_MPARGS.fsize[filp->inode - IOD_MPARGS.min_inode] = filp->fsize;
				tmp = tmp->next;
				continue;
			}
			/* gr8. Continue through the linked list and advance bitmap_inode as well */
			else if (filp->inode == bitmap_inode) {
				gprintf("(found) file \"%s\" is OK\n", filp->name);
				/* Update the size and the bitmap */
				clear_bit(filp->inode - IOD_MPARGS.min_inode, IOD_MPARGS.bitmap);
				IOD_MPARGS.fsize[filp->inode - IOD_MPARGS.min_inode] = filp->fsize;
				/* Advance the bitmap */
				if ((bitmap_inode = decode_phase1_args(&offset, 0)) < 0) {
					check_bitmap = 0;
				}
				tmp = tmp->next;
				continue;
			}
			/* hmm.. We dont know anything about this file? So we print something about file not being
			 * there and crib loudly.
			 */
			else if (filp->inode > bitmap_inode) {
				nodeprintf("(lost) do not know \
						anything about file \"%s\"!\n", fname(bitmap_inode));
				if ((bitmap_inode = decode_phase1_args(&offset, 0)) < 0) {
					check_bitmap = 0;
				}
				/* Note: we dont advance here! We wait for the bitmap to catch up with us */
				continue;
			}
		}
	}
	/*
	 * By the end of this, we should essentially have the file sizes
	 * and bitmaps updated for all the files that we have and for 
	 * all the files that the manager thinks we have
	 */
	return 0;
}

/* Step 1 */
static int do_step1(void)
{
	if (RANK == 0) {
		/* Encode arguments to each IOD */
		if (encode_phase1_args() < 0) {
			perror("encode_phase1_iodargs:");
			return -1;
		}
		/* Send the arguments to the IOD nodes */
		if (send_phase1_args() < 0) {
			perror("send_phase1_args:");
			return -1;
		}
		/* receive response from them */
		if (recv_phase1_args() < 0) {
			perror("recv_phase1_args:");
			return -1;
		}
	}
	else {
		/* Receive bitmaps from MGR */
		if (recv_phase1_args() < 0) {
			perror("recv_phase1_args:");
			return -1;
		}
		/* process and encode response */
		if (do_aux_step1() < 0) {
			perror("do_aux_step1:");
			return -1;
		}
		/* Send it back */
		if (send_phase1_args() < 0) {
			perror("send_phase1_args:");
			return -1;
		}
	}
	return 0;
}

static int present_in_mgr(int64_t ino, struct list_head **tmp)
{
	/* Remember that it is sorted */
	list_for_each((*tmp), &MGR_GFLIST.head) {
		struct files *filp;

		filp = list_entry((*tmp), struct files, level);
		if (filp->inode == ino) {
			return 1;
		}
		else if (filp->inode > ino) {
			break;
		}
	}
	return 0;
}

static int64_t max_mgr_inode_number(void)
{
	struct files *filp;

	if (RANK != 0) {
		return -1;
	}
	if (list_empty(&MGR_GFLIST.head)) {
		return -1;
	}
	filp = list_entry(MGR_GFLIST.head.prev, struct files, level);
	return filp->inode;
}

static int64_t min_mgr_inode_number(void)
{
	struct files *filp;

	if (RANK != 0) {
		return -1;
	}
	if (list_empty(&MGR_GFLIST.head)) {
		return -1;
	}
	filp = list_entry(MGR_GFLIST.head.next, struct files, level);
	return filp->inode;
}

static int do_aux_step2(char *path)
{
	int64_t global_min_inode = LLONG_MAX, global_max_inode = LLONG_MIN, i;
	struct list_head *tmp = NULL;

	if (RANK != 0) {
		/* So based on the bitmaps
		 * that we receive, we have 
		 * to delete a few files and that
		 * is the end of it 
		 */
		/* Note: It is possible, our unlinks may not actually succeed, but we ignore errors here */
		int64_t inode;
		unsigned long offset = 0;

		/* We need to hunt for 1's rather than 0's in the bitmap now in phase 2 */
		while ((inode = decode_phase2_args(&offset)) >= 0) {
			nodeprintf("Deleting file %s/%s\n", path, fname(inode));
			if (!SIMULATE) {
				char cmd[PATH_MAX], c = 'y';
				
				if (PROMPT) {
					panic("Deleting file %s/%s. Press \"y\" or \"n\"\n", path, fname(inode));
					scanf("%c", &c);
				}
				if (c == 'y') {
					snprintf(cmd, PATH_MAX, "rm -f %s/%s", path, fname(inode));
					system(cmd);
				}
			}
		}
		return 0;
	}
	/* on the rank 0 node, calculate the global minimum and maximum inode numbers */
	for (i = 0; i < MGR_NIODS; i++) {
		int64_t gmin, gmax;
	
		if (MGR_MPARGS[i].bitmap_size != 0) {
			gmin = MGR_MPARGS[i].min_inode;
			gmax = gmin + (MGR_MPARGS[i].bitmap_size * BITS_PER_LONG / sizeof(unsigned long));
			global_min_inode = (gmin < global_min_inode) ? gmin : global_min_inode;
			global_max_inode = (gmax > global_max_inode) ? gmax : global_max_inode;
		}
	}
	global_min_inode = ((i = min_mgr_inode_number()) < global_min_inode) ? i : global_min_inode;
	global_max_inode = ((i = max_mgr_inode_number()) > global_max_inode) ? i : global_max_inode;
	/* Also compare it to the maximum /minimum inode numbers that we have locally */
	if (global_min_inode == LLONG_MAX 
			|| global_max_inode == LLONG_MIN) {
		panic("Could not determine the global max inode number and min inode number\n");
		return -1;
	}
	/* Encode phase 2 response now based on the global minimum and maximum inode numbers */
	if (encode_phase2_args(global_min_inode, global_max_inode) < 0) {
		panic("Could not encode phase 2 request gmin:%lld gmax:%lld\n", global_min_inode, global_max_inode);
		return -1;
	}
	/*
	 * This is a very inefficient implementation. Right now I am ignoring performance
	 * for the sake of finishing the functionality first. We should exploit sparseness
	 * in the inode number space.
	 */
	for (i = global_min_inode; i <= global_max_inode; i++) {
		int mgr_present, base, pcount;
		
		mgr_present = present_in_mgr(i, &tmp);
		/* Not present in MGR FS */
		if (!mgr_present) {
			/* Possible candidate for recovery */
			base = valid(i, -1, &pcount);
			if (base == -3) {
				panic("1) badness in checking file validity\n");
				return -1;
			}
			else if (base == -2) { /* Inadmissible bit pattern */
				nodeprintf("Inadmissible IOD bit pattern. Asking IOD's to delete file %lld\n", i);
				/* Set a bit for all IOD's for this file in the phase2 request */
				if (!SIMULATE) {
					char c = 'y';

					if (PROMPT) {
						panic("For file with inode %lld, there was an inadmissible IOD bit pattern.\
								Delete file on IODs? Press \"y\" or \"n\"\n", i);
						scanf("%c", &c);
					}
					if (c == 'y') {
						if (delete_file(i) < 0) {
							panic("Could not set bit to ask IODs to delete their files\n");
							return -1;
						}
					}
				}
			}
			else if (base >= 0) { /* possible starting base for recovering this file */
				nodeprintf("Trying to recover file %lld, base:%d, pcount:%d\n", i, base, pcount);
				/* write out the meta-data for this file in lost+found and continue */
				if (!SIMULATE) {
					char name[PATH_MAX];
					struct fmeta meta;
					struct stat sb;
					int fd;

					snprintf(name, PATH_MAX, "%s/lost+found/r%lld", path, i);
					/* FIXME: it is a little hard to calculate stripe size I think */
					memset(&meta, 0, sizeof(meta));
					meta.p_stat.base = base;
					meta.p_stat.pcount = pcount;
					meta.p_stat.ssize = DEFAULT_SSIZE;

					if ((fd = meta_creat(name, O_RDWR)) < 0) {
						panic("Could not recover: Creating meta data file %s\
								failed %s\n", name, strerror(errno));
						return -1;
					}
					else {
						if (fstat(fd, &sb) < 0) {
							panic("Could not recover: fstat'ing meta data file %s\
									failed %s\n", name, strerror(errno));
						}
						else {
							COPY_STAT_TO_PSTAT(&meta.u_stat, &sb);
							meta.u_stat.st_uid = meta.u_stat.st_gid = 0;
							meta.u_stat.st_mode = S_IFREG | (S_IRWXO | S_IRWXG | S_IRWXU);
							if (meta_write(fd, &meta) < 0) {
								panic("Could not recover: meta_write %s\
										failed %s\n", name, strerror(errno));
								meta_close(fd);
								return -1;
							}
							else {
								nodeprintf("Recovered file %lld as %s\n", i, name);
							}
						}
						meta_close(fd);
					}
				}
			}
			/* if no IOD's had this file, then nothing needs to be done */
		}
		/* Present in MGR FS */
		else {
			/* Check if the file is present in all necessary iod's */
			struct files *filp = list_entry(tmp, struct files, level);
			struct fmeta *meta = filp->ptr;

			/* Strange error */
			if (!meta) {
				nodeprintf("%s could not be verified for consistency!\
						Deleting file.\n", filp->name);
				if (!SIMULATE) {
					char c = 'y', cmd[PATH_MAX];

					if (PROMPT) {
						panic("%s could not be verified for consistency!\
								Delete file on MGR and IODs? Press \"y\" or \"n\"\n", filp->name);
						scanf("%c", &c);
					}
					if (c == 'y') {
						snprintf(cmd, PATH_MAX, "rm -f %s", filp->name);
						system(cmd);
						/* delete the file on the IOD's as well */
						if (delete_file(i) < 0) {
							panic("Could not set bit to ask IODs to delete their files\n");
							return -1;
						}
						/* Pull this filp off the queue and free it */
						list_del(tmp);
						if (filp->ptr) free(filp->ptr);
						free(filp);
					}
				}
			}
			else { /* ok good. Verify presence */
				int iod_mask = 0, j;

				/* Build the iod mask for verification */
				for (j = meta->p_stat.base;
						j < (meta->p_stat.base + meta->p_stat.pcount); j++) {
					iod_mask |= (1 << (j % MGR_NIODS));
				}
				/* check if the file is valid across all the IOD's */
				base = valid(i, iod_mask, &pcount);
				if (base == -3) {
					panic("2) badness in checking file validity\n");
					return -1;
				}
				else if (base == -2 || base == -1) { /* Inadmissible bit pattern or some/all IOD lost its strip */
					nodeprintf("Inadmissible IOD bit pattern or some/all IODs lost its strip.\
								Asking MGR & IOD's to delete file %lld\n", i);
					/* Set a bit for all IOD's for this file in the phase2 request */
					if (!SIMULATE) {
						char c = 'y', cmd[PATH_MAX];

						if (PROMPT) {
							panic("For file %s, there was either an inadmissible IOD bit pattern.\
									or some/all IOD lost this file's strip.\
									Delete file on MGR & IODs? Press \"y\" or \"n\"\n", filp->name);
							scanf("%c", &c);
						}
						if (c == 'y') {
							snprintf(cmd, PATH_MAX, "rm -f %s", filp->name);
							system(cmd);
							/* delete the file on the IOD's as well */
							if (delete_file(i) < 0) {
								panic("Could not set bit to ask IODs to delete file\n");
								return -1;
							}
							/* Pull this filp off the queue and free it */
							list_del(tmp);
							free(filp);
						}
					}
				}
				else { /* all ok */
					gprintf("file %s of size %lld was verified\
							to be OK!\n", filp->name, compute_fsize(filp->inode, iod_mask));
				}
			}
		} /* end present in MGR */
	}/* end for() loop */
	return 0;
}

/* Now, we have the responses from all the IOD's.
 * Compare them and initiate appropriate action 
 */
static int do_step2(char *path)
{
	if (RANK == 0) {
		/* start phase2 processing */
		if (do_aux_step2(path) < 0) {
			perror("do_aux_step2:");
			return -1;
		}
		/* Send back bitmaps to IODs */
		if (send_phase2_args() < 0) {
			perror("send_phase2_args:");
			return -1;
		}
		/* wait for response */
		if (recv_phase2_args() < 0) {
			perror("recv_phase2_args:");
			return -1;
		}
	}
	else {
		/* wait for the bitmaps */
		if (recv_phase2_args() < 0) {
			perror("recv_phase2_args:");
			return -1;
		}
		/* Do the action necessary */
		if (do_aux_step2(path) < 0) {
			perror("do_aux_step2:");
			return -1;
		}
		/* encode response */
		if (encode_phase2_args(0, 0) < 0) {
			perror("encode_phase2_args:");
			return -1;
		}
		/* send back response */
		if (send_phase2_args() < 0) {
			perror("send_phase2_args:");
			return -1;
		}
	}
	return 0;
}

/* fsck:
 * We have designed the PVFS fsck process as a 2-phase recovery kind of protocol.
 * The task running with rank 0 serves to coordinate the whole process, since
 * it is running on the MGR's meta-data file system.
 * Comments before each routine invoked here illustrate the protocol a little.
 */
static int do_fsck(char *path)
{
	int ret;

	nodeprintf("fscking on %s\n", path);
	/*
	 * Traverse the file system hierarchy in a breadth-first manner and 
	 * prune out unnecessary files, and flatten the hierarchy into a singly-linked
	 * list sorted on inode numbers (at the MGR/rank 0 node), and sorted
	 * on filenames (at the IOD/rank non-zero nodes).
	 */
	if ((ret = do_flatten_hierarchy(path)) < 0)   {
		perror("do_fsck: do_flatten_hierarchy:");
		return ret;
	}
	nodeprintf("Finished flattening hierarchy on %s\n", path);
	/*
	 * Step 1 procedure:  
	 * Rank 0 --> Encode the information about all files under the PVFS-meta 
	 * data directory in iod_phase_request and send it to all the other rank
	 * nodes. Wait for a response from each of them before initiating step2.
	 * Rank 1 --> Wait for a request from the rank 0 task. Service it
	 * appropriately, and send back a response to the rank 0 task.
	 */
	if ((ret = do_step1()) < 0) {
		perror("do_fsck: do_step1:");
		return ret;
	}
	nodeprintf("Finished Step 1 of recovery on %s\n", path);
	/*
	 * Step 2 procedure:
	 * Rank 0 --> Compare the responses from all the IODs. Decide on what action
	 * the other tasks on the IOD should take. This could involve either deleting
	 * the file or holding the file. Send a phase 2 request to all tasks.
	 * Wait for a response from all. Declare end of fsck.
	 * Rank 1 --> Wait for a phase 2 request from rank 0. 
	 * Execute actions specified in request. Send back ack to MGR.
	 */
	if ((ret = do_step2(path)) < 0) {
		perror("do_fsck: do_step2:");
		return ret;
	}
	nodeprintf("Finished Step 2 of recovery on %s\n", path);
	return 0;
}

/* Initialize and cleanup node_specific information */

static inline void node_specific_dealloc(void)
{
	switch (RANK) {
		case 0:
			dealloc_mgr_args();
			break;
		default:
			dealloc_iod_args();
			break;
	}
	return;
}

static int node_specific_alloc(int argc, char *argv[])
{
	MPI_Comm_rank(MPI_COMM_WORLD, &RANK);
	MPI_Comm_size(MPI_COMM_WORLD, &NPROCS);
	INIT_LIST_HEAD(&info.tree);
	if (parse_args(argc, argv) < 0) {
		return -1;
	}
	switch (RANK) {
		case 0:
			MGR_NIODS = 0;
			MGR_IODINFO_P = NULL;
			MGR_FLIST = NULL;
			INIT_LIST_HEAD(&MGR_GFLIST.head);
			MGR_MIN_INODE = NULL;
			MGR_MAX_INODE = NULL;
			MGR_IPARGS = NULL;
			MGR_MPARGS = NULL;
			break;
		default:
			INIT_LIST_HEAD(&IOD_FLIST.head);
			memset(&IOD_MPARGS, 0, sizeof(struct mgr_phase_args));
			memset(&IOD_IPARGS, 0, sizeof(struct iod_phase_args));
			IOD_MIN_INODE = LLONG_MAX;
			IOD_MAX_INODE = LLONG_MIN;
			break;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	char *path = NULL;

	MPI_Init(&argc, &argv);
	/* Initialize according to the rank of this task */
	if (node_specific_alloc(argc, argv) < 0) {
		node_specific_dealloc();
		MPI_Finalize();
		return -1;
	}
	/*
	 * Each task of the MPI program is designated to work either on the
	 * the MGR meta-data directory or the IOD-data directory of the
	 * machine on which they are running on currently.
	 * Application with rank 0 will work on the MGR-meta data
	 * directory, while the rest work on the IOD-data directory
	 */
	if ((path = check_validity()) == NULL) {
		if (RANK == 0) {
			panic("Skipping fsck because of errors in parameters\n");
		}
		node_specific_dealloc();
		MPI_Finalize();
		return -1;
	}
	/* launch the fsck process */
	if (do_fsck(path) < 0) {
		if (RANK == 0) {
			panic("fsck did not finish successfully!\n");
		}
		node_specific_dealloc();
		MPI_Finalize();
		return -1;
	}
	if (RANK == 0) {
		/* misnomer! */
		panic("fsck finished successfully!\n");
	}
	node_specific_dealloc();
	MPI_Finalize();
	return 0;
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
