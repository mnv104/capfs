/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */

#include <lib.h> /* bunches of defines, including the CAPFS ones */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <errno.h>
#include <mntent.h>
#include <alloca.h>
#include <log.h>

#include <sockio.h>

static char canonicalize_outbuf[MAXPATHLEN];

static char *canonicalize (const char *name);
struct mntent *search_fstab(char *dir);

/* capfs_detect() - given a file name, attempts to determine if the given
 * file is a CAPFS one
 *
 * Returns -1 on error.
 * Returns 0 if file name does not appear to refer to a CAPFS file.
 * Returns 1 if file name refers to an existing CAPFS file OR if it
 *   refers to a file that does not yet exist but that would exist
 *   on a CAPFS file system.
 * Returns 2 if file name refers to an existing CAPFS directory.
 *
 * IF the name refers to a CAPFS file, we return a pointer to our static
 * region holding the canonicalized file name.  This isn't too
 * thread-safe <smile>, but neither are a lot of other things in this
 * version of the library...
 */
int capfs_detect(const char *fn, char **sname_p, struct sockaddr **saddr_p,
int64_t *fs_ino_p, int64_t *fino_p, int to_follow)
{
	struct mntent *ent;
	int len;
	char *remainder, *fsdir;
	mreq req;
	mack ack;
	static struct sockaddr saddr;
	static char snambuf[MAXPATHLEN+1];
	u_int16_t port;
	char host[1024];
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(req));
	/* canonicalize the filename, result in canonicalize_outbuf */
	if (!canonicalize(fn)) return(-1);


	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "canonicalize: %s\n", canonicalize_outbuf);

	/* check for capfs fstab match, return 0 if no match */
	if (!(ent = search_fstab(canonicalize_outbuf))) {
		LOG(stderr, INFO_MSG, SUBSYS_LIB,  "search_fstab didn't find a match\n");
		return(0);
	}

	/* piece together the filename at the server end
	 *
	 * Steps:
	 * 1) skip over hostname and ':' in fsname
	 * 2) replace directory name in filename with one on server
	 *
	 * Assumption: unless mnt_dir refers to the root directory, there
	 * will be no trailing /.
	 */
	if (!(fsdir = strchr(ent->mnt_fsname, ':'))) return(-1);
	fsdir++;
	len = strlen(ent->mnt_dir);
	remainder = canonicalize_outbuf + len;
	snprintf(snambuf, MAXPATHLEN, "%s/%s", fsdir, remainder);
	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "capfs name: %s\n", snambuf);

	/* ideally we would like to hit the manager here to see if this is a
	 * directory or a file, but we don't want to use stat because it
	 * causes traffic to the iods.
	 *
	 * So instead we do an access call, which now returns the file stats
	 * too (except for size).  This way we can check to see if we're
	 * looking at a file or directory.
	 */
	req.uid = getuid();
	req.gid = getgid();
	req.type = MGR_ACCESS;
	req.dsize = strlen(snambuf); /* null terminated at other end */
	req.req.access.mode = F_OK;
	req.req.access.to_follow = to_follow;
	ack.majik_nr = 0;

	/* piece together host and port for server */
	if (!(remainder = strchr(ent->mnt_fsname, ':'))) return(-1);
	if (!(len = remainder - ent->mnt_fsname)) return(-1);
	strncpy(host, ent->mnt_fsname, len);
	*(host + len) = '\0';

	if (!(remainder = strstr(ent->mnt_opts, "port="))) return(-1);
	remainder += 5; /* strlen("port=") */
	port = atoi(remainder);

	/* send the request; don't return an error just because we get -1
	 * back from send_mreq_saddr; file might have just not existed
	 */
	if (init_sock(&saddr, host, port)) {
		return(-1);
	}
	/* send_mreq_saddr < 0 means mgr is down or something */
	if (send_mreq_saddr(&opt, &saddr, &req, snambuf, &ack, NULL) < 0) {
		PERROR(SUBSYS_LIB,"capfs_detect: send_mreq_saddr -");
		return -1;
	}

	*saddr_p = &saddr;
	*sname_p = snambuf;
	if (ack.status == 0) {
		*fs_ino_p = ack.ack.access.meta.fs_ino;
		if(fino_p) *fino_p = ack.ack.access.meta.u_stat.st_ino;
		if (S_ISDIR(ack.ack.access.meta.u_stat.st_mode)) 
			return(2);
	}
	else {
		if (ack.eno == ENOENT) {
			return(1);
		}
	}

	/* fill in dmeta structure */
	return(1);
}

