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
#include <fcntl.h>
#include <linux/types.h>
#include <unistd.h>
#include <meta.h>
#include <req.h>
#include <dirent.h>
#include "metaio.h"
#include <log.h>

int md_rmdir(mreq_p req_p, char *fname)
{
	DIR *dp;
	struct dirent *dep;
	char temp[MAXPATHLEN];
	int length;

	/* check for permissions */
   strncpy(temp, fname, MAXPATHLEN);
	length = get_parent(temp);
   /* if length<0, CWD is being used and directory permissions will
    * not be checked pursuant to the UNIX method */
   if (length >= 0) {
      /* check for permissions to write to directory */
      if (meta_access(0, temp, req_p->uid, req_p->gid, X_OK | W_OK)
         < 0) {
         PERROR(SUBSYS_META,"md_rmdir: meta_access");
         return(-1);
      }
   }

	if (!(dp = opendir(fname))) return(-1);

	/* read the directory and make sure that our dot files are all that
	 * is left
	 */
	while((dep = readdir(dp))) {
		if (strcmp(dep->d_name,".capfsdir") &&
		    strcmp(dep->d_name,".") &&
		    strcmp(dep->d_name,"..")) {
			errno = ENOTEMPTY;
			return(-1);
		}
	}
	closedir(dp);
	/* remove the dotfile */
	strncpy(temp, fname, MAXPATHLEN);
	strcat(temp,"/.capfsdir"); /* should check bounds... */
	if (unlink(temp)) {
		return(-1);
	}
	/* remove the directory */
	if (rmdir(fname)) {
		return(-1);
	}
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
