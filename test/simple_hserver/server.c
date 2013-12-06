#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <assert.h>
#include "capfs_config.h"
#include "log.h"
#include "sockio.h"
#include "tp_proto.h"
#include "sha.h"
#include "req.h"

#define M_SERVER 1
#define D_SERVER 2
#define M_PORT   7001
#define D_PORT   7002
#define DEFAULT_THREADS 5
#define TIMEOUT  21
#define BSIZE    4096

static int role = -1;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cv    = PTHREAD_COND_INITIALIZER;

static void signal_termination(void)
{
	pthread_mutex_lock(&mutex);
	pthread_cond_signal(&cv);
	pthread_mutex_unlock(&mutex);
	return;
}

static void wait_for_termination(void)
{
	pthread_mutex_lock(&mutex);
	pthread_cond_wait(&cv, &mutex);
	pthread_mutex_unlock(&mutex);
	return;
}

static void my_shutdown(int sig)
{
	signal_termination();
}

static int setup(unsigned short port)
{
	int fd;

	if ((fd = new_sock()) < 0) {
		PERROR(SUBSYS_NONE,"capfsmgr: new_sock");
		return(-1);
	}
	set_sockopt(fd, SO_REUSEADDR, 1);
	if (bind_sock(fd, port) < 0) {
		PERROR(SUBSYS_NONE,"capfsmgr: bind_sock");
		return(-1);
	}
	if (listen(fd, 5) != 0) {
		PERROR(SUBSYS_NONE,"capfsmgr: listen");
		return(-1);
	}
	signal(SIGTERM, (void *) my_shutdown);
	signal(SIGINT, (void *) my_shutdown);
	signal(SIGPIPE, (void *) my_shutdown);
	signal(SIGSEGV, (void *) my_shutdown);
	return fd;
}

struct recipe {
	int count;
	unsigned char **hashes;
	size_t  *hash_lengths;
};

void destroy_recipe(struct recipe *recipe)
{
	int i;
	free(recipe->hash_lengths);
	for (i = 0; i < recipe->count; i++) {
		free(recipe->hashes[i]);
	}
	free(recipe->hashes);
	free(recipe);
	return;
}

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
	if (S_ISDIR(statbuf.st_mode)) {
		*error = EISDIR;
		return NULL;
	}
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		*error = errno;
		return NULL;
	}
	if ((file_addr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		close(fd);
		*error = errno;
		return NULL;
	}
	recipe = (struct recipe *) calloc(1, sizeof(struct recipe));
	if (recipe == NULL) {
		munmap(file_addr, statbuf.st_size);
		close(fd);
		*error = ENOMEM;
		return NULL;
	}
	recipe->count = statbuf.st_size / chunk_size + 1;
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
		destroy_recipe(recipe);
		munmap(file_addr, statbuf.st_size);
		close(fd);
		return NULL;
	}
	munmap(file_addr, statbuf.st_size);
	close(fd);
	return recipe;
}

static void print(unsigned char *hash, int hash_length) __attribute__((unused));
static void print(unsigned char *hash, int hash_length)
{
	int i;

	for (i = 0; i < hash_length; i++) {
		printf("%02x", hash[i]);
	}
	printf("\n");
	return;
}

static void* mworker(void *args)
{
	int sock = (int) args, error = 0, i;
	mreq req;
	mack ack;
	struct recipe *recipe = NULL;

	memset(&ack, 0, sizeof(ack));
	if (brecv_timeout(sock, &req, sizeof(req), TIMEOUT) < 0) {
		if (errno == EPIPE) {
			printf("socket %d closed\n", sock);
		}
		else {
			printf("socket %d timeout\n", sock);
		}
		close(sock);
		return NULL;
	}
	recipe = get_recipe_list(req.fname, BSIZE, &error);
	if (recipe == NULL) {
		printf("Could not get recipe list for file %s\n", req.fname);
		ack.status = error;
		ack.num_hash = 0;
		ack.trailer = 0;
		bsend(sock, &ack, sizeof(ack));
		close(sock);
		return NULL;
	}
	ack.status = 0;
	ack.num_hash = recipe->count;
	ack.trailer = recipe->count * recipe->hash_lengths[0];
	bsend(sock, &ack, sizeof(ack));
	for (i = 0; i < recipe->count; i++) {
		/*
		printf("Entry %d)->", i);
		print(recipe->hashes[i], recipe->hash_lengths[i]);
		*/
		bsend(sock, recipe->hashes[i], recipe->hash_lengths[i]);
	}
	close(sock);
	destroy_recipe(recipe);
	return NULL;
}

static void *mserver(void *args)
{
	int sock;
	tp_id id = (tp_id) args;

	sock = setup(M_PORT);
	if (sock < 0) {
		PERROR(SUBSYS_NONE,"Could not open socket\n");
		signal_termination();
		return NULL;
	}
	while (1) {
		int new_sock;
		struct sockaddr_in fromaddr;
		socklen_t fromlen = sizeof(fromaddr);

		new_sock = accept(sock, (struct sockaddr *)&fromaddr, &fromlen);
		if (new_sock < 0) {
			PERROR(SUBSYS_NONE,"Error in accept!\n");
			continue;
		}
		tp_assign_work_by_id(id, mworker, (void *)new_sock);
	}
	return NULL;
}

static void *dworker(void *args)
{
	iack ack;

	memset(&ack, 0, sizeof(ack));
	return NULL;
}

static void *dserver(void *args)
{
	int sock;
	tp_id id = (tp_id) args;

	sock = setup(D_PORT);
	if (sock < 0) {
		PERROR(SUBSYS_NONE,"Could not open socket\n");
		signal_termination();
		return NULL;
	}
	while (1) {
		int new_sock;
		struct sockaddr_in fromaddr;
		socklen_t fromlen = sizeof(fromaddr);

		new_sock = accept(sock, (struct sockaddr *)&fromaddr, &fromlen);
		if (new_sock < 0) {
			PERROR(SUBSYS_NONE,"Error in accept!\n");
			continue;
		}
		tp_assign_work_by_id(id, dworker, (void *)new_sock);
	}
	return NULL;
}

static void setup_server(tp_id id)
{
	if (role == M_SERVER) {
		tp_assign_work_by_id(id, mserver, (void *)id);
	}
	else {
		tp_assign_work_by_id(id, dserver, (void *)id);
	}
	return;
}

static void usage(char *str)
{
	fprintf(stderr, "Usage: %s -m {for meta-data service} -d {for data service} -t <number of threads>\n", str);
	return;
}

int main(int argc, char *argv[])
{
	char c;
	tp_info info;
	tp_id   id;

	info.tpi_name = NULL;
	info.tpi_count = -1;
	while ((c = getopt(argc, argv, "mdt:h")) != EOF) {
		switch(c) {
			case 'm':
				role = M_SERVER;
				break;
			case 'd':
				role = D_SERVER;
				break;
			case 't':
				info.tpi_count = atoi(optarg);
				break;
			case 'h':
			case '?':
			default:
				usage(argv[0]);
				exit(1);
		}
	}
	if (role != M_SERVER && role != D_SERVER) {
		fprintf(stderr, "Either of -m or -d must be specified\n");
		usage(argv[0]);
		exit(1);
	}
	if (info.tpi_count < 0) {
		info.tpi_count = DEFAULT_THREADS;
	}
	sha1_init();
	id = tp_init(&info);
	if (id < 0) {
		printf("Could not initialize thread pool\n");
		exit(1);
	}
	setup_server(id);
	wait_for_termination();
	tp_cleanup_by_id(id);
	return 0;
}
