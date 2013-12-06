/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/*This file contains the unlink metadata file call for the CAPFS*/
/*Assumes that request and return data has been allocated */

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
#include "metaio.h"
#include <log.h>

/* md_unlink(request, metadata_ptr) - takes a manager request,
 * determines the name of the metadata file to be accessed, checks
 * permissions for unlinking the file, and unlinks it.
 *
 * On failure the file is closed and -1 is returned.
 *
 * Open FD is returned on success.  This is done so that the mgr can
 * keep the inode number from being reused.  meta_close() is the appropriate
 * function to call on this FD.
 */
int md_unlink(mreq_p request, fmeta_p metar_p, char *fname, int *is_link)
{
	fmeta meta1;
	int fd, length;
	int daccess_ok = 0;
	char temp[MAXPATHLEN];
	struct stat statbuf;

	/* Do an lstat first */
	if(lstat(fname, &statbuf) < 0) {
		PERROR(SUBSYS_META,"md_unlink: lstat");
		return (-1);
	}
	if(S_ISLNK(statbuf.st_mode)) {
		if(is_link) *is_link = 1;
		/* Check permission on directory */
		strncpy(temp, fname, MAXPATHLEN);
		length = get_parent(temp);

		if (length >= 0) {
			/* check for permissions to write to directory */
			if((daccess_ok = meta_access(0, temp, request->uid, request->gid, W_OK)) < 0){
				return daccess_ok;
			}
		}
		/*
		 * if length<0, CWD is being used and directory permissions will 
		 * not be checked pursuant to the UNIX method 
		 *
		 * if we have access, we will also remove the link.
		 */
		return unlink(fname);
	}

	if (is_link) *is_link = 0;

   if ((fd = meta_open(fname, O_RDONLY )) < 0) {
		PERROR(SUBSYS_META,"md_unlink: meta_open");
		return (-1);
	}

	/* check execute permission on directory */
	/* strip off file name */
	strncpy(temp, fname, MAXPATHLEN);
	length = get_parent(temp);

	/* if length<0, CWD is being used and directory permissions will 
	 * not be checked pursuant to the UNIX method */
	if (length >= 0) {
		/* check for permissions to write to directory */
		daccess_ok = meta_access(0, temp, request->uid, request->gid, W_OK);
	}

	/* Read metadata file */	
	if (meta_read(fd, &meta1) < 0) {
		PERROR(SUBSYS_META,"md_unlink: meta_read");
		meta_close(fd);
		return (-1);
	}

	/* Check for root or write permission to directory */
	if ((request->uid == 0) /* root */
		 || (daccess_ok == 0) /* write access to directory */
		 )
	{
		if (meta_unlink(fname) < 0) {
			PERROR (SUBSYS_META, "md_unlink: meta_unlink");
			meta_close(fd);
			return(-1);
		}
	}
	else {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META, "md_unlink: permission check failed\n");
		errno = EPERM;
		meta_close(fd);
		return(-1);
	}	 

	*metar_p = meta1;
	return(fd);
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 *
 * vim: ts=3
 */ 
