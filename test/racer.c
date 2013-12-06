#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#define NUM_THREADS 2

#define CAPFS_FILE "/mnt/capfs/tst1"

int shared_fd = 0;

static void *func(void *unused)
{
	char *buf= (char *) malloc(100);
	sprintf(buf, "Murali%ld\n", pthread_self());
	printf("%d\n", write(shared_fd, buf, 100));
	pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
	int i;
	pthread_t t[NUM_THREADS];

	shared_fd = open(CAPFS_FILE, O_RDWR | O_CREAT, 0700);
	if (shared_fd < 0) {
		fprintf(stderr, "%s\n", strerror(errno));
		exit(1);
	}
	for (i = 0; i < NUM_THREADS; i++) {
		pthread_create(&t[i], NULL, func, NULL);
	}
	for (i = 0; i < NUM_THREADS; i++) {
		pthread_join(t[i], NULL);
	}
	close(shared_fd);
	return 0;
}
