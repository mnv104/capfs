#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "capfs_config.h"
#include "hcache.h"
#include <openssl/evp.h>

#define BSIZE 4096

static void print(unsigned char *hash, int hash_length)
{
	int i;

	for (i = 0; i < hash_length; i++) {
		printf("%02x", hash[i]);
	}
	printf("\n");
	return;
}

#define diff(p2, p1) (((p2)->tv_sec - (p1)->tv_sec) * 1e03 + ((p2)->tv_usec - (p1)->tv_usec) * 1e-03)

static void func(char *name)
{
	char *hash = NULL;
	int ret, i, total = 0;
	long off = 0;

	hash = (char *) calloc(EVP_MAX_MD_SIZE * 100, 1);
	while ((ret = hcache_get(name, off, 100, -1, hash)) > 0) {
		printf("ret = %d\n", ret);
		total += (ret / CAPFS_MAXHASHLENGTH);
		for (i = 0; i < ret / CAPFS_MAXHASHLENGTH; i++) {
			print(hash + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH);
		}
		off += (ret / CAPFS_MAXHASHLENGTH);
	}
	printf("name %s had %d hashes\n", name, total);
	return;
}

int main(int argc, char *argv[])
{
	int chunk_size = BSIZE;
	char str[256];
	struct stat sbuf;

	if (argc != 2 && argc != 3) {
		fprintf(stderr, "usage: %s <filename> {chunk size}\n", argv[0]);
		return 1;
	}
	if (argc == 3) {
		chunk_size = atoi(argv[2]);
	}
	stat(argv[1], &sbuf);
	sprintf(str,"%d", chunk_size);
	setenv("CMGR_CHUNK_SIZE", str, 1);
	sprintf(str,"%ld", sbuf.st_size / 10);
	setenv("CMGR_BCOUNT", str, 1);
	hcache_init(NULL);
	
	/*
	hash = (char *) calloc(EVP_MAX_MD_SIZE, sbuf.st_size / chunk_size + 1);
	for (i = 0; i < 10; i++) {
		gettimeofday(&begin, NULL);
		hcache_get(argv[1], 0, sbuf.st_size / chunk_size + 1, -1, hash);
		gettimeofday(&end, NULL);
		printf("Iteration %d took %g msec\n", i+1, diff(&end, &begin));
	}
	*/
	/*
	hash = (char *) calloc(EVP_MAX_MD_SIZE, 1000);
	for (j = 0; j < 10; j++) {
		for (i = 0; i < sbuf.st_size / chunk_size + 1; i++) {
			snprintf(hash, EVP_MAX_MD_SIZE, "murali%d", i);
			printf("Before Block %d has a hash of ", i);
			print(hash, CAPFS_MAXHASHLENGTH);
			gettimeofday(&begin, NULL);
			hcache_put(argv[1], i, 1, hash);
			gettimeofday(&end, NULL);
			printf("Iteration %d of block_put() %d took %g msec\n", j+1, i+1, diff(&end, &begin));

			memset(hash, 0, EVP_MAX_MD_SIZE);

			gettimeofday(&begin, NULL);
			hcache_get(argv[1], i, 1, -1, hash);
			gettimeofday(&end, NULL);
			printf("Iteration %d of block_get() %d took %g msec\n", j+1, i+1, diff(&end, &begin));
			printf("After Block %d has a hash of ", i);
			print(hash, CAPFS_MAXHASHLENGTH);
			//print(hash + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH);
		}
		hcache_clear(argv[1]);
	}
	free(hash);
	*/
	func(argv[1]);
	hcache_finalize();
	return 0;
}
