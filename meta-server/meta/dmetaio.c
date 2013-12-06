/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/* DMETAIO - function calls used to access CAPFS directory metadata files
 *
 */

/* $Header: /home/vilayann/CVSHOME/CAPFS/meta-server/meta/dmetaio.c,v 1.1.1.1 2005/03/09 09:12:26 vilayann Exp $ */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* CAPFS INCLUDES */
#include <meta.h>

/* DMETA_OPEN() - opens a CAPFS directory metadata file and checks to make
 * sure that the file is indeed a CAPFS directory metadata file
 *
 * Takes a pathname to a CAPFS directory as an argument, not the name of
 * the metadata file
 *
 * Note: THIS IS NOT FOR CREATING NEW METADATA FILES!  Use dmeta_creat()
 * for that.  This will just give you an error.
 *
 * Returns a file descriptor (>=0) on success, -1 on failure.
 */
int dmeta_open(char *pathname, int flags)
{
	int fd, len;
	static char mpath[MAXPATHLEN];

	flags &= ~(O_CREAT | O_TRUNC);

	/* put together the name of the metadata file */
	strncpy(mpath, pathname, MAXPATHLEN);
	len = strlen(mpath);
	strncat(&mpath[len], "/.capfsdir", MAXPATHLEN-len);

	if ((fd = open(mpath, flags, 0)) < 0) return(-1);

	/* do any other checking we might like to do */

	return(fd);
} /* end of dmeta_open() */


/* DMETA_CREAT() - creates a new CAPFS directory metadata file
 */
int dmeta_creat(char *pathname, int flags)
{
	int fd, len, old_umask, cflags = O_RDWR | O_CREAT | O_EXCL,
		mode = S_IRWXU | S_IRGRP | S_IROTH;
	static char mpath[MAXPATHLEN];

	/* create the file */
	strncpy(mpath, pathname, MAXPATHLEN);
	len = strlen(mpath);
	strncat(&mpath[len], "/.capfsdir", MAXPATHLEN-len);

	old_umask = umask(0);
	fd = open(mpath, cflags, mode);
	umask(old_umask);
	if(fd < 0){
		return(-1);
	}

	close(fd);

	/* reopen the new file using the specified flags */
	return(dmeta_open(mpath, flags));
} /* end of dmeta_creat() */


/* DMETA_READ() - read the directory metadata from a CAPFS directory
 * metadata file
 *
 * The struct dmeta is stored starting at the position pointed to by
 * buf.  This function also stores the ascii strings associated with the
 * directory metadata in the buffer, placing them after the struct dmeta
 * and setting the pointers in the structure appropriately.
 *
 * Returns the size of used region if successful, or -1 on
 * error.
 */
int dmeta_read(int fd, void *buf, size_t len)
{
	struct dmeta *dm_p = (struct dmeta *)buf;
	int tlen, ret;
	char tmpbuf[4096], *b_p;

	lseek(fd, 0, SEEK_SET);
	if (read(fd, tmpbuf, 4096) < 0) return(-1);

	/* read the set values out of the file */
	dm_p->fs_ino  = strtol(strtok(tmpbuf, "\n"), NULL, 10);
	dm_p->dr_uid  = strtol(strtok(NULL, "\n"), NULL, 10);
	dm_p->dr_gid  = strtol(strtok(NULL, "\n"), NULL, 10);
	dm_p->dr_mode = strtol(strtok(NULL, "\n"), NULL, 8);
	dm_p->port    = strtol(strtok(NULL, "\n"), NULL, 10);

	/* read strings and save temporary locations in tmpbuf */
	dm_p->host    = strtok(NULL, "\n");
	dm_p->rd_path = strtok(NULL, "\n");

	/* copy strings into the buffer; check to make sure we have the space */
	b_p = (char *) buf + sizeof(struct dmeta);

	tlen = strlen(dm_p->host)+1;
	if (b_p + tlen > (char *) buf + len) {
		errno = EOVERFLOW;
		return(-1);
	}
	bcopy(dm_p->host, b_p, tlen); /* hostname */
	b_p += tlen;

	tlen = strlen(dm_p->rd_path)+1;
	if (b_p + tlen > (char *) buf + len) {
		errno = EOVERFLOW;
		return(-1);
	}
	bcopy(dm_p->rd_path, b_p, tlen); /* root dir */
	b_p += tlen;

	ret = (int)(b_p - (char *) buf);
	return((ret < 0) ? -1 : ret);
} /* end of dmeta_read() */

/* DMETA_CLOSE() - closes an open CAPFS directory metadata file
 */
int dmeta_close(int fd)
{
	return close(fd);
} /* end of dmeta_close() */

/* DMETA_UNLINK() - removes a CAPFS directory metadata file
 */
int dmeta_unlink(char *pathname)
{
	return(0);
} /* end of dmeta_unlink() */

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
