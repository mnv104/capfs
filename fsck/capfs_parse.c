/*
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
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>
#include "mpi.h"
#include "iodtab.h"
#include "capfs.fsck.h"

extern struct iodtabinfo *my_parse_iodtab(char *fname);

static inline void usage(char *str)
{
	panic("Usage: %s\
-m <mgr meta-data directory>\
-f {iod configuration file}\
-p {dont prompt for deletion}\
-s {just simulate actions that would be taken}\
-h {{this help message}}\n", str);
	return;
}

int parse_args(int argc, char *argv[])
{
	int c;

	/* by default, we need to prompt for action especially deletion of files */
	PROMPT = 1;
	SIMULATE = 0;
	while ((c = getopt(argc, argv, "m:f:psh")) != EOF) {
		switch(c) {
			case 'm':
				META_DIR = optarg;
				break;
			case 'f':
				IOD_CONF_FILE = optarg;
				break;
			case 'p':
				PROMPT = 0;
				break;
			case 's':
				SIMULATE = 1;
				break;
			case 'h':
				/* fall through */
			default:
				usage(argv[0]);
				return -1;
		}
	}
	if (!META_DIR) {
		panic("Invalid meta-data directory!\n");
		usage(argv[0]);
		return -1;
	}
	/* Specify atleast one of the two iod related options */
	if (!IOD_CONF_FILE) {
		panic("Please specify iod config file\n");
		usage(argv[0]);
		return -1;
	}
	return 0;
}

/* Should be called once the .iodtab of the PVFS file system is parsed already */
static int alloc_mgr_args(void)
{
	if (RANK != 0) {
		return 0;
	}
	MGR_NIODS 		= MGR_IODINFO_P->nodecount;
	MGR_FLIST 		= (struct LIST_HEAD *)calloc(MGR_NIODS, sizeof(struct LIST_HEAD));
	MGR_MIN_INODE 	= (int64_t *)calloc(MGR_NIODS, sizeof(int64_t));
	MGR_MAX_INODE 	= (int64_t *)calloc(MGR_NIODS, sizeof(int64_t));
	MGR_IPARGS		= (struct iod_phase_args *)calloc(MGR_NIODS, sizeof(struct iod_phase_args));
	MGR_MPARGS		= (struct mgr_phase_args *)calloc(MGR_NIODS, sizeof(struct mgr_phase_args));
	/* panic on error in allocation */
	if (!MGR_FLIST 
			|| !MGR_MIN_INODE
			|| !MGR_MAX_INODE 
			|| !MGR_IPARGS
			|| !MGR_MPARGS) {
		if (MGR_FLIST) 		free(MGR_FLIST);
		if (MGR_MIN_INODE) 	free(MGR_MIN_INODE);
		if (MGR_MAX_INODE) 	free(MGR_MAX_INODE);
		if (MGR_IPARGS)    	free(MGR_IPARGS);
		if (MGR_MPARGS)    	free(MGR_MPARGS);
		return -1;
	}
	return 0;
}

/*
 * Parse the iod_configuration file and
 * return the rank'{th|st|nd} line's second field
 */
static char *get_iod_dir(int iod_conf_fd)
{
	FILE *fp = fdopen(iod_conf_fd, "r");
	int count = 0, ret = 0;
	char *line = NULL;
	size_t n = 0;
	static char hostname[HOST_MAX], iod_data_dir[PATH_MAX];

	if (!fp) {
		perror("fdopen:");
		return NULL;
	}
	while (count < RANK) {
		if (line) free(line);
		line = NULL;
		n = 0;
		if (getline(&line, &n, fp) < 0) {
			return NULL;
		}
		/* Update count only if it is a valid line */
		if (line[0] != '#') {
			if ((ret = sscanf(line, "%s %s\n", hostname, iod_data_dir)) == 2) {
				count++;
			}
			else if (ret > 0) {
				panic("%s Invalid iod-config file format!\n", get_host_name());
				free(line);
				return NULL;
			}
		}
	}
	ret = sscanf(line,"%s %s\n", hostname, iod_data_dir);
	if (ret != 2) {
		panic("%s Invalid iod-config file format!\n", get_host_name());
		free(line);
		return NULL;
	}
	free(line);
	return iod_data_dir;
}


/*
 * Checks the validity of the paths provided by user.
 * Once that is verified, this function returns the 
 * parsed path name. On any errors, we return NULL.
 */
