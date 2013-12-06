/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/*This file contains the chown metadata file call for the CAPFS*/
/*Assumes that space for request has been allocated. */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/param.h>
#include <linux/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <meta.h>
#include <req.h>
#include <log.h>

/* MD_MKDIR() - given a pathname to a directory and a dmeta structure
 * describing the directory, creates the metadata files needed in the
 * directory (currently only the .capfsdir file).
 *
 * This function used to modify some of the structures in the dmeta_p
 * structure, but we don't do that any more.
 *
 * This function does not assume that the dirpath has a trailing /.
 *
 * put_dmeta() does ALMOST the same thing as this function, but doesn't
 * understand that the fname field should be used as part of the name
 * when we're making a new .capfsdir file.  So this should always be used
 * when we're making a new file...
 *
 * TODO: NOW THAT WE AREN'T DOING SD_PATH AND FNAME ANY MORE, ANY DIFF?
 *
 * Returns 0 on success, -1 on error.
 */
int md_mkdir(char *dirpath, dmeta_p dir)
{
	FILE *fp;
	char temp[MAXPATHLEN];

	/* create dotfile */
	strncpy(temp, dirpath, MAXPATHLEN);
	strcat(temp,"/.capfsdir");
	umask(0033);
	if ((fp = fopen(temp, "w+")) == 0) {
		PERROR(SUBSYS_META,"Error creating dot file");
		return(-1);
	}

	/* write dotfile */
	fprintf(fp, "%Ld\n%d\n%d\n", dir->fs_ino, dir->dr_uid, dir->dr_gid);
	fprintf(fp, "%07o\n%d\n%s\n", dir->dr_mode, dir->port, dir->host);
	fprintf(fp, "%s\n", dir->rd_path);
	fclose(fp);

	/* return success */
	return(0);

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
