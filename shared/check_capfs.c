/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/*
 * CHECK_CAPFS.C - determine if a file or directory is a CAPFS file/dir.
 *
 */

#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/timeb.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <resv_name.h>

int unix_stat(const char *fn, struct stat *s_p);

/* check_capfs() - checks to see if the filename pointed to by "pathname"
 * is or would be a CAPFS file.  This is determined as follows.  If the
 * name refers to a file that exists or a name that does not exist, the
 * directory is checked for the existance of a ".capfsdir" file.  If this
 * file is there, the file is (or would be) a CAPFS file and 1 is
 * returned.
 *
 * If there is no ".capfsdir" file, 0 is returned.
 * If there is a ".capfsdir" file and the pathname refers to a directory,
 * 2 is returned.
 */

#define MAXTMP 512
int check_capfs(const char* pathname)
{
	int length, isdir=0;
	char tmp[MAXTMP];
	struct stat foo;

	if (!pathname) {
		errno = EFAULT;
		return(-1);
	}

	strncpy(tmp, pathname, MAXTMP);
	length = strlen(tmp);
	if (length == 0) {
		errno = ENOENT;
		return(-1);
	}
	if (unix_stat(tmp, &foo) < 0) {
		/* file doesn't exist.  We need to fool the next check so that we
		 * can see if this would be a CAPFS file were it to be created...
		 */
		foo.st_mode = 0;
	}
	if (!S_ISDIR(foo.st_mode)) {
		/* find division between file name and directory */
		for (; length >= 0 && tmp[length] != '/'; length--);

		/* NEW CODE AND BEHAVIOR: RETURN -1 FOR RESERVED NAMES, USE NEW FN */
		if (resv_name(pathname)) {
			errno = ENOENT;
			return -1;
		}
		/* END OF NEW CODE */

		if (length < 0) /* no directory here! */ {
			tmp[0] = '.';
			tmp[1] = 0;
		}
		else  /* cut off the file name */ {
			tmp[length] = 0;
		}
	}
	else {
		isdir=1;
	}
	strcat(tmp, "/.capfsdir");
	if (unix_stat(tmp, &foo) < 0) {
		return(0);
	}
	else {
		/* Otherwise must be capfs file */
		return(1+isdir);
	}
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
