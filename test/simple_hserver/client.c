#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <assert.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "capfs_config.h"
#include "log.h"
#include "sockio.h"
#include "tp_proto.h"
#include "sha.h"
#include "req.h"

#define M_PORT   7001
#define D_PORT   7002
#define TIMEOUT  50
#define DEFAULT_THREADS 5

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cv    = PTHREAD_COND_INITIALIZER;
static int count = 0;

static void signal_termination(int force)
{
	pthread_mutex_lock(&mutex);
	if (force == 1 || count == 1) {
		pthread_cond_signal(&cv);
	}
	count--;
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

static void print(unsigned char *hash, int hash_length)
{
	int i;

	for (i = 0; i < hash_length; i++) {
		printf("%02x", hash[i]);
	}
	printf("\n");
	return;
}

static void my_shutdown(int sig)
{
	signal_termination(1);
	return;
}

static int setup_connection(void)
{
	int fd;
	struct sockaddr_in toaddr;
	socklen_t tolen = sizeof(toaddr);

	fd = new_sock();
	if (fd < 0) {
		return -1;
	}
	memset(&toaddr, 0, sizeof(toaddr));
	toaddr.sin_family = AF_INET;
	toaddr.sin_port = htons(M_PORT);
	inet_aton("192.168.2.97", &toaddr.sin_addr);
	if (connect_timeout(fd, (struct sockaddr* )&toaddr, tolen, TIMEOUT) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void* do_work(void *args)
{
	int i, j, fd;
	DIR *dir = NULL;
	int count = 0;
	char **file_names = NULL;
	struct dirent *entry = NULL;

	dir = opendir("/home/vilayann/misc");
	do {
		entry = readdir(dir);
		if (entry) {
			count++;
			file_names = (char **) realloc(file_names, count * sizeof(char *));
			file_names[count - 1] = strdup(entry->d_name);
		}
	} while (entry != NULL);
	closedir(dir);

	srand(time(NULL));
	for (i = 0; i < 100; i++) {
		mreq req;
		mack ack;

		if ((fd = setup_connection()) < 0) {
			printf("Connection errors: %s\n", strerror(errno));
			break;
		}
		j = rand() % count;
		snprintf(req.fname, 256, "/home/vilayann/misc/%s", file_names[j]);
		bsend(fd, &req, sizeof(req));
		if (brecv_timeout(fd, &ack, sizeof(ack), TIMEOUT) < 0) {
			if (errno == EPIPE) {
				printf("Socket %d closed\n", fd);
			}
			else {
				printf("Socket %d timed out\n", fd);
			}
		}
		else {
			if (ack.status != 0) {
				printf("Request for recipes of %s had errors : %s\n", req.fname, strerror(ack.status));
			}
			else if (ack.trailer >= 0) {
				char *trailer = (char *) calloc(ack.trailer, sizeof(char));
				size_t size;

				assert(ack.num_hash * CAPFS_MAXHASHLENGTH == ack.trailer);
				assert(trailer);
				if ((size = brecv(fd, trailer, ack.trailer)) < 0) {
					if (errno == EPIPE) {
						printf("Socket %d closed\n", fd);
					}
					else {
						printf("Socket %d timed out\n", fd);
					}
				}
				else {
					printf("File %s has %d entries in recipe\n", req.fname, ack.num_hash);
					for (j = 0; j < ack.num_hash; j++) {
						printf("Entry %d)->", j);
						print(trailer + j * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH);
					}
				}
				free(trailer);
			}
		}
		close(fd);
	}
	signal_termination(0);
	return NULL;
}

int main(int argc, char *argv[])
{
	int num_threads = DEFAULT_THREADS;
	tp_info info;
	tp_id   id;
	int i;

	if (argc == 2) {
		num_threads = atoi(argv[1]);
	}
	signal(SIGTERM, (void *) my_shutdown);
	signal(SIGINT, (void *) my_shutdown);
	signal(SIGPIPE, (void *) my_shutdown);
	signal(SIGSEGV, (void *) my_shutdown);
	info.tpi_name = NULL;
	count = info.tpi_count = num_threads;
	id = tp_init(&info);
	for (i = 0; i < num_threads; i++) {
		tp_assign_work_by_id(id, do_work, NULL);
	}
	wait_for_termination();
	tp_cleanup_by_id(id);
	return 0;
}