/* Same as above, but also slap the URL of meta-server in front of the name */
int capfs_detect2(const char *fn, char **sname_p, struct sockaddr **saddr_p,
int64_t *fs_ino_p, int64_t *fino_p, int to_follow)
{
	struct mntent *ent;
	int len;
	char *remainder, *fsdir;
	mreq req;
	mack ack;
	static struct sockaddr saddr;
	static char snambuf[MAXPATHLEN+1];
	u_int16_t port;
	char host[1024];
	struct capfs_options opt;
	
	opt.tcp = MGR_USE_TCP;
	opt.use_hcache = 0;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(req));
	/* canonicalize the filename, result in canonicalize_outbuf */
	if (!canonicalize(fn)) return(-1);


	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "canonicalize: %s\n", canonicalize_outbuf);

	/* check for capfs fstab match, return 0 if no match */
	if (!(ent = search_fstab(canonicalize_outbuf))) {
		LOG(stderr, INFO_MSG, SUBSYS_LIB,  "search_fstab didn't find a match\n");
		return(0);
	}

	/* piece together the filename at the server end
	 *
	 * Steps:
	 * 1) skip over hostname and ':' in fsname
	 * 2) replace directory name in filename with one on server
	 *
	 * Assumption: unless mnt_dir refers to the root directory, there
	 * will be no trailing /.
	 */
	if (!(fsdir = strchr(ent->mnt_fsname, ':'))) return(-1);
	fsdir++;
	len = strlen(ent->mnt_dir);
	remainder = canonicalize_outbuf + len;
	snprintf(snambuf, MAXPATHLEN, "%s/%s", fsdir, remainder);
	LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "capfs name: %s\n", snambuf);

	/* ideally we would like to hit the manager here to see if this is a
	 * directory or a file, but we don't want to use stat because it
	 * causes traffic to the iods.
	 *
	 * So instead we do an access call, which now returns the file stats
	 * too (except for size).  This way we can check to see if we're
	 * looking at a file or directory.
	 */
	req.uid = getuid();
	req.gid = getgid();
	req.type = MGR_ACCESS;
	req.dsize = strlen(snambuf); /* null terminated at other end */
	req.req.access.mode = F_OK;
	req.req.access.to_follow = to_follow;
	ack.majik_nr = 0;

	/* piece together host and port for server */
	if (!(remainder = strchr(ent->mnt_fsname, ':'))) return(-1);
	if (!(len = remainder - ent->mnt_fsname)) return(-1);
	strncpy(host, ent->mnt_fsname, len);
	*(host + len) = '\0';

	if (!(remainder = strstr(ent->mnt_opts, "port="))) return(-1);
	remainder += 5; /* strlen("port=") */
	port = atoi(remainder);

	/* send the request; don't return an error just because we get -1
	 * back from send_mreq_saddr; file might have just not existed
	 */
	if (init_sock(&saddr, host, port)) {
		return(-1);
	}
	/* send_mreq_saddr < 0 means mgr is down or something */
	if (send_mreq_saddr(&opt, &saddr, &req, snambuf, &ack, NULL) < 0) {
		PERROR(SUBSYS_LIB,"capfs_detect: send_mreq_saddr -");
		return -1;
	}

	*saddr_p = &saddr;
	*sname_p = (char *) calloc(1, MAXPATHLEN);
	if (*sname_p) {
		snprintf(*sname_p, MAXPATHLEN, "%s:%d%s", host, port, snambuf);
	}
	if (ack.status == 0) {
		*fs_ino_p = ack.ack.access.meta.fs_ino;
		if(fino_p) *fino_p = ack.ack.access.meta.u_stat.st_ino;
		if (S_ISDIR(ack.ack.access.meta.u_stat.st_mode)) 
			return(2);
	}
	else {
		if (ack.eno == ENOENT) {
			return(1);
		}
	}

	/* fill in dmeta structure */
	return(1);
}


/* MODIFIED CANONICALIZE FUNCTION FROM GLIBC BELOW */

/* Return the canonical absolute name of file NAME.	A canonical name
	does not contain any `.', `..' components nor any repeated path
	separators ('/') or symlinks.

	Path components need not exist.	If they don't, it will be assumed
	that they are not symlinks.	This is necessary for our work with
	CAPFS.

	Output is returned in canonicalize_outbuf.

	If RESOLVED is null, the result is malloc'd; otherwise, if the
	canonical name is PATH_MAX chars or more, returns null with `errno'
	set to ENAMETOOLONG; if the name fits in fewer than PATH_MAX chars,
	returns the name in RESOLVED.	If the name cannot be resolved and
	RESOLVED is non-NULL, it contains the path of the first component
	that cannot be resolved.	If the path can be resolved, RESOLVED
	holds the same value as the value returned.
 */

