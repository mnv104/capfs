/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/*
 * Implements the symbolic and hard link
 * manipulation functions. 
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
#include <stdlib.h>
#include "metaio.h"
#include <log.h>



/* PROTOTYPES */
static inline int md_check_link(char *link_name, struct stat *statbuf)
{
   int ret;
   ret = stat(link_name, statbuf);
   if(ret == 0) {
		/* link_name exists already */
		return 1;
   }
   /* Does not exist */
   return 0;
}

/* Creates a hard link file */
int md_link(char *link_name, char *target_name, mreq_p request)
{
	int exist, length;
	char temp[MAXPATHLEN];
	struct stat statbuf;
	/* Check for existence of the link file */
	exist = md_check_link(link_name, &statbuf);
	if(exist == 1) {
		errno = EEXIST;
		LOG(stderr, WARNING_MSG, SUBSYS_META, " Hard link file %s exists", link_name);
		return -1;
	}
	/* Check for existence of the target file as well */
	exist = md_check_link(target_name, &statbuf);
	/* if the target file does not exist or if it is a directory, then we disallow */
	if(!exist || S_ISDIR(statbuf.st_mode)) {
		errno = EPERM;
		LOG(stderr, WARNING_MSG, SUBSYS_META, " Hard link target file %s does not exist or is a directory\n", 
		    target_name);
		return -1;
	}
   /* check execute permissions on directory */
   strncpy(temp, link_name, MAXPATHLEN);
	length = get_parent(temp);
	/* 
	 * if length<0, CWD is being used and directory permissions will 
	 *	not be checked pursuant to the UNIX method 
	 */
	if(length >= 0) {
   	/* check write and execute permissions on parent directory */
 		if (meta_access(0, temp, request->uid, request->gid, X_OK | W_OK) < 0) {
			PERROR(SUBSYS_META,"md_link: meta_access (dir)");
	 		return(-1);
 		}
	}
	return link(target_name, link_name);
}

/* Creates a symbolic link file */
int md_symlink(char *link_name, char *target_name, mreq_p request)
{
	int exist, length;
	char temp[MAXPATHLEN];
	struct stat statbuf;
	/* Check for existence of the link file */
	exist = md_check_link(link_name, &statbuf);
	if(exist == 1) {
		errno = EEXIST;
		LOG(stderr, WARNING_MSG, SUBSYS_META, " Soft link file %s exists", 
		    link_name);
		return -1;
	}
   strncpy(temp, link_name, MAXPATHLEN);
	length = get_parent(temp);
	/* 
	 * if length<0, CWD is being used and directory permissions will 
	 *	not be checked pursuant to the UNIX method 
	 */
	if(length >= 0) {
   	/* check write and execute permissions on parent directory */
 		if (meta_access(0, temp, request->uid, request->gid, X_OK | W_OK) < 0) {
			PERROR(SUBSYS_META,"md_symlink: meta_access (dir)");
	 		return(-1);
 		}
	}
	return symlink(target_name, link_name);
}

int md_islink(char *link_name, struct stat *statbuf)
{
	int ret;
	ret = lstat(link_name, statbuf);
	if(ret < 0) {
		return -1;
	}
	return S_ISLNK(statbuf->st_mode);
}

/* caller must free the returned pointer */
char* md_readlink(char *link_name)
{
	int ret;
	char *target_name = NULL;
	struct stat statbuf;
	ret = md_islink(link_name, &statbuf);
	/* if it is a link */
	if(ret == 1) {
		target_name = (char *)calloc(sizeof(char), 1 + statbuf.st_size);
		if(target_name) {
			ret = readlink(link_name, target_name, 1 + statbuf.st_size);
			if(ret < 0) {
				PERROR(SUBSYS_META,"md_readlink: readlink failed");
				free(target_name);
				target_name = NULL;
			}
		}
	}
	else {
		PERROR(SUBSYS_META,"file does not exist or is not a symbolic link");
	}
	return target_name;
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
