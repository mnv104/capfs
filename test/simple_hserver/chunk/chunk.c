#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "sha.h"

#define BSIZE 4096

struct recipe {
	int count;
	unsigned char **hashes;
	size_t  *hash_lengths;
};

struct recipe* get_recipe_list(char *filename, int chunk_size, int *error)
{
	struct stat statbuf;
	struct recipe *recipe = NULL;
	int i, fd;
	void *file_addr;
	size_t size = 0;

	if (stat(filename, &statbuf) < 0) {
		*error = errno;
		return NULL;
	}
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		*error = errno;
		return NULL;
	}
	if ((file_addr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		close(fd);
		*error = ENOMEM;
		return NULL;
	}
	recipe = (struct recipe *) calloc(1, sizeof(struct recipe));
	if (recipe == NULL) {
		munmap(file_addr, statbuf.st_size);
		close(fd);
		*error = ENOMEM;
		return NULL;
	}
	recipe->count = statbuf.st_size / chunk_size;
	if (statbuf.st_size % chunk_size != 0) {
		recipe->count++;
	}
	recipe->hashes = (unsigned char **) calloc(recipe->count, sizeof(unsigned char *));
	if (recipe->hashes == NULL) {
		free(recipe);
		munmap(file_addr, statbuf.st_size);
		close(fd);
		*error = ENOMEM;
		return NULL;
	}
	recipe->hash_lengths = (size_t *) calloc (recipe->count, sizeof(size_t));
	if (recipe->hash_lengths == NULL) {
		free(recipe->hashes);
		free(recipe);
		munmap(file_addr, statbuf.st_size);
		close(fd);
		*error = ENOMEM;
		return NULL;
	}
	*error = 0;
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
			*error = -ret;
			break;
		}
		size += input_length;
	}
	if (*error < 0) {
		free(recipe->hash_lengths);
		free(recipe->hashes);
		free(recipe);
		munmap(file_addr, statbuf.st_size);
		close(fd);
		return NULL;
	}
	munmap(file_addr, statbuf.st_size);
	close(fd);
	return recipe;
}

void print(unsigned char *hash, int hash_length)
{
	int i;

	for (i = 0; i < hash_length; i++) {
		printf("%02x", hash[i]);
	}
	return;
}

int main(int argc, char *argv[])
{
	struct recipe *recipe = NULL;
	int error = 0, chunk_size = BSIZE;

	if (argc != 2 && argc != 3) {
		fprintf(stderr, "usage: %s <filename> {chunk size}\n", argv[0]);
		return 1;
	}
	if (argc == 3) {
		chunk_size = atoi(argv[2]);
	}
	recipe = get_recipe_list(argv[1], chunk_size, &error);
	if (error == 0) {
		int i;

		for (i = 0; i < recipe->count; i++) {
			printf("Entry %d) -> ", i);
			print(recipe->hashes[i], recipe->hash_lengths[i]);
			printf("\n");
		}
	}
	return error;
}
