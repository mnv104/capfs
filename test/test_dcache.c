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
#include "dcache.h"
#include "sha.h"
#include <openssl/evp.h>

#define BSIZE 4096

struct recipe {
	int count;
	unsigned char **hashes;
	size_t  *hash_lengths;
};

struct recipe* get_recipe_list(char *filename, int chunk_size, char **ptr)
{
	struct stat statbuf;
	struct recipe *recipe = NULL;
	int i, fd;
	void *file_addr;
	size_t size = 0;

	if (stat(filename, &statbuf) < 0) {
		return NULL;
	}
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		return NULL;
	}
	if ((file_addr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		close(fd);
		return NULL;
	}
	*ptr = file_addr;
	recipe = (struct recipe *) calloc(1, sizeof(struct recipe));
	if (recipe == NULL) {
		munmap(file_addr, statbuf.st_size);
		close(fd);
		return NULL;
	}
	recipe->count = statbuf.st_size / chunk_size + 1;
	recipe->hashes = (unsigned char **) calloc(recipe->count, sizeof(unsigned char *));
	if (recipe->hashes == NULL) {
		free(recipe);
		munmap(file_addr, statbuf.st_size);
		close(fd);
		return NULL;
	}
	recipe->hash_lengths = (size_t *) calloc (recipe->count, sizeof(size_t));
	if (recipe->hash_lengths == NULL) {
		free(recipe->hashes);
		free(recipe);
		munmap(file_addr, statbuf.st_size);
		close(fd);
		return NULL;
	}
	for (i = 0; i < recipe->count; i++) {
		int ret;
		size_t input_length = 0;

		if (size + chunk_size <= statbuf.st_size) {
			input_length = chunk_size;
		}
		else {
			input_length = statbuf.st_size - size;
		}
		if ((ret = sha1((char *)file_addr + i * chunk_size, input_length,
						&recipe->hashes[i], &recipe->hash_lengths[i])) < 0) {
			break;
		}
		size += input_length;
	}
	if (i != recipe->count) {
		free(recipe->hash_lengths);
		free(recipe->hashes);
		free(recipe);
		munmap(file_addr, statbuf.st_size);
		close(fd);
		return NULL;
	}
	close(fd);
	return recipe;
}

#define diff(p2, p1) (((p2)->tv_sec - (p1)->tv_sec) * 1e03 + ((p2)->tv_usec - (p1)->tv_usec) * 1e-03)

int main(int argc, char *argv[])
{
	int i, block_size = BSIZE;
	struct recipe *recipe = NULL;
	char *ptr= NULL;
	char str[256];
	struct stat sbuf;
	size_t count = 0;

	if (argc != 2 && argc != 3) {
		fprintf(stderr, "usage: %s <filename> {block size}\n", argv[0]);
		return 1;
	}
	if (argc == 3) {
		block_size = atoi(argv[2]);
	}
	stat(argv[1], &sbuf);
	sprintf(str,"%ld", sbuf.st_size / 10);
	setenv("CMGR_BCOUNT", str, 1);
	dcache_init();
	recipe = get_recipe_list(argv[1], block_size, &ptr);
	for (i = 0; i < recipe->count; i++) {
		if (count + block_size < sbuf.st_size) {
			dcache_put(recipe->hashes[i], ptr + i * block_size, block_size);
		}
		else {
			dcache_put(recipe->hashes[i], ptr + i * block_size, sbuf.st_size - count);
		}
		count += block_size;
	}
	ptr = (void *) calloc(1, block_size);
	for (i = 0; i < recipe->count; i++) {
		dcache_get(recipe->hashes[i], ptr, block_size);
		printf("%s", ptr);
	}
	printf("\n");
	dcache_finalize();
	return 0;
}
