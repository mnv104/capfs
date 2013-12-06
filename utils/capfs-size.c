/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>


#include <capfs.h>
#include <capfs_proto.h>

int capfs_mode = 1;

/* NOTES:
 *
 * This is a quick and silly little tool to show the size of CAPFS
 * files without the preloaded library or kernel module or whatever.  It
 * is here to make testing easier.  Enjoy.
 */
int main(int argc, char **argv)
{
	int fd;
	int64_t size;

	/* wow, I actually check to see that there is a parameter! */
	if (argc != 2) {
		fprintf(stderr, "%s: <filename>\n", argv[0]);
		return 0;
	}

	fd = capfs_open(argv[1], O_RDONLY, 0,0,0);
	if (fd < 0) {
		perror("capfs_open");
		return 0;
	}
	size = capfs_lseek64(fd, 0L, SEEK_END);
	if (size < 0) {
		perror("capfs_lseek64");
		return 0;
	}
	printf("%s: %Ld bytes\n", argv[1], size);
	capfs_close(fd);
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
