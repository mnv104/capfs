/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/* METAIO - function calls used to access CAPFS metadata files
 *
 * Currently there are six functions that may be used to access these
 * files: meta_creat(), meta_open(), meta_read(), meta_write(),
 * meta_unlink() and meta_close().  Only these functions should be used
 * when accessing/manipulating CAPFS metadata files.
 *
 */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* CAPFS INCLUDES */
#include <capfs_config.h>
#include <meta.h>
#include <metaio.h>
#include <mgr.h>
#include <log.h>

#include <pwd.h>
#include <grp.h>

/* IN_GROUP() - determines if a uid is a member of a given gid
 *	return 0 on false/error, 1 on true 
 */
extern int check_capfs(char *pathname);

int in_group(uid_t uid, gid_t gid){
	struct passwd *pwd;
	struct group *grp;
	int i;

	/* check default group */
	if( (pwd = getpwuid(uid)) == NULL){
		return 0;
	}
	if(pwd->pw_gid == gid) return 1;
	
	/* check /etc/groups as well */
	if( (grp = getgrgid(gid)) == NULL){
		return 0;
	}

	for(i = 0; grp->gr_mem[i] != NULL; i++){
		if(0 == strcmp(pwd->pw_name, grp->gr_mem[i]) ){
			return 1;
		} 
	}

	/*should I perhaps free pwd and grp? -- don't think so. -- RobR */

	return 0;
}




/* META_OPEN() - opens a file as a CAPFS metadata file and checks to make
 * sure that the file is indeed a CAPFS metadata file
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
         LOG(stderr, CRITICAL_MSG, SUBSYS_META,
             "meta_open: Metadata file %s is not the correct size.  This is usually due to "
             "running a newer mgr on an old CAPFS file system or someone mucking with the "
             "files in the metadata directory (which they should not do).  Aborting!\n",
             pathname);
			errno = EINVAL;
			return -1;
		}
	}
	else if (errno != ENOENT)
	{
		PERROR(SUBSYS_META,"meta_open: stat");
		return -1;
	}

	/* no creating files with this; use meta_creat() */
	flags &= ~O_CREAT;

	if ((fd = open(pathname, flags, 0)) < 0) return(-1);

	/* looks good, return fd */
	return(fd);
} /* end of meta_open() */


/* META_CREAT() - creates a new CAPFS metadata file
 */
int meta_creat(char *pathname, int flags)
{
	int fd, old_umask, cflags = O_RDWR | O_CREAT | O_EXCL;
	int mode = S_IRWXU | S_IRGRP | S_IROTH;
	char hashpath[MAXPATHLEN];

	/* create the file */
	old_umask = umask(0);
	fd = open(pathname, cflags, mode);
	umask(old_umask);
	if (fd < 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "opening %s with cflag = %d failed\n", pathname, cflags);
		return(-1);
	}
	close(fd);
	/* also create the hash file */
	sprintf(hashpath, "%s.hashes", pathname);
	old_umask = umask(0);
	fd = open(hashpath, cflags, mode);
	umask(old_umask);
	if (fd < 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "opening hash file with %s and cflag = %d failed\n", hashpath, cflags);
		return(-1);
	}
	close(fd);
	/* reopen the new file using the specified flags */
	flags &= ~O_CREAT;
	return(open(pathname, flags, 0));
} /* end of meta_creat() */

/*
 * reads the file's hashes. Needs to be called with a readlock on the finfo_p structure.
 * This will basically try to read hashes from "begin_chunk" upto "*nchunks"
 * into a buffer that will be allocated internally.
 * *nchunks will be updated to the actual number of hashes that could be read!!
 * if begin_chunk is set to -1, the entire file's hashes are read.
 * *phashes will be set to point to the buffer containing the hashes that
 * must be freed by the caller.
 * name must be the meta-data file without the suffix.
 */

