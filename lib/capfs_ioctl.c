/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


/* Notes:
 * - Currently, SETPART is not implemented for normal files.
 * - SETPART resets the file offset to 0
 *
 */

/* CAPFS INCLUDES */
#include <lib.h>

/* UNIX INCLUDES */
#include <sys/ioctl.h>
#include <errno.h>

/* GLOBALS/EXTERNS */
extern fdesc_p pfds[];

int capfs_ioctl(int fd, int cmd, void *data)
{
	fpart     p_part={0,0,0,0,0};     /* partition */

	if (fd < 0 || fd >= CAPFS_NR_OPEN 
	    || (pfds[fd] && pfds[fd]->fs == FS_RESV))
	{
		errno = EBADF;
		return(-1);
	}

	if (!pfds[fd]) return ioctl(fd, cmd, data);

	switch(cmd) {
		case GETPART:
			if (pfds[fd]->part_p) *(fpart_p)data = *pfds[fd]->part_p;
			else *(fpart_p)data = p_part;
			return(0);
		case SETPART:
			if (!pfds[fd]->part_p) {
				if (!(pfds[fd]->part_p = malloc(sizeof(fpart)))) {
					PERROR(SUBSYS_LIB,"capfs_ioctl: malloc");
					return(-1);
				}
			}
			*pfds[fd]->part_p = *(fpart_p)data;
			pfds[fd]->fd.off = 0; /* reset file pointer */
			return(0);
		case GETMETA:
			*(capfs_filestat_p)data = pfds[fd]->fd.meta.p_stat;
			return(0);
		default:
			if (pfds[fd]->fs==FS_UNIX || pfds[fd]->fs==FS_PDIR) {
				return ioctl(fd, cmd, data);
			}
			LOG(stderr, WARNING_MSG, SUBSYS_LIB,  "capfs_ioctl: command %x not implemented\n", cmd);
			errno = EINVAL;
			return(-1);
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
