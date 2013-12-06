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

int do_truncate(int fd, off_t size)
{
	return ftruncate(fd, size);
}

int do_write(int fd, const void *buf, size_t count)
{
	return write(fd, buf, count);
}

int do_read(int fd, void *buf, size_t count)
{
	return read(fd, buf, count);
}

int do_lseek(int fd, off_t seekdist, int whence)
{
	return lseek(fd, seekdist, whence);
}

long do_stat(char *fname)
{
	struct stat sbuf;
	if (stat(fname, &sbuf) < 0) {
		return -1;
	}
	return sbuf.st_size;
}

void print_buf(char *buf, int size)
{
	int i;
	for (i = 0; i < size; i++)
	{
		printf("%2c\n", buf[i]);
	}
	return;
}

#define FILENAME "/mnt/capfs/testfile"

int main(int argc, char *argv[])
{
	int fd, count = 0, nc = 1, oc = 2, i;
	char *wrbuf = NULL, *rdbuf = NULL, *extra_buf = NULL, *fname = NULL;
	off_t offset = 0, fsize = 0;
	int _count, _offset;
	char c;

	while ((c = getopt(argc, argv, "f:n:o:")) != EOF) {
		switch(c) {
			case 'f':
				fname = optarg;
				break;
			case 'n':
				nc = atoi(optarg);
				break;
			case 'o':
				oc = atoi(optarg);
				break;
			case '?':
			default:
				fprintf(stderr, "Usage: %s -f <filename> -n <count multiplier> -o <offset multiplier>\n", argv[0]);
				exit(1);
		}
	}
	if (nc <= 0 || oc <= 0) {
		fprintf(stderr, "Usage: %s -f <filename> -n <count multiplier> -o <offset multiplier>\n", argv[0]);
		exit(1);
	}
	srand(time(NULL));
	_count = (int) (nc * CAPFS_CHUNK_SIZE);
	_offset = (int) (oc * CAPFS_CHUNK_SIZE);
	count = rand() % _count;
	//offset = rand() % _offset;
	count = 100;
	printf("Writing to %s %d bytes at offset %ld\n", fname ? fname : FILENAME, count, offset);
	wrbuf = (char *) calloc(1, count);
	for (i = 0; i < count / sizeof(int); i++) {
		wrbuf[i] = i;
	}

	fd = fname ? do_open(fname, O_RDWR | O_CREAT | O_TRUNC) : do_open(FILENAME, O_RDWR | O_CREAT | O_TRUNC);
	if (fd < 0) {
		printf("do_open - %s\n", strerror(errno));
		exit(1);
	}
	do_lseek(fd, offset, SEEK_SET);
	do_write(fd, wrbuf, count);
	do_close(fd);
	fsize = fname ? do_stat(fname) : do_stat(FILENAME);
	printf("Size of file before truncate %ld (%ld)\n", fsize, offset + count);
	fd = fname ? do_open(fname, O_RDWR) : do_open(FILENAME, O_RDWR);
	do_truncate(fd, offset + count / 2);
	fsize = fname ? do_stat(fname) : do_stat(FILENAME);
	printf("Size of file after truncate %ld (%ld)\n", fsize, offset + count / 2);
	rdbuf = (char *) calloc(fsize, 1);
	do_read(fd, rdbuf, fsize);
	do_close(fd);
	extra_buf = (char *) calloc(1, count/2);
	if (memcmp(rdbuf + offset, wrbuf, count / 2) == 0) {
		printf("Matches file content\n");
		if (memcmp(rdbuf + offset + count / 2, extra_buf, count/2) == 0) {
			printf("Rest of file seems to be zeroed out correctly!\n");
		}
		else {
			printf("Rest of file seems to be NOT zeroed out. This is OK! POSIX semantics on truncated file is undefined\n");
		}
	}
	else {
		printf("Does not match\n");
	}
	return 0;
}
