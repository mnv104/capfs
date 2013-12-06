/*
 * quick way to get the hashes of a particular file
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <netdb.h>
#include <sockio.h>

#include <capfs.h>
#include <capfs_proto.h>
#include "capfs_config.h"
#include "mgr_prot.h"
#include "sha.h"

#define BUFSZ (128 * 1024)

int usage(int argc, char **argv);

extern char *optarg;
extern int optind, opterr;
int capfs_mode = 0;

int main(int argc, char **argv)
{
	int ret, opt, fd;
	int64_t begin = 0;
	char *fname = NULL;
	unsigned char *ptr = NULL;

	while ((opt = getopt(argc, argv,"f:")) != EOF) {
		switch (opt) {
			case 'f' : fname = optarg; break;
			default : usage(argc, argv); exit(1);
		}
	}
	if (fname == NULL)
	{
		usage(argc, argv);
		exit(1);
	}
	
	ptr = (unsigned char *) calloc(CAPFS_MAXHASHES, CAPFS_MAXHASHLENGTH);
	if (ptr == NULL)
	{
		usage(argc, argv);
		exit(1);
	}
	fd = capfs_open(fname, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "No such file %s\n", fname);
		exit(1);
	}
	do {
		int i;

		ret = capfs_gethashes(fname, ptr, begin, 100);
		if (ret < 0) {
			perror("capfs_gethashes:");
			return -1;
		}
		else if (ret == 0)
		{
			break;
		}
		for (i = 0; i < ret; i++)
		{
			char str[256];
			hash2str(ptr + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH, str);
			printf("%d: %s\n", i, str);
		}
		begin += ret;
	} while (1);
	capfs_close(fd);
	return 0;
}

int usage(int argc, char **argv)
{
	fprintf(stderr, "usage: %s -f <filename>\n", argv[0]);
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


