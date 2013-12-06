#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include "mpi.h"

#define CAPFS_FILE "/mnt/capfs/tst1"

int shared_fd = 0;

static void print(char *buf, int size)
{
	int i;
	for (i = 0; i < size; i++) {
		printf("%c", *(buf + i));
	}
	printf("\n");
	return;
}

static void func(int rank, int np)
{
	char *buf= (char *) calloc(1, 65536);
	int count = 0, i, offset;
	offset = rand() % 10;
	count = sprintf(buf + offset, "Murali_Rank_%d_Np_%d_", rank, np);
	for (i = 0; i < 10; i++) {
		int number = rand() % 100 + 1;
		count += sprintf(buf + count, "%d_", number);
	}
	sprintf(buf + count, "\n");
	printf("Node %d is writing ", rank);
	print(buf, 65536);
	MPI_Barrier(MPI_COMM_WORLD);
	printf("%d\n", write(shared_fd, buf, 65536));
	return;
}

int main(int argc, char *argv[])
{
	int rank, size;
	
	srand(time(NULL));
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	shared_fd = open(CAPFS_FILE, O_RDWR | O_CREAT, 0700);
	if (shared_fd < 0) {
		fprintf(stderr, "%s\n", strerror(errno));
		exit(1);
	}
	func(rank, size);
	close(shared_fd);
	MPI_Finalize();
	return 0;
}
