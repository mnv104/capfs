#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

typedef unsigned long long uint64_t;
typedef unsigned int			uint32_t;

struct capfs_stat {
    uint64_t st_size;
    uint64_t st_ino;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad;    /* round up to 64-bit fields */
    /* unused standard stat fields: dev, nlink, rdev, blksize, blocks */
};

struct capfs_filestat {
	int32_t base;
	int32_t pcount;
	int32_t ssize;
	int32_t __pad;  /* round up to 64-bit fields */
};

typedef struct fmeta fmeta, *fmeta_p;

/* this is the structure we're actually using */
struct fmeta {
	int64_t fs_ino; /* file system root directory inode # */
	struct capfs_stat u_stat;
	struct capfs_filestat p_stat;
	struct sockaddr mgr;
};


int main(int argc, char *argv[])
{
	int fd;
	struct stat sbuf;
	struct fmeta meta;

	if (argc != 2)
	{
		printf("Usage: %s <meta-file-name>\n", argv[0]);
		exit(1);
	}
	if (stat(argv[1], &sbuf) < 0 || sbuf.st_size != sizeof(struct fmeta))
	{
		perror("stat:");
		exit(1);
	}
	fd = open(argv[1], O_RDONLY);
	if (fd < 0)
	{
		perror("open:");
		exit(1);
	}
	read(fd, &meta, sizeof(struct fmeta));
	close(fd);
	printf("*** CAPFS Meta-file DUMP ***\n");
	printf("Root FS inode #: %Lu\n", meta.fs_ino);
	printf("Size: %Lu\n", meta.u_stat.st_size);
	printf("Inode Number: %Lu\n", meta.u_stat.st_ino);
	printf("Atime: %Lu\n", meta.u_stat.atime);
	printf("Mtime: %Lu\n", meta.u_stat.mtime);
	printf("Ctime: %Lu\n", meta.u_stat.ctime);
	printf("Mode: %u\n", meta.u_stat.st_mode);
	printf("UID: %u\n", meta.u_stat.st_uid);
	printf("GID: %u\n", meta.u_stat.st_gid);
	printf("base: %u pcount: %u ssize: %u\n", meta.p_stat.base, meta.p_stat.pcount, meta.p_stat.ssize);
	printf("*** END ***\n");
	return 0;
}

