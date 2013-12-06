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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include "capfs_config.h"
#include "meta.h"
#include <metaio.h>
#include "list.h"
#include "capfs.fsck.h"

static inline struct files* clone_filp(struct files *filp)
{
	struct files *new_filp = (struct files *)calloc(1, sizeof(struct files));
	if (new_filp) {
		memcpy(new_filp, filp, sizeof(*filp));
		/* Reset next and prev to NULL */
		new_filp->level.next = new_filp->level.prev = NULL;
	}
	return new_filp;
}

/* filp's inode fields must be initialized for this routine to work correctly! */
static void list_add_sorted(struct files *filp, struct LIST_HEAD *head)
{
	struct list_head *tmp = NULL;
	int64_t mean;

	if (filp->level.next || filp->level.prev) {
		panic("Refusing to add to sorted list! Non NULL next/prev pointers\n");
		return;
	}
	head->total += filp->inode;
	head->count++;
	mean = head->total / head->count;
	/* This routine was proving to be quite a show stopper,
	 * on reasonably large file systems.
	 * so I have reworked it to either start search from 
	 * front or from the back depending on what the value of 
	 * the inode number that is to be inserted is 
	 */
	if (mean < 0 || filp->inode < mean) {
		list_for_each (tmp, &(head->head)) {
			struct files *entry = list_entry(tmp, struct files, level);
			if (entry->inode >= filp->inode) 
				break;
		}
		__list_add(&filp->level, tmp->prev, tmp);
	}
	else {
		list_for_each_prev (tmp, &(head->head)) {
			struct files *entry = list_entry(tmp, struct files, level);
			if (entry->inode < filp->inode) 
				break;
		}
		__list_add(&filp->level, tmp, tmp->next);
	}
	return;
}

/* glibc does not seem to provide a getdents() routine, so we provide one */
_syscall3(int, getdents, uint, fd, struct dirent *, dirp, uint, count);

/*
 * All valid IOD file names should begin with f.
 * if it does not, then these files are candidates
 * for unlinking in phase 1 itself.
 * This function returns a valid +ve number in case of sucess.
 * returns -1 in case this is not a valid PVFS data file name
 */
static int64_t get_inode_number(struct files *filp)
{
	/* get the last component of the file name */
	char fname[PATH_MAX], *ptr1 = NULL, *ptr2 = NULL;
	int64_t inode_number;

	snprintf(fname, PATH_MAX, "%s", filp->name);
	ptr1 = strrchr(fname, '/');
	if (!ptr1) {
		nodeprintf("Strange! No \"/\" found in file name \"%s\"\n", filp->name); 
		return -1;
	}
	ptr1++;
	if (*ptr1 != 'f') {
		nodeprintf("Parse error in file name \"%s\". Could not find letter \"f\"!\n", filp->name);
		return -1;
	}
	ptr1++;
	if (*ptr1 == '\0') {
		nodeprintf("Strange! Invalid file name \"%s\". Name ended prematurely!\n", filp->name);
		return -1;
	}
	/* Now ptr1 should point to a number.0, We return that number */
	ptr2 = strchr(ptr1, '.');
	if (!ptr2) {
		nodeprintf("Strange! No \".\" found in file name \"%s\"\n", filp->name);
		return -1;
	}
	if (*(ptr2 + 1) != '0' || *(ptr2 + 2) != '\0') {
		nodeprintf("Strange! No \"0\" or \\0 found at the end of the file name \"%s\"\n", filp->name);
		return -1;
	}
	/* Replace the "." with a \0 */
	*ptr2 = '\0';
	if ((inode_number = strtoll(ptr1, &ptr2, 10)) < 0 
			|| inode_number == LLONG_MIN 
			|| inode_number == LLONG_MAX 
			|| *ptr2 != '\0') {
		nodeprintf("Parse error in file name \"%s\" -> Inode number yielded %lld\n", filp->name, inode_number);
		return -1;
	}
	return inode_number;
}

/* free the struct files queued on the TREE list */

static void dealloc_treelist(void)
{
	while (TREE.next != &TREE) {
		struct files *filp;

		filp = list_entry(TREE.next, struct files, level);
		list_del(TREE.next);
		free(filp);
	}
	return;
}