int meta_hash_read(char *name, int64_t begin_chunk, int64_t* nchunks, unsigned char **phashes)
{
	char hashpath[MAXPATHLEN];
	int fd;
	size_t act_size = 0, req_size = 0;
	struct stat sbuf;

	snprintf(hashpath, MAXPATHLEN, "%s.hashes", name);
	fd = open(hashpath, O_RDWR);
	if (fd < 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "meta_hash_read: hash file for %s has not yet been created?\n", name);
		*nchunks = -errno;
		return -1;
	}
	fstat(fd, &sbuf);
	if (sbuf.st_size % CAPFS_MAXHASHLENGTH != 0) {
		errno = EINVAL;
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "meta_hash_read: corrupted hash file %s's size is %Ld not a multiple of %d\n",
				hashpath, sbuf.st_size, CAPFS_MAXHASHLENGTH);
		close(fd);
		*nchunks = -errno;
		return -1;
	}
	if (begin_chunk < 0) {
		req_size = sbuf.st_size;
		begin_chunk = 0;
	}
	else {
		req_size = (*nchunks) * CAPFS_MAXHASHLENGTH;
	}
	if (req_size < 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "invalid value of req_size: %d\n", req_size);
		errno = EINVAL;
		close(fd);
		*nchunks = -errno;
		return -1;
	}
	*phashes = (char *) calloc(1, req_size);
	if (*phashes == NULL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "could not allocate memory\n");
		errno = ENOMEM;
		close(fd);
		*nchunks = -errno;
		return -1;
	}
	if ((act_size = pread(fd, *phashes, req_size, (begin_chunk * CAPFS_MAXHASHLENGTH))) < 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "could not read meta-data/hash file %s\n", strerror(errno)); 
		free(*phashes);
		*phashes = NULL;
		close(fd);
		*nchunks = -errno;
		return -1;
	}
	*nchunks = act_size / CAPFS_MAXHASHLENGTH;
	close(fd);
	return 0;
}

/* 
 * Write the hashes to the file's meta data. Definitely needs to be called with a write lock
 * on the finfo_p structure. Upto higher level to make sure that
 * the hashes are written after due verification that there was no race conditions.
 */
int meta_hash_write(char *name, int64_t begin_chunk, int64_t nchunks, unsigned char **phashes)
{
	char hashpath[MAXPATHLEN];
	int fd;
	int64_t i;
	struct stat sbuf;

	if (begin_chunk < 0 || nchunks <= 0) {
		errno = EINVAL;
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "meta_hash_write: invalid value of begin_chunk %Ld, nchunks: %Ld\n",
				begin_chunk, nchunks);
		return -1;
	}
	snprintf(hashpath, MAXPATHLEN, "%s.hashes", name);
	fd = open(hashpath, O_RDWR);
	if (fd < 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "meta_hash_write: hash file for %s has not yet been created?\n", name);
		return -1;
	}
	fstat(fd, &sbuf);
	if (sbuf.st_size % CAPFS_MAXHASHLENGTH != 0) {
		errno = EINVAL;
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "meta_hash_write: corrupted hash file %s's size is %Ld not a multiple of %d\n",
				hashpath, sbuf.st_size, CAPFS_MAXHASHLENGTH);
		close(fd);
		return -1;
	}
	for (i = 0; i < nchunks; i++) {
		if (pwrite(fd, phashes[i], CAPFS_MAXHASHLENGTH, (begin_chunk + i) * CAPFS_MAXHASHLENGTH) < 0) {
			close(fd);
			return -1;
		}
	}
	close(fd);
	return 0;
}

int meta_hash_truncate(char *name, int64_t new_nchunks)
{
   char hashpath[MAXPATHLEN];
   
   snprintf(hashpath, MAXPATHLEN, "%s.hashes", name);
   if (truncate(hashpath, new_nchunks * CAPFS_MAXHASHLENGTH) < 0) {
      LOG(stderr, CRITICAL_MSG, SUBSYS_META, "meta_hash_truncate: could not truncate file for %s to nchunks %Ld\n",
            name, new_nchunks);
      return -1;
   }

   return 0;
}

