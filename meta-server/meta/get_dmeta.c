/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <meta.h>
#include <sys/param.h>
#include <metaio.h>
#include <log.h>

#define TMPSZ 512

int get_dmeta(char * fname, dmeta_p dir);

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
		LOG(stderr, CRITICAL_MSG, SUBSYS_META, "use current working directory!?!?!  shouldn't be here!!!\n");
		return -1;
	}
	else if ((fd = dmeta_open(nbuf, O_RDONLY)) < 0) {
		PERROR(SUBSYS_META,"dmeta_open");
		return(-1);
	}
	if (dmeta_read(fd, dmbuf, 4096) < 0) {
		PERROR(SUBSYS_META,"dmeta_read");
		return(-1);
	}
	dmeta_close(fd);

	/* copy the dmeta structure to the specified location */
	*dir = *(struct dmeta *)dmbuf;

	return(0);
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