char* check_validity(void)
{
 	int to_continue = 0, result;
	struct stat statbuf;
	char str[ERR_MAX];
	char *path = NULL;

	/* On the MGR node */
	if (RANK == 0) {
		char pathname[PATH_MAX];

		do {
			/* check if META_DIR a valid directory? */
			if ((to_continue = stat(META_DIR, &statbuf)) < 0 
					|| !S_ISDIR(statbuf.st_mode)) {
				to_continue = -1;
				snprintf(str, ERR_MAX, "%s -> stat: %s", get_host_name(), path);
				perror(str);
				break;
			}
			/* Make sure that path is readable, writeable and executable */
			if ((to_continue = 
						access(META_DIR, R_OK | W_OK | X_OK | F_OK)) < 0) {
				snprintf(str, ERR_MAX, "%s -> access: %s\n", get_host_name(), META_DIR);
				perror(str);
				break;
			}
			/* Make sure that there is a valid .iodtab and .capfsdir file */
			snprintf(pathname, PATH_MAX, "%s/.iodtab", META_DIR);
			if ((to_continue = stat(pathname, &statbuf)) < 0 
					|| !S_ISREG(statbuf.st_mode)) {
				to_continue = -1;
				snprintf(str, ERR_MAX, "%s -> stat: %s/.iodtab\n", get_host_name(), META_DIR);
				perror(str);
				break;
			}
			else {
				/* Now that we have a valid .iodtab file, let us parse it and keep the information */
				if ((MGR_IODINFO_P = my_parse_iodtab(pathname)) == NULL) {
					to_continue = -1;
					snprintf(str, ERR_MAX, "%s -> parse error %s/.iodtab\n", get_host_name(), META_DIR);
					perror(str);
					break;
				}
				else {
					int i;
					/*
					 * Parse was successful. let us check if MPI_COMM_WORLD size - 1 equals
					 * number of iod's configured for this filesystem. One of the members
					 * runs on the mgr node and hence does not count towards the total.
					 */

					if (MGR_IODINFO_P->nodecount != (NPROCS - 1)) {
						to_continue = -1;
						errno = EINVAL;
						snprintf(str, ERR_MAX, "%s -> Mismatch file system iod count (%d), MPI_COMM_WORLD size - 1 (%d)\n", get_host_name(), MGR_IODINFO_P->nodecount, (NPROCS - 1));
						perror(str);
						break;
					}
					/* 
					 * The above check is really a little weak, since we should also
					 * check if the host names on which the fsck is currently running on
					 * should match the IOD host names mentioned in the config file.
					 * Ideally, this program should be launched from the shell script
					 * which anyway does the right thing, but suppose this program was
					 * launched by mistake(or even intentionally) with the wrong set of
					 * arguments, we dont want to mess up an existing file system.
					 * Hmm.. Need to think along those lines.
					 */
					if ((to_continue = alloc_mgr_args()) < 0) {
						snprintf(str, ERR_MAX, "%s -> Could not calloc\n", get_host_name());
						perror(str);
						break;
					}
					for (i=0; i < MGR_NIODS; i++) {
						INIT_LIST_HEAD(&MGR_FLIST[i].head);
						MGR_MIN_INODE[i] = LLONG_MAX;
						MGR_MAX_INODE[i] = LLONG_MIN;
					}
				}
				snprintf(pathname, PATH_MAX, "%s/.capfsdir", META_DIR);
				if ((to_continue = stat(pathname, &statbuf)) < 0
						|| !S_ISREG(statbuf.st_mode)) {
					to_continue = -1;
					snprintf(str, ERR_MAX, "%s -> stat: %s/.capfsdir\n", get_host_name(), META_DIR);
					perror(str);
				}
				/* Now create a directory called lost+found if one does not exist already */
				snprintf(pathname, PATH_MAX, "%s/lost+found", META_DIR);
				if ((to_continue = create_capfs_dir(pathname)) < 0) {
					if (errno == EEXIST) {
						to_continue = 0;
					}
					else {
						snprintf(str, ERR_MAX, "%s -> create_capfs_dir %s\n", get_host_name(), pathname);
						perror(str);
					}
				}
			}
		} while (0);
		path = META_DIR;
	}
	else { /* all tasks with rank != 0 */
		/* parse the IOD config file and verify the IOD data directory */
		int iod_conf_fd = 0;
		char *iod_dir = NULL;

		do {
			/* Check for the presence of the iod config file? */
			if ((to_continue = stat(IOD_CONF_FILE, &statbuf)) < 0) {
				snprintf(str, ERR_MAX, "%s -> stat: %s\n", get_host_name(), IOD_CONF_FILE);
				perror(str);
				break;
			}
			iod_conf_fd = open(IOD_CONF_FILE, O_RDONLY);
			/* permission error possibly? */
			if (iod_conf_fd < 0) {
				to_continue = -1;
				snprintf(str, ERR_MAX, "%s -> open: %s\n", get_host_name(), IOD_CONF_FILE);
				perror(str);
				break;
			}
			/*
			 * Doing host name comparison in the iod_conf_file might be a
			 * little messy or painful. Hence what we do here is count
			 * the number of lines (that dont begin with a "#" or are not
			 * empty lines) and stop when it equals our rank. Then
			 * we get the appropriate iod_data directory off that line.
			 */
			iod_dir = get_iod_dir(iod_conf_fd);
			if (!iod_dir) {
				to_continue = -1;
				/* oops! some sort of parse error I think?! */
				close(iod_conf_fd);
				break;
			}
			close(iod_conf_fd);
			/* Make sure that iod_dir is a valid directory! */
			if ((to_continue = stat(iod_dir, &statbuf)) < 0 
					|| !S_ISDIR(statbuf.st_mode)) {
				to_continue = -1;
				snprintf(str, ERR_MAX, "%s -> stat: %s", get_host_name(), iod_dir);
				perror(str);
				break;
			}
			/* Make sure that the iod data dir is present and readable,writeable etc*/
			if ((to_continue = access(iod_dir, R_OK | W_OK | X_OK | F_OK)) < 0) {
				snprintf(str, ERR_MAX, "%s -> access: %s", get_host_name(), iod_dir);
				perror(str);
				break;
			}
		} while (0);
		path = iod_dir;
	}
	/* Should we continue or not? */
	MPI_Allreduce(&to_continue, &result, 1, MPI_INTEGER, MPI_SUM, MPI_COMM_WORLD);
	if (result < 0) {
		return NULL;
	}
	/* Wow! At this point, we have a valid(hopefully?) mgr/iod base directory readable, writeable! */
	return path;
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