/* META_READ() - read the file metadata from a CAPFS metadata file
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
			  //meta_p->u_stat.st_size=0;
#endif
			  return 0;
	}
	/* it's no longer ok to read the old 1_5_0 structure */
	return -1;
} /* end of meta_read() */


/* META_WRITE() - write file metadata into a CAPFS metadata file
 *
 * Returns 0 if successful, or -1 on error.
 */
int meta_write(int fd, struct fmeta *meta_p)
{
	/* seek past magic */
	if (lseek(fd, 0, SEEK_SET) < 0) return(-1);

	return write(fd, meta_p, sizeof(struct fmeta))==sizeof(struct fmeta) ? 0:-1;
} /* end of meta_write() */


/* META_CLOSE() - closes an open CAPFS metadata file
 */
int meta_close(int fd)
{
	/* all we do right now is close the file... */
	return close(fd);
} /* end meta_close() */


/* META_UNLINK() - removes a CAPFS metadata file
 */
int meta_unlink(char *pathname)
{
	int fd, flags = O_RDONLY, ret;
	char hashpath[MAXPATHLEN];

	/* ensure this is a valid metadata file -- call meta_open() */
	if ((fd = meta_open(pathname, flags)) < 0) return(-1);
	meta_close(fd);

	/* looks good, try to unlink it */
	ret = unlink(pathname);
	/* also unlink any hashes file for this file */
	sprintf(hashpath, "%s.hashes", pathname);
	unlink(hashpath);
	return ret;
} /* end of meta_unlink() */

/*
 * GET_PARENT() - strips the child filename or directory from the
 * pathname and returns the new length.
 * This does cause side effects, pathname will be modified!
 */
int get_parent(char *pathname)
{
	int length;

	/* check for special case of "/" only */
	if (!strcmp(pathname, "/"))
		return(-1);
	/* strip off extra slashes */
	for (length=strlen(pathname)-1;length > 0 && pathname[length] == '/';
		length--);
	/* strip off file or directory name */
	for (;length >= 0 && pathname[length] != '/'; length--);
	/* strip off extra slashes */
	for (;length > 0 && pathname[length] == '/'; length--);
	switch (length) {
		case -1:	pathname[0] = '\0'; /* CWD used -- NULL parent */
					break;
		case  0:	strcpy(pathname, "/"); /* root directory */
					break;
		default: pathname[length+1] = '\0'; /* normal strip */
	}
	return(length);
}
/*
 *	META_ACCESS() - determines access capabilities for a given file and a
 *	given user. Returns 0 on success or -1 on failure and sets errno
 *	appropriate to the corresponding error.
 * If fd=0, meta_access will assume pathname points to a directory.
 * Values for the parameters are like that of access() except for fd
 * which does not exist in the access system call.
 */