/*
 * NOTES: Since we want BREADTH-FIRST TRAVERSAL, we build a link list(queue).
 * If we wanted DEPTH-FIRST TRAVERSAL, then we need to build a stack here, i.e
 * we need to use list_add() function instead of list_add_tail()
 */

static int path_init(const char *path)
{
	struct files *filp = NULL;
	struct stat statbuf;

	filp = (struct files *)calloc(1, sizeof(struct files));
	if (!filp) {
		perror("do_root:calloc");
		return -1;
	}
	snprintf(filp->name, NAME_MAX + 1, "%s", path);
	/* After all the checks that we did previously, it would be crazy if stat() bombs now! */
	stat(filp->name, &statbuf);
	filp->inode = statbuf.st_ino;
	/* fsize is really a valid field only for files, not directories */
	filp->fsize = 0;
	/* add it to the tree of to-be visited nodes */
	list_add_tail(&filp->level, &TREE);
	filp->ptr = NULL;
	return 0;
}

/*
 * Notes: We don't follow symbolic links, and we ignore any special
 * files like sockets, pipes etc that may be residing on this file
 * system. Hence, the hierarchy will be a directed acyclic tree!.
 * Thus, the traversal becomes much simpler than that of a complex graph.
 */
static int path_walk(struct files *root_filp)
{
	int dir_fd, ret;
	struct files *filp;
	struct stat statbuf;
	static int invoke_count = 0;

	ret = 0;
	memset(&statbuf, 0, sizeof(statbuf));
	/* Dequeue from the list of to-be-visited nodes */
	list_del(&root_filp->level);

	dir_fd = open(root_filp->name, O_RDONLY | O_NOFOLLOW);
	if (dir_fd < 0) {
		/* lets just stat the file here, it might be some sort of stale or special file or symbolic link */
		lstat(root_filp->name, &statbuf);
	}
	else {
		fstat(dir_fd, &statbuf);
	}
	/* Are we looking at a directory? */
	if (S_ISDIR(statbuf.st_mode)) {
		int to_continue = 1;

		/* On the MGR node, we must make additional checks to see if this really is a PVFS dir. */
		if(RANK == 0) {
			char dname[PATH_MAX];
			snprintf(dname, PATH_MAX, "%s/.capfsdir", root_filp->name);
			/* oops! this is not a valid PVFS directory! */
			if (stat(dname, &statbuf) < 0 
					|| !S_ISREG(statbuf.st_mode)) {

				to_continue = 0;
				nodeprintf("%s is not a PVFS meta-data directory!\
						Deleting contents\n", root_filp->name);
				if (!SIMULATE) {
					char cmd[PATH_MAX], c = 'y';

					if (PROMPT) {
						panic("%s is not a PVFS meta-data directory!\
								Delete entire sub-tree? Press \"y\" or \"n\"\n", root_filp->name);
						scanf("%c", &c);
					}
					if (c == 'y') {
						snprintf(cmd, PATH_MAX, "rm -rf %s", root_filp->name);
						system(cmd);
					}
				}
			}
		}
		/* On the IOD nodes, all sub-directory names are numbers from 0 - IOD_DIR_HASH_SZ-1 */
		else {
			long int number;
			char *ptr = NULL;

			/* Except of course the root directory itself */
			if ( invoke_count > 0) {
				char *component = strrchr(root_filp->name, '/');

				component++;
				/* component should now point to the number */
				if ((number = strtol(component, (char **)&ptr, 10)) < 0
					|| number == LONG_MIN
					|| number == LONG_MAX 
					|| *ptr != '\0'
					|| (number > IOD_DIR_HASH_SZ)) {

					to_continue = 0;
					nodeprintf("%s(%ld) is not a PVFS data directory!\
							Deleting contents\n", root_filp->name, number);
					if (!SIMULATE) {
						char cmd[PATH_MAX], c = 'y';

						if (PROMPT) {
							panic("%s is not a PVFS data directory!\
									Delete entire sub-tree? Press \"y\" or \"n\"\n", root_filp->name);
							scanf("%c", &c);
						}
						if (c == 'y') {
							snprintf(cmd, PATH_MAX, "rm -rf %s", root_filp->name);
							system(cmd);
						}
					}
				}
			}
		}
		/* get the contents of the directory */
		if (to_continue == 1 && dir_fd >= 0) {
			off_t off = 0;
			struct dirent p;

			/* get the directory contents one at a time */
			do {
				char err[ERR_MAX];

				if ((ret = lseek(dir_fd, off, SEEK_SET)) < 0) {
					snprintf(err, ERR_MAX, "lseek(%d): %s", dir_fd, strerror(errno));
					perror(err);
					break;
				}
				if ((ret = getdents(dir_fd, &p, sizeof(p))) <= 0) {
					if (ret < 0) {
						snprintf(err, ERR_MAX, "getdents(%d): %s", dir_fd, strerror(errno));
						perror(err);
					}
					break;
				}
				/* ignore . and .. */
				else if (strcmp(p.d_name, ".") && strcmp(p.d_name, "..")) {
					/* Also ignore .capfsdir, .iodtab and lost+found on the mgr node */
					if ((RANK == 0 
							&& strcmp(p.d_name, ".capfsdir")
							&& strcmp(p.d_name, ".iodtab")
							&& strcmp(p.d_name, "lost+found")) || RANK != 0) {

							filp = (struct files *)calloc(1, sizeof(struct files));
							if (!filp) {
								perror("filp:calloc");
								ret = -1;
								break;
							}
							snprintf(filp->name, NAME_MAX + 1, "%s/%s", root_filp->name, p.d_name);
							/* Size and inode numbers of a file are filled up when the file is actually visited */
							filp->fsize = 0;
							filp->inode = 0;
							filp->ptr = NULL;
							/* Add to the tree */
							list_add_tail(&filp->level, &TREE);
					}
				}
				off = p.d_off;
			} while (1);
		} /* end if (to_continue) */
	}
	/* or are we looking at a regular file? */
	else if (S_ISREG(statbuf.st_mode)) {
		/* on the MGR node, we need to use the meta-data functions to try and read the file */
		if (RANK == 0) {
			struct fmeta meta;

			/* Make sure that it is a valid meta-data file */
			if (statbuf.st_size != sizeof(struct fmeta)) {
				nodeprintf("%s is not a valid PVFS meta-data file!\
						Deleting file\n", root_filp->name);
				if (!SIMULATE) {
					char cmd[PATH_MAX], c = 'y';

					if (PROMPT) {
						panic("%s is not a valid PVFS meta-data file!\
								Delete file? Press \"y\" or \"n\"\n", root_filp->name);
						scanf("%c", &c);
					}
					if (c == 'y') {
						snprintf(cmd, PATH_MAX, "rm -f %s", root_filp->name);
						system(cmd);
					}
				}
			}
			else {
				int i, *flags = NULL;
				flags = (int *)calloc(MGR_NIODS, sizeof(int));
				if(!flags) {
					perror("flags: calloc");
					ret = -1;
				}
				else {
					/* read the contents of the meta-data */
					meta_read(dir_fd, &meta);
					/* hold the size and inode information */
					root_filp->inode = meta.u_stat.st_ino;
					//root_filp->fsize = meta.fsize;
					root_filp->fsize = meta.u_stat.st_size;
					/* for each iod-node involved for this file, queue it in the appropriate list head */
					for (i = meta.p_stat.base; i < (meta.p_stat.base + meta.p_stat.pcount); i++) {
						if (flags[i % MGR_NIODS] == 0) {
							filp = clone_filp(root_filp);
							if (!filp) {
								perror("clone_filp:");
								ret = -1;
								break;
							}
							/* sorted on inode number of the meta-data file */
							list_add_sorted(filp, &MGR_FLIST[i % MGR_NIODS]);
							if (filp->inode > MGR_MAX_INODE[i % MGR_NIODS]) 
								MGR_MAX_INODE[i % MGR_NIODS] = filp->inode;
							if (filp->inode < MGR_MIN_INODE[i % MGR_NIODS])
								MGR_MIN_INODE[i % MGR_NIODS] = filp->inode;
							flags[i % MGR_NIODS] = 1;
						}
					}
					free(flags);
					if (ret >= 0) {
						/* Also add it to a global list as well */
						if ((filp = clone_filp(root_filp)) == NULL) {
							perror("clone_filp:");
							ret = -1;
						}
						else {
							/* hold the meta-data for this file for later use */
							filp->ptr = (struct fmeta *)calloc(1, sizeof(struct fmeta));
							if (!filp->ptr) {
								perror("filp->ptr: calloc");
								ret = -1;
								free(filp);
							}
							else {
								memcpy(filp->ptr, &meta, sizeof(struct fmeta));
								list_add_sorted(filp, &MGR_GFLIST);
							}
						}
					}
				}
				if (ret < 0) {
					int err = errno;
					dealloc_mgrflist();
					dealloc_mgrgflist();
					errno = err;
				}
			} /* end else valid meta-data file */
		} /* end if(RANK == 0) */
		else { 
			/* get the size, inode is not really the inode number 
			 * of the file on the IOD nodes. It is the number after 
			 * the letter f in the last component of the path name.
			 */
			if((root_filp->inode = get_inode_number(root_filp)) < 0) {
				nodeprintf("%s is not a valid PVFS data file!\
						Deleting file\n", root_filp->name);
				if (!SIMULATE) {
					char cmd[PATH_MAX], c = 'y';

					if (PROMPT) {
						panic("%s is not a valid PVFS data file!\
								Delete file? Press \"y\" or \"n\"\n", root_filp->name);
						scanf("%c", &c);
					}
					if (c == 'y') {
						snprintf(cmd, PATH_MAX, "rm -f %s", root_filp->name);
						system(cmd);
					}
				}
			}
			else { /* valid file on the IOD */
				root_filp->fsize = statbuf.st_size;
				if ((filp = clone_filp(root_filp)) == NULL) {
					int err = errno;
					ret = -1;
					perror("clone_filp:");
					dealloc_iodflist();
					errno = err;
				}
				else { /* queue it */
					if (filp->inode > IOD_MAX_INODE)
						IOD_MAX_INODE = filp->inode;
					if (filp->inode < IOD_MIN_INODE)
						IOD_MIN_INODE = filp->inode;
					/* List is sorted on the inode number of the meta-data file not the real inode number */
					list_add_sorted(filp, &IOD_FLIST);
				}
			}
		} /* end else if (RANK != 0) */
	} /* end (regular file) */
	else if (!S_ISLNK(statbuf.st_mode)) {
		/* All other special files */
		if (RANK == 0) {
			nodeprintf("%s is a special file on the PVFS meta-data tree!\
					Deleting file\n", root_filp->name);
		}
		else {
			nodeprintf("%s is a special file on the PVFS data tree!\
					Deleting file\n", root_filp->name);
		}
		if (!SIMULATE) {
			char cmd[PATH_MAX], c = 'y';

			if (PROMPT) {
				panic("%s is a special file!\
						Delete file? Press \"y\" or \"n\"\n", root_filp->name);
				scanf("%c", &c);
			}
			if (c == 'y') {
				snprintf(cmd, PATH_MAX, "rm -f %s", root_filp->name);
				system(cmd);
			}
		}
	}
	if (ret < 0) {
		/* walk through tree and free all the elements */
		int err = errno;
		panic("Deleting entire tree list because of error\n");
		dealloc_treelist();
		errno = err;
	}
	/* Ignore any symbolic links, device-files, any other special files */
	free(root_filp);
	if (dir_fd > 0) close(dir_fd);
	invoke_count++;
	return ret;
}


int do_flatten_hierarchy(char *path)
{
	struct files *filp = NULL;

	/* traverse the tree and prune out unnecessary files and flatten the hierarchy */
	path_init(path);
	/* walk the tree */
	while (TREE.next != &TREE) {
		filp = (struct files *)list_entry(TREE.next, struct files, level);
		/* Visit the node */
		if (path_walk(filp) < 0) {
			perror("do_flatten_hierarchy: path_walk:");
			return -1;
		}
	}
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