#define __set_errno(x) errno = (x)
#define __alloca alloca
#define __getcwd getcwd
#define __readlink readlink
#define __lxstat(x,y,z) unix_lstat((y),(z))

static char *canonicalize (const char *name)
{
	char *rpath, *dest, *extra_buf = NULL;
	const char *start, *end, *rpath_limit;
	long int path_max;
	int num_links = 0, err = 0;

	if (name == NULL) {
		/* As per Single Unix Specification V2 we must return an error if
			 either parameter is a null pointer.	We extend this to allow
			 the RESOLVED parameter be NULL in case the we are expected to
			 allocate the room for the return value.	*/
		__set_errno (EINVAL);
		return NULL;
	}

	if (name[0] == '\0') {
		/* As per Single Unix Specification V2 we must return an error if
			 the name argument points to an empty string.	*/
		__set_errno (ENOENT);
		return NULL;
	}

#ifdef PATH_MAX
	path_max = PATH_MAX;
#else
	path_max = pathconf (name, _PC_PATH_MAX);
	if (path_max <= 0)
		path_max = 1024;
#endif

	rpath = (char *) __alloca (path_max);
	rpath_limit = rpath + path_max;

	if (name[0] != '/') {
		if (!__getcwd (rpath, path_max))
			goto error;
		dest = strchr (rpath, '\0');
	}
	else {
		rpath[0] = '/';
		dest = rpath + 1;
	}

	for (start = end = name; *start; start = end) {
		struct stat st;
		int n;

		/* Skip sequence of multiple path-separators.	*/
		while (*start == '/')
			++start;

		/* Find end of path component.	*/
		for (end = start; *end && *end != '/'; ++end)
			/* Nothing.	*/;

		if (end - start == 0)
			break;
		else if (end - start == 1 && start[0] == '.')
			/* nothing */;
		else if (end - start == 2 && start[0] == '.' && start[1] == '.') {
			/* Back up to previous component, ignore if at root already.	*/
			if (dest > rpath + 1)
				while ((--dest)[-1] != '/');
		}
		else {
			if (dest[-1] != '/')
				*dest++ = '/';

			if (dest + (end - start) >= rpath_limit) {
				__set_errno (ENAMETOOLONG);
				goto error;
			}

			memmove(dest, start, end - start);
			dest = (char *) dest + (end - start);
			*dest = '\0';

			/* let's try changing the semantic to always ignore
			 * underlying filesystem if path so far is in capfstab file.
			 * That way we don't get silly kernel errors when using the
			 * library.  This is easy - just move (well, just copy for
			 * now) the search_fstab check to here. If you get a match,
			 * increment err.  err doesn't actually signal a fatal error,
			 * just causes it to quit getting at underlying
			 * filesystem. -- don
			 *
			 */

			if (search_fstab(rpath)) { /* rpath is in the capfstab */
				err++;
			}
			
			/* we used to crap out in this case; now we simply note that we
			 * hit an error and stop trying to stat from now on. -- Rob
			 */

			if (!err && __lxstat (_STAT_VER, rpath, &st) < 0) {
				err++;
			}
			if (!err && (S_ISLNK (st.st_mode))) {
				char *buf = (char *) __alloca (path_max);
				size_t len;

				if (++num_links > MAXSYMLINKS) {
					__set_errno (ELOOP);
					goto error;
				}

				n = __readlink (rpath, buf, path_max);
				if (n < 0)
					goto error;
				buf[n] = '\0';

				if (!extra_buf)
					extra_buf = (char *) __alloca (path_max);

				len = strlen (end);
				if ((long int) (n + len) >= path_max) {
					__set_errno (ENAMETOOLONG);
					goto error;
				}

				/* Careful here, end may be a pointer into extra_buf... */
				memmove (&extra_buf[n], end, len + 1);
				name = end = memcpy (extra_buf, buf, n);

				if (buf[0] == '/')
					dest = rpath + 1;	/* It's an absolute symlink */
				else
					/* Back up to previous component, ignore if at root already: */
					if (dest > rpath + 1)
						while ((--dest)[-1] != '/');
			}
		}
	}
	if (dest > rpath + 1 && dest[-1] == '/')
		--dest;
	*dest = '\0';

	memcpy(canonicalize_outbuf, rpath, dest - rpath + 1);
	return(canonicalize_outbuf);

error:
	/* copy in component causing trouble */
	strcpy (canonicalize_outbuf, rpath);
	return(canonicalize_outbuf);
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



