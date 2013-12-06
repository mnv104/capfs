/*
 * (C) 2002 Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */
#include <capfs.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include <string.h>
/* have to use getdents(2)-ish dirent for capfs_getdents */
#include <unistd.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/unistd.h>

#define NR_DIRENT 256
int capfs_mode = 1;

extern int capfs_getdents(int fd, struct dirent *userp, unsigned int count);

int main (int argc, char ** argv ) {
	struct dirent *de;
	int fd, res, i=0, size=0, req_size;
	char * dirname;


	req_size = sizeof(struct dirent) * NR_DIRENT;
	if ( (de = calloc(1,req_size)) == NULL) {
		perror("calloc");
		exit(-1);
	}

	if ( argc == 1 ) {
		dirname = strdup(".");
	} else {
		dirname = strdup(argv[1]);
	}
	if ( (fd=capfs_open(dirname, O_RDONLY, 0755, NULL, NULL )) == -1 ) {
		perror("capfs_open");
		exit(-1);
	}
	/* should get a chunk of directory entries at a time, instead of one */
	/* the dirent used in capfs_getdents is like that specified 
	 * in getdents(2) */
	while (  ( res=capfs_getdents(fd, de, req_size)) != 0 ) {
		if ( res == -1 ) {
			perror("capfs_getdents");
			exit(-1);
		}
		i=0, size=0;
		do {
			struct dirent *new_de;

			new_de = ( struct dirent *)((char *)de + size);
			printf("%s\n", new_de->d_name);
			size+=new_de->d_reclen;
			i++;
		} while ( i<NR_DIRENT && size < res ); 
	}
	free(de);
	free(dirname);
	capfs_close(fd);
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
