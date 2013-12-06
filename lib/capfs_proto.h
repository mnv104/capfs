/*
 * (C) 2005 Penn State University 
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#ifndef CAPFS_PROTO_H
#define CAPFS_PROTO_H

#ifdef __cplusplus
extern "C" {
#endif


int capfs_access(char* pathname, int mode);
int capfs_chmod(const char* pathname, mode_t mode);
int capfs_chown(const char* pathname, uid_t owner, gid_t group);
int capfs_close(int fd);
int capfs_dup(int fd);
int capfs_dup2(int old_fd, int new_fd);
int capfs_fchmod(int fd, mode_t mode);
int capfs_fchown(int fd, uid_t owner, gid_t group);
int capfs_fcntl(int fd, int cmd, long arg);
int capfs_fdatasync(int fd);
int capfs_flock(int fd, int operation);
int capfs_fstat(int fd, struct stat *buf);
#if defined _LARGEFILE64_SOURCE
int capfs_fstat64(int fd, struct stat64 *buf);
#endif
int capfs_fsync(int fd);
int capfs_ftruncate(int fd, size_t length);
int capfs_ftruncate64(int fd, int64_t length);
int capfs_ioctl(int fd, int cmd, void *data);
int capfs_lseek(int fd, int off, int whence);
int64_t capfs_lseek64(int fd, int64_t off, int whence);
int64_t capfs_llseek(int fd, int64_t off, int whence);
int capfs_lstat(char* pathname, struct stat *buf);
int capfs_mkdir(const char* pathname, int mode);
int capfs_munmap(void *start, size_t length);
int capfs_open(const char* pathname, int flag, ...);
int capfs_open64(const char* pathname, int flag, ...);
int capfs_creat(const char* pathname, mode_t mode);
int capfs_ostat(char* pathname, struct stat *buf);
int capfs_stat(char* pathname, struct stat *buf);
#if defined _LARGEFILE64_SOURCE
int capfs_ostat64(char* pathname, struct stat64 *buf);
int capfs_stat64(char* pathname, struct stat64 *buf);
#endif
int capfs_read(int fd, char *buf, size_t count);
int capfs_read_list(int     fd,
		   int     mem_list_count,
		   char   *mem_offsets[],
		   int     mem_lengths[],
		   int     file_list_count,
		   int64_t file_offsets[],
		   int32_t file_lengths[]);
int capfs_readv(int fd, const struct iovec *vector, size_t count);
int capfs_rmdir(const char* pathname);
int capfs_truncate(const char *path, size_t length);
int capfs_truncate64(const char *path, int64_t length);
int capfs_unlink(const char* pathname);
int capfs_utime(const char *filename, struct utimbuf *buf);
int capfs_write(int fd, char *buf, size_t count);
int capfs_write_list(int     fd,
		    int     mem_list_count,
		    char   *mem_offsets[],
		    int    *mem_lengths,
		    int     file_list_count,
		    int64_t file_offsets[],
		    int32_t file_lengths[]);
int capfs_writev(int fd, const struct iovec *vector, size_t count);
int capfs_symlink(const char* target_name, const char *link_name);
int capfs_link(const char* target_name, const char *link_name);
int capfs_readlink(const char *path, char *buf, size_t bufsiz);
int capfs_gethashes(const char *path, unsigned char *phashes, int64_t begin_offset, int max_hashes);

#ifdef __cplusplus
}
#endif

#endif

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 



