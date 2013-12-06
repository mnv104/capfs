#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include "capfs_config.h"

int do_open(char *fname, int flags)
{
	return open(fname, flags, 0775);
}

int do_close(int fd)
{
	return close(fd);
}

int do_lseek(int fd, off_t seekdist, int whence)
{
	return lseek(fd, seekdist, whence);
}

int do_write(int fd, const void *buf, size_t count)
{
	return write(fd, buf, count);
}

int do_read(int fd, void *buf, size_t count)
{
	return read(fd, buf, count);
}

long do_stat(char *fname)
{
	struct stat sbuf;
	if (stat(fname, &sbuf) < 0) {
		return -1;
	}
	return sbuf.st_size;
}

#define FILENAME "/mnt/capfs/testfile"

int main(int argc, char *argv[])
{
	int fd, count = 0, nc = 1, oc = 2, i;
	char *wrbuf = NULL, *rdbuf = NULL;
	off_t offset = 0, fsize = 0;
	int _count, _offset;
	char c;

	while ((c = getopt(argc, argv, "n:o:")) != EOF) {
		switch(c) {
			case 'n':
				nc = atoi(optarg);
				break;
			case 'o':
				oc = atoi(optarg);
				break;
			case '?':
			default:
				fprintf(stderr, "Usage: %s -n <count multiplier> -o <offset multiplier>\n", argv[0]);
				exit(1);
		}
	}
	if (nc <= 0 || oc <= 0) {
		fprintf(stderr, "Usage: %s -n <count multiplier> -o <offset multiplier>\n", argv[0]);
		exit(1);
	}
	srand(time(NULL));
	_count = (int) (nc * CAPFS_CHUNK_SIZE);
	_offset = (int) (oc * CAPFS_CHUNK_SIZE);
	count = rand() % _count;
	offset = rand() % _offset;
	printf("Writing %d bytes at offset %ld\n", count, offset);
	wrbuf = (char *) calloc(1, count);
	for (i = 0; i < count / sizeof(int); i++) {
		wrbuf[i] = i;
	}

	fd = do_open(FILENAME, O_RDWR | O_CREAT);
	if (fd < 0) {
		printf("do_open - %s\n", strerror(errno));
		exit(1);
	}
	do_lseek(fd, offset, SEEK_SET);
	do_write(fd, wrbuf, count);
	do_close(fd);
	fsize = do_stat(FILENAME);
	fd = do_open(FILENAME, O_RDWR);
	rdbuf = (char *) calloc(fsize, 1);
	do_read(fd, rdbuf, fsize);
	do_close(fd);
	if (memcmp(rdbuf + offset, wrbuf, count) == 0) {
		printf("Matches\n");
	}
	else {
		printf("Does not match\n");
	}
	return 0;
}

