/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/* This file contains the fstat metadata file call for the CAPFS	*/
/* Also assumes space for request has been allocated 					*/

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

#define NOFOLLOW_LINK 0
#define FOLLOW_LINK   1

/*
 * if name refers to an ordinary file/directory, then behaviour is
 * the same as old.
 * if name refers to a symbolic link/hard link, then depending upon 
 * the value of follow_link, we do the following,
 * a) if(follow_link) we try to return the results of the stat system call
 * on the target of the symbolic link
 * b) if(!follow_link) we try to return the resuls of the stat system call
 * on the symbolic link itself
 */
int md_stat(char *name, mreq_p request, fmeta_p metar_p, int follow_link)
{
	fmeta meta1;
	int fd, length, myerr;
	dmeta dir;
	char temp[MAXPATHLEN];
	struct stat statbuf;
	/* zero the metadata */
	memset(metar_p, 0, sizeof(fmeta));
	memset(&statbuf, 0, sizeof(struct stat));

	/* First we try to lstat the file */
	if(lstat(name, &statbuf) < 0) {
		myerr = errno;
		//LOG(stderr, WARNING_MSG, SUBSYS_META, " md_stat: lstat on %s failed\n", name);
		errno = myerr;
		return (-1);
	}
	/* In case of a link that we are not supposed to follow, we return early */
	if(S_ISLNK(statbuf.st_mode) && follow_link == NOFOLLOW_LINK) {
		/* check execute permission on parent directory */
		strncpy(temp, name, MAXPATHLEN);
		length = get_parent(temp); /* strip off filename */
		/* 
		 * if length<0, CWD is being used and directory permissions will
	 	 * not be checked pursuant to the UNIX method 
		 */
		if (length >= 0) {
			/* check for permissions to read from directory */
			if (meta_access(0, temp, request->uid, request->gid, X_OK | R_OK) < 0) {
				PERROR(SUBSYS_META,"md_stat: meta_access");
				return(-1);
			}
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "Size of link file %s is %Ld\n", name, statbuf.st_size);
	 	COPY_STAT_TO_PSTAT(&metar_p->u_stat, &statbuf);
		metar_p->u_stat.st_mode = S_IFLNK | (S_IRWXO | S_IRWXG | S_IRWXU);
		return(0);

	} else if(S_ISDIR(statbuf.st_mode)){

		/* directory - just return the normal stats */ 
		/* check execute permission on parent dir */
		strncpy(temp, name, MAXPATHLEN);
		length = get_parent(temp); /* strip off filename */
		if (length >= 0) {
			if (meta_access(0, temp, request->uid, request->gid, X_OK) < 0)
				{
					return(-1);
				}
		}
		/* else CWD is being used and directory permissions are not
		 * checked */

		if (get_dmeta(name, &dir) < 0) {
			PERROR(SUBSYS_META,"md_stat: get_dmeta");
			return(-1);
		}

		COPY_STAT_TO_PSTAT(&metar_p->u_stat, &statbuf);

		metar_p->u_stat.st_mode =
			S_IFDIR | ((S_IRWXO | S_IRWXG | S_IRWXU | S_ISVTX | S_ISGID) & dir.dr_mode);

		metar_p->fs_ino        = dir.fs_ino;
		metar_p->u_stat.st_uid = dir.dr_uid;
		metar_p->u_stat.st_gid = dir.dr_gid;
		return(0);
	}
	else if (S_ISLNK(statbuf.st_mode) && follow_link == FOLLOW_LINK) {
		
		/* now we try to stat the file */
		if(stat(name, &statbuf) < 0) {
			myerr = errno;
			LOG(stderr, WARNING_MSG, SUBSYS_META, " md_stat: stat on %s failed\n", name);
			errno = myerr;
			return (-1);
		}
		/* if the link points to a directory, return the stat info for that early */
		if(S_ISDIR(statbuf.st_mode)) {
			/* target of the link is a directory - just return the normal stats */ 
			strncpy(temp, name, MAXPATHLEN);
			/* check execute permission on parent dir */
			length = get_parent(temp); /* strip off filename */
			if (length >= 0) {
				if (meta_access(0, temp, request->uid, request->gid, X_OK) < 0)
					{
						return(-1);
					}
			}
			/* else CWD is being used and directory permissions are not
			 * checked */

			if (get_dmeta(name, &dir) < 0) {
				PERROR(SUBSYS_META,"md_stat: get_dmeta");
				return(-1);
			}

			COPY_STAT_TO_PSTAT(&metar_p->u_stat, &statbuf);

			metar_p->u_stat.st_mode =
				S_IFDIR | ((S_IRWXO | S_IRWXG | S_IRWXU | S_ISVTX | S_ISGID) & dir.dr_mode);

			metar_p->fs_ino        = dir.fs_ino;
			metar_p->u_stat.st_uid = dir.dr_uid;
			metar_p->u_stat.st_gid = dir.dr_gid;
			return(0);
		}
	}

	/* else regular file or followed link */
		
	/* Open metadata file */	
	if ((fd = meta_open(name, O_RDONLY)) < 0) {
		if (errno != ENOENT) PERROR(SUBSYS_META,"md_stat, meta_open");
		return (-1);
	}

	/* check execute permission on parent directory */
	strncpy(temp, name, MAXPATHLEN);
	length = get_parent(temp); /* strip off filename */
	/* if length<0, CWD is being used and directory permissions will
	 * not be checked pursuant to the UNIX method */
	if (length >= 0) {
		/* check for permissions to read from directory */
		/* FIXME: Check for R_OK also? */
		if (meta_access(0, temp, request->uid, request->gid, X_OK | R_OK) < 0) {
			PERROR(SUBSYS_META,"md_stat: meta_access");
			meta_close(fd); /* try to close */
			return(-1);
		}
	}

	/* Read metadata file */
	if (meta_read(fd, &meta1) < 0) {
		PERROR(SUBSYS_META,"md_stat: meta_read");
		meta_close(fd); /* try to close */
		return(-1);
	}

	/* Close metadata file */
	if (meta_close(fd) < 0) {
		PERROR(SUBSYS_META,"md_stat: meta_close");
		return(-1);
	}	

	*metar_p = meta1;
	metar_p->u_stat.st_mode =
	S_IFREG|((S_IRWXO|S_IRWXG|S_IRWXU) & metar_p->u_stat.st_mode);
	return (0);
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
