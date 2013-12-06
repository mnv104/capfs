/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

#include <capfs.h>
#include <capfs_proto.h>

int capfs_mode =1;

int usage(int argc, char **argv);

/* NOTES:
 *
 * This is a quick and silly little tool to allow for creation of CAPFS
 * symbolic and hard links without the preloaded library or kernel 
 * module or whatever.  It is here to make testing easier.  Enjoy.
 */
int main(int argc, char **argv)
{
	char *target_name = NULL, *link_name = NULL;
	int soft = 0, i;

	if(argc != 2 && argc != 3 && argc != 4) {
		usage(argc, argv);
		return -1;
	}
	for(i=1; i < argc; i++) {
		if(!strncmp(argv[i], "-s", 2)) {
			soft = 1;
		}
		else if(!strncmp(argv[i], "-h", 2)) {
			usage(argc, argv);
			return 0;
		}
		else {
			if(!target_name) {
				target_name = argv[i];
			}
			else if(!link_name) {
				link_name = argv[i];
			}
			else {
				fprintf(stderr, "Could not recognize the arguments\n");
				usage(argc, argv);
				return -1;
			}
		}
	}
	if(!target_name || !link_name) {
		fprintf(stderr, "Both the target name and link name need to be specified\n");
		usage(argc, argv);
		return -1;
	}
	if(soft) {
		if ( capfs_symlink(target_name, link_name) == -1 ) {
			perror("capfs_symlink");
			return -1;
		}
	}
	else {
		if ( capfs_link(target_name, link_name) == -1 ) {
			perror("capfs_link");
			return -1;
		}
	}
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
