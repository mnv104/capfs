/*
 * Copyright (C) 2004
 * Murali Vilayannur
 *
 * This file contains the assorted routines from the 
 * PVFS sources that are used by the capfs.fsck program.
 * Unfortunately, these routines are not part of the
 * PVFS-library as they are used either by the MGR
 * or the IOD programs and hence the need to duplicate.
 */
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/param.h>

#include <capfs_config.h>
#include <capfs.h>
#include <meta.h>
#include <metaio.h>
#include <iodtab.h>
#include <log.h>

#define INBUFSZ 1024

static struct iodtabinfo iods;

struct iodtabinfo *my_parse_iodtab(char *fname)
{
	FILE *cfile;
	char inbuf[INBUFSZ], *entry, *port;
	struct hostent *hep;

	/* open file */
	if (!(cfile = fopen(fname, "r"))) {
		PERROR(SUBSYS_NONE,"fopen");
		return(NULL);
	}

	iods.nodecount = 0;

	while (fgets(inbuf, INBUFSZ, cfile)) {
		if (iods.nodecount >= CAPFS_MAXIODS) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_NONE, "CAPFS_MAXIODS exceeded!\n");
			return(NULL);
		}
		/* standard comments get skipped here */
		if (*inbuf == '#') continue;

		/* blank lines get skipped here */
		if (!(entry = strtok(inbuf, "#\n"))) continue;

		for (port = entry; *port && *port != ':'; port++);
		if (*port == ':') /* port number present */ {
			char *err;
			int portnr;

			portnr = strtol(port+1, &err, 10);
			if (err == port+1) /* ack, bad port */ {
				LOG(stderr, CRITICAL_MSG, SUBSYS_NONE, "my_parse_iodtab: bad port\n");
				return(NULL);
			}
			iods.iod[iods.nodecount].sin_port = htons(portnr);
		}
		else /* use default port number */ {
			iods.iod[iods.nodecount].sin_port = htons(IOD_REQ_PORT);
		}
		*port = 0;
		if (!inet_aton(entry, &iods.iod[iods.nodecount].sin_addr)) {
			if (!(hep=gethostbyname(entry)))
				bzero((char *)&iods.iod[iods.nodecount].sin_addr,
					sizeof(struct in_addr));
			else
				bcopy(hep->h_addr,(char *)&iods.iod[iods.nodecount].sin_addr,
					hep->h_length);
		}
		iods.iod[iods.nodecount].sin_family = AF_INET;
		iods.nodecount++;
	}
	fclose(cfile);
	return(&iods);
} /* end of my_parse_config() */

/* META_OPEN() - opens a file as a PVFS metadata file and checks to make
 * sure that the file is indeed a PVFS metadata file
 *
 * Note: THIS IS NOT FOR CREATING NEW METADATA FILES!  Use meta_creat()
 * for that.  This will just give you an error.
 *
 * Returns a file descriptor (>=0) on success, -1 on failure.
 */
int meta_open(char *pathname, int flags)
{
	int fd;
	struct stat st;

	/* Check to make sure that the size is appropriate to prevent old
		metadata from being clobbered.  This is intended to save the
		bacon of one who forgets to run the migration script.*/
	if (lstat(pathname, &st) == 0) 
	{
		if (S_ISREG(st.st_mode) && st.st_size != sizeof(struct fmeta))
		{
         LOG(stderr, CRITICAL_MSG, SUBSYS_NONE,
             "meta_open: Metadata file %s is not the correct size.  This is usually due to "
             "running a newer mgr on an old PVFS file system or someone mucking with the "
             "files in the metadata directory (which they should not do).  Aborting!\n",
             pathname);
			errno = EINVAL;
			return -1;
		}
	}
	else if (errno != ENOENT)
	{
		PERROR(SUBSYS_NONE,"meta_open: stat");
		return -1;
	}

	/* no creating files with this; use meta_creat() */
	flags &= ~O_CREAT;

	if ((fd = open(pathname, flags, 0)) < 0) return(-1);

	/* looks good, return fd */
	return(fd);
} /* end of meta_open() */


/* META_CREAT() - creates a new PVFS metadata file
 */
int meta_creat(char *pathname, int flags)
{
	int fd, old_umask, cflags = O_RDWR | O_CREAT | O_EXCL,
	    mode = S_IRWXU | S_IRGRP | S_IROTH;

	/* create the file */
	old_umask = umask(0);
	fd = open(pathname, cflags, mode);
	umask(old_umask);
	if(fd < 0){
		return(-1);
	}

#ifdef USE_NEW_META_FORMAT
	/* write in the identifier */
	if (write(fd, magic, MAGIC_SZ) < MAGIC_SZ) return(-1);
#endif
	/* MAYBE WRITE IN DUMMY METADATA? */
	close(fd);

	/* reopen the new file using the specified flags */
	return(meta_open(pathname, flags));
} /* end of meta_creat() */

/* META_READ() - read the file metadata from a PVFS metadata file
 *
 * Returns 0 if successful, or -1 on error.
 */
