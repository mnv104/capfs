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
#include <grp.h>
#include <pwd.h>
#include "metaio.h"
#include <log.h>

int md_chown(mreq_p request, char *fname)
{
	fmeta meta1;
	int fd,length,i, setgid = -1, force_group_change = 0;
	char temp[MAXPATHLEN];
	dmeta dir;
	struct passwd *pw;
	struct group *grp;


	if ((fd = meta_open(fname, O_RDWR)) < 0) {
		if (errno != EISDIR) {
			PERROR(SUBSYS_META,"md_chown: meta_open");
			return (-1);
		}
	}

   /* check execute permissions on directory */
   strncpy(temp, fname, MAXPATHLEN);
	length = get_parent(temp);
	/* length < 0 means CWD */
   if (length >= 0) {
		dmeta pdir;
		if (meta_access(0, temp, request->uid, request->gid, X_OK) < 0) {
			PERROR(SUBSYS_META,"md_chown: meta_access");
			return(-1);
		}
		get_dmeta(temp, &pdir);
		if(S_ISGID & pdir.dr_mode) {
			setgid = pdir.dr_gid;
		}
	}
	
	/* Read metadata file */	
	if (fd >= 0) {
		if (meta_read(fd, &meta1) < 0) {
			PERROR(SUBSYS_META,"md_chown: meta_read");
			meta_close(fd);
			return (-1);
		}
		lseek(fd, 0, SEEK_SET);
	}
	else { /* directory */
		get_dmeta(fname, &dir);
		meta1.u_stat.st_uid = dir.dr_uid;
		meta1.u_stat.st_gid = dir.dr_gid;
	}

	/* Change owner if necessary */
	if (request->req.chown.owner >= 0) {
		/* root can change this or if uid = file owner uid,
		 * let group chown be performed */
		if (((request->uid != 0) && (request->uid != request->req.chown.owner))
			 || ((request->uid != 0) && (request->uid != meta1.u_stat.st_uid))) 
      {
			errno = EPERM;
			PERROR(SUBSYS_META,"md_chown: permission denied");
			if (fd >= 0)
				meta_close(fd);
			return(-1);
		}
		meta1.u_stat.st_uid = request->req.chown.owner;
	}
	
	/* Change group if necessary */
	if (request->req.chown.group >= 0) {
		if (request->uid) /* not root, check perms */ {
			/* from chown(2), only root or owner can change group
				permissions, so check for owner of file */
			if(request->uid != meta1.u_stat.st_uid){
				LOG(stderr, WARNING_MSG, SUBSYS_META, "md_chown: change group permission denied (1)");
				if (fd >= 0) meta_close(fd);
				errno = EPERM;
				return(-1);
			}

			/* grab group info from /etc/group or wherever */
			if (!(grp = getgrgid(request->req.chown.group))) {
				PERROR(SUBSYS_META,"md_chown: getgrgid");
				if (fd >= 0) meta_close(fd);
				errno = EINVAL;
				return(-1);
			}
			/* see if user is a member of target group in /etc/group or
				/etc/passwd */
			if (!(pw = getpwuid(request->uid))) {
				PERROR(SUBSYS_META,"md_chown: getpwuid");
				if (fd >= 0) meta_close(fd);
				errno = EINVAL;
				return(-1);
			}
			for(i=0;grp->gr_mem[i] && strcmp(pw->pw_name, grp->gr_mem[i]); i++);
			if(!grp->gr_mem[i] && pw->pw_gid != request->req.chown.group) {
				LOG(stderr, WARNING_MSG, SUBSYS_META, "md_chown: change group permission denied (2)");
				if (fd >= 0) meta_close(fd);
				errno = EPERM;
				return(-1);
			}
		}
		if (setgid == -1) {
			meta1.u_stat.st_gid = request->req.chown.group;
		}
		else {
			force_group_change = request->req.chown.force_group_change;
			if(force_group_change == 1) {
				meta1.u_stat.st_gid = request->req.chown.group;
			}
			else {
				meta1.u_stat.st_gid = setgid;
			}
		}
	}	

	/* Write metadata back */
	if (fd >= 0) {
		if (meta_write(fd, &meta1) < 0) {
			PERROR(SUBSYS_META,"md_chown: meta_write");
			meta_close(fd);
			return(-1);
		}
	}
	else { /* directory */
		dir.dr_uid = meta1.u_stat.st_uid;
		dir.dr_gid = meta1.u_stat.st_gid;
		put_dmeta(fname, &dir);
	}

	/* Close metadata file */
	if (fd >= 0 && (meta_close(fd)) < 0) {
		PERROR(SUBSYS_META,"md_chown: meta_close");
		return(-1);
	}	

	/* Do acknowledge and return */
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
