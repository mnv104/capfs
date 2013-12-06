/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/*This file contains the chmod metadata file call for the CAPFS*/

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

int md_chmod(mreq_p request, char *fname)
{
	fmeta meta1;
	int fd, i, length, isdir=0;
	char temp[MAXPATHLEN];
	dmeta dir;

	/* Open file listed in request */
   if ((fd = meta_open(fname, O_RDWR)) < 0) {
		if (errno != EISDIR) {
			PERROR(SUBSYS_META,"md_chmod: meta_open");
			meta_close(fd);
			return (-1);
		}
		else isdir=1;
	}

	/* check execute permissions on directory */
	strncpy(temp, fname, MAXPATHLEN);
	length = get_parent(temp);
	/* length < 0 means CWD */
	if (length >= 0) {
		if (meta_access(0, temp, request->uid,
			request->gid, X_OK) < 0) {
			PERROR(SUBSYS_META,"md_chmod: meta_access");
			return(-1);
		}
	}
	
	/* Read metadata file */
	if (!isdir) {
		if ((i = meta_read(fd, &meta1)) < 0) {
			PERROR(SUBSYS_META,"md_chmod: meta_read");
			meta_close(fd);
			return (-1);
		}
	}
	else { /* directory */
		get_dmeta(fname, &dir);
		meta1.u_stat.st_uid = dir.dr_uid;
	}

	/* Change  if necessary */
	if ((request->uid != 0)&&(request->uid != meta1.u_stat.st_uid)) {
		errno = EPERM;
		PERROR(SUBSYS_META,"md_chmod");
		if (!isdir) meta_close(fd);
		return(-1);
	}

	/* had to add OR to get the high bits set right */
	meta1.u_stat.st_mode = request->req.chmod.mode | S_IFREG;

	/* Write metadata back */
	if (!isdir) {
		if ((i = meta_write(fd, &meta1)) < 0) {
			PERROR(SUBSYS_META,"md_chmod: meta_write");
			meta_close(fd);
			return(-1);
		}
	}
	else { /* directory */
		dir.dr_mode = (meta1.u_stat.st_mode & ~S_IFREG) | S_IFDIR;
		put_dmeta(fname, &dir);
	}
	
	/* Close metadata file */
	if (!isdir) meta_close(fd);

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