int meta_access(int fd, char *pathname, uid_t uid, gid_t gid, int mode)
{
	char temp[MAXPATHLEN];
	int i,length;
	uid_t st_uid;
	gid_t st_gid;
	mode_t st_mode;
	fmeta metadat;
	dmeta dmetadat;

	/* check for valid values for parameters */
	if ((fd < 0) || (uid < 0) || (gid < 0) || (mode & 0xfffffff8)) {
		errno = EINVAL;
		return (-1);
	}
	i = check_capfs(pathname);
	switch (i) {
		case 0:	return(0); /* noncapfs, no need to check */
		case 1: 
		case 2:	break;
		default:
			errno = ENOENT;
			return(-1); /* file not found */
	}
	/* 
	 * determine if all directories in the path have execute permissions
	 * for the given user.  Could be recursive if the directory is a capfs
	 * mounted file system directory
	 */
	strncpy(temp, pathname, MAXPATHLEN);
	length = get_parent(temp); /* strips to just parent directory */
	if (length >= 0) {
		i = check_capfs(temp);
		switch (i) {
			/* noncapfs, no need to check */
			case 0:	break;
			/* recursive call to capfs directories */
			case 2:	if (meta_access(0, temp, uid, gid, X_OK) < 0)
							return(-1);
						break;
			/* if its a capfs file or an error, this is incorrect */
			default:	errno = ENOTDIR;
						return(-1);
		}
	}
	/* get file metadata */
	if ((fd != 0) && ((i = meta_read(fd, &metadat)) < 0) && (errno != EISDIR)) 
	{
		PERROR(SUBSYS_META,"meta_access: meta_read");
		return (-1);
	}
	/* directory? */
	if ((fd == 0) || (i < 0)) {
		/* get directory metadata */
		if (get_dmeta(pathname, &dmetadat) < 0)
				return(-1);
		st_uid  = dmetadat.dr_uid;
		st_gid  = dmetadat.dr_gid;
		st_mode = dmetadat.dr_mode;
	}
	else { /* yep, it's a meta file */
		/* seek back to beginning of file for future reads */
		if (lseek(fd, 0, SEEK_SET) < 0) return(-1);
		st_uid  = metadat.u_stat.st_uid;
		st_gid  = metadat.u_stat.st_gid;
		st_mode = metadat.u_stat.st_mode;
	}
	/* give root permission, no matter what */
	if (uid == 0) {
		return(0);
	}
	/* uid test */
	else if (uid == st_uid){
		if(((st_mode >> 6) & mode) == mode){ 
			return(0);
		}
	} 
	/* gid test */ 
 	else if (1 == in_group(uid, st_gid)){
		if(((st_mode >> 3) & mode) == mode){
			return(0);
		}
	}
	/* check for other permissions */
	else if ((mode & st_mode) == mode){
		return(0); /* success */
	}

	/* access denied */
	errno = EACCES;
	return(-1);
}


/*
 *	META_CHECK_OWNERSHIP() - checks to see if the given file or directory
 *	is ownership matches the given uid.  Returns 0 on match, -1 on
 *	failure and sets errno appropriate to the corresponding error.
 * If fd=0, meta_access will assume pathname points to a directory.
 */
int meta_check_ownership(int fd, char *pathname, uid_t uid, gid_t gid)
{
	char temp[MAXPATHLEN];
	int i,length;
	uid_t st_uid;
	fmeta metadat;
	dmeta dmetadat;

	/* check for valid values for parameters */
	if ((fd < 0) || (uid < 0)) {
		errno = EINVAL;
		return (-1);
	}
	i = check_capfs(pathname);
	switch (i) {
		case 0:	return(0); /* noncapfs, no need to check */
		case 1: 
		case 2:	break;
		default:
			errno = ENOENT;
			return(-1); /* file not found */
	}
	/* 
	 * determine if all directories in the path have execute permissions
	 * for the given user.  Could be recursive if the directory is a capfs
	 * mounted file system directory
	 */
	strncpy(temp, pathname, MAXPATHLEN);
	length = get_parent(temp); /* strips to just parent directory */
	if (length >= 0) {
		i = check_capfs(temp);
		switch (i) {
			/* noncapfs, no need to check */
			case 0:	break;
			/* recursive call to capfs directories */
			case 2:	if (meta_access(0, temp, uid, gid, X_OK) < 0)
							return(-1);
						break;
			/* if its a capfs file or an error, this is incorrect */
			default:	errno = ENOTDIR;
						return(-1);
		}
	}
	/* get file metadata */
	if ((fd != 0) && ((i = meta_read(fd, &metadat)) < 0) && (errno != EISDIR)) 
	{
		PERROR(SUBSYS_META,"meta_access: meta_read");
		return (-1);
	}
	/* directory? */
	if ((fd == 0) || (i < 0)) {
		/* get directory metadata */
		if (get_dmeta(pathname, &dmetadat) < 0)
				return(-1);
		st_uid  = dmetadat.dr_uid;
	}
	else { /* yep, it's a meta file */
		/* seek back to beginning of file for future reads */
		if (lseek(fd, 0, SEEK_SET) < 0) return(-1);
		st_uid  = metadat.u_stat.st_uid;
	}
	/* give root permission, no matter what */
	if (uid == 0)
		return(0);
	/* uid test */
	if (uid == st_uid)
		return(0);

	/* access denied */
	errno = EACCES;
	return(-1);
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