int meta_read(int fd, struct fmeta *meta_p)
{
	int ret;

	/* seek past magic */
	if (lseek(fd, 0, SEEK_SET) < 0) return(-1);

	ret = read(fd, meta_p, sizeof(struct fmeta));
	if (ret == sizeof(struct fmeta)) {
#ifndef MGR_USE_CACHED_FILE_SIZE
			  meta_p->u_stat.st_size=0;
#endif
			  return 0;
	}
	/* it's no longer ok to read the old 1_5_0 structure */
	return -1;
} /* end of meta_read() */


/* META_WRITE() - write file metadata into a PVFS metadata file
 *
 * Returns 0 if successful, or -1 on error.
 */
int meta_write(int fd, struct fmeta *meta_p)
{
	/* seek past magic */
	if (lseek(fd, 0, SEEK_SET) < 0) return(-1);

	return write(fd, meta_p, sizeof(struct fmeta))==sizeof(struct fmeta) ? 0:-1;
} /* end of meta_write() */


/* META_CLOSE() - closes an open PVFS metadata file
 */
int meta_close(int fd)
{
	/* all we do right now is close the file... */
	return close(fd);
} /* end meta_close() */

/* fname() - quick little function to build the filename
 */
char *fname(ino_t f_ino) {
	static char buf[1024];
	int dir;

	dir = f_ino % IOD_DIR_HASH_SZ;
	sprintf(buf, "%03d/f%Ld.%d", dir, (long long)f_ino, 0);
	return(buf);
}

/* Directory manipulation stuff */

/* DMETA_OPEN() - opens a PVFS directory metadata file and checks to make
 * sure that the file is indeed a PVFS directory metadata file
 *
 * Takes a pathname to a PVFS directory as an argument, not the name of
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


/* DMETA_CREAT() - creates a new PVFS directory metadata file
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


/* DMETA_READ() - read the directory metadata from a PVFS directory
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

/* DMETA_CLOSE() - closes an open PVFS directory metadata file
 */
int dmeta_close(int fd)
{
	return close(fd);
} /* end of dmeta_close() */

/* get_dmeta() - grabs directory metadata in the directory of a given
 * file
 *
 * NOTE: metadata is only valid until next call to get_dmeta()
 */
int get_dmeta(char * fname, dmeta_p dir)
{
	int fd, len, is_dir = 0, use_cwd = 0;
	struct stat filestat;
	char nbuf[MAXPATHLEN];
	static char dmbuf[4096];
	int ret = -1;

	strncpy(nbuf, fname, MAXPATHLEN);

	/* strip off any trailing /'s on the file name */
	for (len = strlen(nbuf); len > 0 && nbuf[len] == '/'; nbuf[len--] = '\0');
	if (len < 0) return(-1);

	/* determine if name refers to a directory or file, find appropriate
	 * directory name
	 */
	ret = lstat(nbuf, &filestat);
	if (ret < 0 || !S_ISDIR(filestat.st_mode)) {
		/* need to strip off trailing name, pass the rest to dmetaio
		 * calls so we get the dir. metadata for the parent directory
		 *
		 * we don't want to wax the last bit of the name, because we need
		 * it later...
		 */
		for (/* len already set */; len >= 0 && nbuf[len] != '/'; len--);
		if (len <= 0) /* no directory; use current working directory */ {
			/* should not ever happen as of v1.4.2 and later */
			use_cwd = 1;
		}
		else /* the fname wasn't just a file name */ {
			nbuf[len] = '\0';
		}
	}
	else /* it's a directory */ {
		is_dir = 1;
	}
	
	/* open the dir. metadata file, read it, get the info */
	if (use_cwd) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_NONE, "use current working directory!?!?!  shouldn't be here!!!\n");
		return -1;
	}
	else if ((fd = dmeta_open(nbuf, O_RDONLY)) < 0) {
		PERROR(SUBSYS_NONE,"dmeta_open");
		return(-1);
	}
	if (dmeta_read(fd, dmbuf, 4096) < 0) {
		PERROR(SUBSYS_NONE,"dmeta_read");
		return(-1);
	}
	dmeta_close(fd);

	/* copy the dmeta structure to the specified location */
	*dir = *(struct dmeta *)dmbuf;

	return(0);
}

int md_mkdir(char *dirpath, dmeta_p dir)
{
	FILE *fp;
	char temp[MAXPATHLEN];

	/* create dotfile */
	strncpy(temp, dirpath, MAXPATHLEN);
	strcat(temp,"/.capfsdir");
	umask(0033);
	if ((fp = fopen(temp, "w+")) == 0) {
		PERROR(SUBSYS_NONE,"Error creating dot file");
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

int create_capfs_dir(char *dir_path)
{
	dmeta dir;

	if (get_dmeta(dir_path, &dir) < 0) {
		errno = ENOENT;
		return -1;
	}
	if (mkdir(dir_path, 0775) < 0) {
		return -1;
	}
	dir.dr_uid = 0;
	dir.dr_gid = 0;
	dir.dr_mode = 0755;
	if (md_mkdir(dir_path, &dir) < 0) {
		errno = ENOENT;
		return -1;
	}
	return 0;
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
