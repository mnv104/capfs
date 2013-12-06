/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/*
 * 10/27/2000 - THIS IS ALL MOOT NOW; JUST CALLING STAT CALLS
 *
 */

#include <sys/stat.h>


int unix_lstat(const char *fn, struct stat *s_p)
{
	return lstat(fn, s_p);
}

int unix_stat(const char *fn, struct stat *s_p)
{
	return stat(fn, s_p);
}

int unix_fstat(int fd, struct stat *s_p)
{
	return fstat(fd, s_p);
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
