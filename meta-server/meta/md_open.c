/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/* This file contains the open metadata file call for the CAPFS	* 
 * Assumes request and metar are previously allocated. 		  	* 
 * rootbuf is a static char string.  The root directory of 		* 
 * the file system is determined from the .capfsdir file and 	* 
 * returned to the manager.												* 
 *	If file does not exist, it will be created, but only if  	* 
 * the capfs dot file is present for the directory.             * 
 */

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <linux/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <meta.h>
#include <req.h>
#include <sys/param.h>
#include <time.h>
#include "metaio.h"
#include <log.h>

/* DEFINES */
#define RPSTAT request->req.open.meta.p_stat

/* PROTOTYPES */

/* MD_OPEN() - opens (and may create) metadata files for the manager.
 * This code really, really sucks.  I'm ashamed of it.  There is
 * little/no permission checking going on, and there are pointer
 * oddities as well WRT metar_p.  I don't like it.  Some day it should
 * be rewritten...
 *
 * NOTES ON FILE CREATION:
 * This function only sets the user, group, and permissions in the
 * metadata when the file is created, so the date, for example, will be
 * wrong unless more is done.
 */
int md_open(char *name, mreq_p request, fmeta_p metar_p)
{
	int uid, gid, fd, length, mode, sgid = -1;
	fmeta meta1;
	mode_t orig_mode;
	char temp[MAXPATHLEN];

	/* open meta data file (this will not create it!) */
	if ((fd = meta_open(name, O_RDONLY)) < 0) {
		/* ok, something happened...were we trying to create a new file? */
		if (!(request->req.open.flag & O_CREAT)) {
			/* file does not exist and we aren't trying to create file! */
			LOG(stderr, CRITICAL_MSG, SUBSYS_META, "md_open: meta_open (%s): %s\n", name, strerror(errno));
			return(-1);
		}

		/* strip off file name so we can check write permissions */
		strncpy(temp, name, MAXPATHLEN);
		for (length=strlen(temp);length >= 0 && temp[length] != '/';length--);
		/* if length<0, CWD is being used and directory permissions will 
		 *	not be checked pursuant to the UNIX method */
		if (length >= 0) {
			dmeta dir;
			temp[length] = '\0'; /* cut off file name */
			/* check for permissions to write to directory */
			if (meta_access(0, temp, request->uid, request->gid, X_OK | W_OK)
				< 0) {
				PERROR(SUBSYS_META,"md_open: meta_access (dir)");
				return(-1);
			}
			/* Now check if the directory had the setgid bit set or not */
			if (get_dmeta(temp, &dir) < 0) {
				PERROR(SUBSYS_META,"md_open: get_dmeta(dir)");
				return -1;
			}
			if(dir.dr_mode & S_ISGID) {
				sgid = dir.dr_gid;
			}
		}

		/* Yep...need to try to create the file...check params first */
		if ((RPSTAT.base < 0) || (RPSTAT.pcount < 0)
		|| (RPSTAT.ssize < 0) || (RPSTAT.pcount > 65535))
		{
			LOG(stderr, WARNING_MSG, SUBSYS_META, "Invalid pstat values");
			errno = EINVAL;
			return(-1);
		}

		/* now create meta file */
		if ((fd = meta_creat(name, O_RDWR)) < 0) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_META, "md_open: meta_creat (%s): %s\n", name, strerror(errno));
			return(-1);
		}

		/* fill in the metadata and exit...again, should be permission
		 * checking going on...
		 */
		uid = request->req.open.meta.u_stat.st_uid;
		if (sgid == -1) {
			gid = request->req.open.meta.u_stat.st_gid;
		}
		else {
			gid = sgid;
		}
		orig_mode = request->req.open.meta.u_stat.st_mode;
		
		{
			struct stat sb;
			if (fstat(fd, &sb) < 0) {
				PERROR(SUBSYS_META,"Error doing fstat");
				return (-1);
			}	
			COPY_STAT_TO_PSTAT(&request->req.open.meta.u_stat, &sb);
		}
		request->req.open.meta.u_stat.st_uid = uid;
		request->req.open.meta.u_stat.st_gid = gid;
		request->req.open.meta.u_stat.st_mode =
			S_IFREG | ((S_IRWXO | S_IRWXG | S_IRWXU) & orig_mode);

		if (meta_write(fd, &(request->req.open.meta)) < 0)
		{ 
			PERROR(SUBSYS_META,"Error writing created file.");
			return (-1);
		}	
		*metar_p = request->req.open.meta;
		meta_close(fd);
		return(0);
	}
	else /* the file was there */ {
		if ((request->req.open.flag & O_EXCL)
		&& (request->req.open.flag & O_CREAT)) {
			/* file exists and we wanted O_EXCL | O_CREAT */
			meta_close(fd);
			errno = EEXIST;
			return(-1);
		}
	}

	/* check for read/write permissions here */
	mode = R_OK | W_OK | X_OK; /* default mode (SHOULD never happen!) */
	switch (request->req.open.flag & ((mode_t) O_ACCMODE)) {
		case O_RDONLY:
			mode = R_OK;
			break;
		case O_WRONLY:
			mode = W_OK;
			break;
		case O_RDWR:
			mode = R_OK | W_OK;
			break;
	}
	if (meta_access(fd, name, request->uid, request->gid, mode) < 0) {
		PERROR(SUBSYS_META,"md_open: meta_access (file)");
		meta_close(fd);
		return(-1);
	}

	if (meta_read(fd, &meta1) < 0)  {
 		PERROR(SUBSYS_META,"md_open: meta_read");
		meta_close(fd);
		return (-1);
	}

	meta_close(fd);
	*metar_p = meta1;
	return (0);
}

#undef RPSTAT

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
