/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/param.h>

#include <capfs.h>
#include <capfs_proto.h>

int capfs_mode = 1;

int usage(int argc, char **argv);

/* NOTES:
 *
 * This is a quick and silly little tool to read the contents of a CAPFS
 * symbolic link without the preloaded library or kernel 
 * module or whatever.  It is here to make testing easier.  Enjoy.
 */
int main(int argc, char **argv)
{
	char *link_name = NULL, *buf = NULL;
	int i;

	if(argc != 2) {
		usage(argc, argv);
		return -1;
	}
	for(i=1; i < argc; i++) {
		if(!strncmp(argv[i], "-h", 2)) {
			usage(argc, argv);
			return 0;
		}
		else {
			if(!link_name) {
				link_name = argv[i];
			}
			else {
				fprintf(stderr, "Could not recognize the arguments\n");
				usage(argc, argv);
				return -1;
			}
		}
	}
	if(!link_name) {
		fprintf(stderr, "link name needs to be specified\n");
		usage(argc, argv);
		return -1;
	}
	buf = (char *)calloc(sizeof(char), MAXPATHLEN);
	if(!buf) {
		fprintf(stderr, "Out of memory\n");
		return -1;
	}
	if ( capfs_readlink(link_name, buf, MAXPATHLEN) == -1 ) {
		perror("capfs_readlink");
		free(buf);
		return -1;
	}
	fprintf(stdout, "%s\n", buf);
	free(buf);
	return 0;
}

int usage(int argc, char **argv)
{
	fprintf(stderr, "usage: %s <target file name> <link name>\n", argv[0]);
	fprintf(stderr," default: create hard links\n");
	fprintf(stderr," -s: create soft links\n");
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
