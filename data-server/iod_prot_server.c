/*
 * Copyright (C) 2005 Murali Vilayannur (vilayann@cse.psu.edu)
 * Implements a simple content addressable data server (CAS server)
 */
#include "iod_prot.h"
#include "capfs_iod.h"
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/statfs.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <linux/unistd.h>
#include <errno.h>
#include <sys/select.h>
#include <capfs_config.h>
#include "list.h"
#include "log.h"
#include "sha.h"
#include "cas.h"
#include "sockio.h"

#define ERR_MAX 256

static char *zero_chunk = NULL;

int compare_to_zero(char *hash, int hash_len)
{
	static char *zero_hash = NULL;

	if (zero_hash == NULL) {
		zero_hash = (char *) calloc(sizeof(char), hash_len);
		if (zero_hash == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "could not allocate memory\n");
			return -ENOMEM;
		}
		zero_chunk = (char *) calloc(sizeof(char), CAPFS_CHUNK_SIZE);
		if (zero_chunk == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "could not allocate memory\n");
			free(zero_hash);
			return -ENOMEM;
		}
	}
	return memcmp(hash, zero_hash, hash_len);
}

static void opstatus_dtor(op_status *status)
{
	free(status->op_status_val);
	status->op_status_len = 0;
	status->op_status_val = NULL;
	return;
}

static int opstatus_ctor(op_status *status, int len)
{
	status->op_status_len = len;
	status->op_status_val = (int *) calloc(status->op_status_len, sizeof(int));
	/* No memory */
	if (status->op_status_val == NULL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "could not allocate memory\n");
		opstatus_dtor(status);
		return -ENOMEM;
	}
	return 0;
}

static void blocks_dtor(data_blocks *blocks, int i)
{
	int j;

	for (j = 0; j < i; j++) {
		free(blocks->data_blocks_val[j].data_val);
		blocks->data_blocks_val[j].data_len = 0;
		blocks->data_blocks_val[j].data_val = NULL;
	}
	free(blocks->data_blocks_val);
	blocks->data_blocks_val = NULL;
	blocks->data_blocks_len = 0;
	return;
}

static int blocks_ctor(data_blocks *blocks, int len)
{
	int i;

	blocks->data_blocks_len = len;
	blocks->data_blocks_val = (data *) calloc(blocks->data_blocks_len, sizeof(data));
	if (blocks->data_blocks_val == NULL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "could not allocate memory\n");
		return -ENOMEM;
	}
	for (i = 0; i < len; i++) {
		blocks->data_blocks_val[i].data_len = CAPFS_CHUNK_SIZE;
		blocks->data_blocks_val[i].data_val = (char *) calloc(CAPFS_CHUNK_SIZE, sizeof(char));
		if (blocks->data_blocks_val[i].data_val == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "could not allocate memory\n");
			break;
		}
	}
	if (i != len) {
		blocks_dtor(blocks, i);
		return -ENOMEM;
	}
	return 0;
}

static int64_t time_diff(struct timeval *end, struct timeval *begin)
{
	int64_t difference = 0;
	if (end->tv_usec < begin->tv_usec) {
		end->tv_usec += 1000000;
		end->tv_sec--;
	}
	end->tv_sec -= begin->tv_sec;
	end->tv_usec -= begin->tv_usec;
	difference = (end->tv_sec * 1000000) + end->tv_usec;
	return difference;
}

bool_t
capfs_get_1_svc(get_req arg1, get_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	int i;
	char *fileName = NULL;
	struct stat fileInfo;
	struct timeval begin, end;

	gettimeofday(&begin, NULL);
	if (opstatus_ctor(&result->status, arg1.h.get_hashes_len) < 0) {
		return retval;
	}
	if (blocks_ctor(&result->blocks, arg1.h.get_hashes_len) < 0) {
		opstatus_dtor(&result->status);
		return retval;
	}
	
	for (i = 0; i < arg1.h.get_hashes_len; i++) {

		fileName = get_fileName(arg1.h.get_hashes_val[i]);
		/* if all the hashes are zeroes!, then we are sure that this is a sparse block */
		if (compare_to_zero(arg1.h.get_hashes_val[i], CAPFS_MAXHASHLENGTH) == 0) {
			/* just continue */
			result->status.op_status_val[i] = 0;
			continue;
		}
#ifdef DEBUG
		{
			char str[256];
			hash2str(arg1.h.get_hashes_val[i], CAPFS_MAXHASHLENGTH, str);
			LOG(stderr, DEBUG_MSG, SUBSYS_DATA, "GET file name : %s : %s\n", fileName, str);
		}
#endif

		if (stat(fileName, &fileInfo) < 0) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "access failed for %s\n", fileName);
			result->status.op_status_val[i] = -errno;
		}
		else {
			int fd;

			result->blocks.data_blocks_val[i].data_len = fileInfo.st_size;
			fd = open(fileName, O_RDONLY);
			if (fd < 0) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,"Could not open file %s\n", fileName);
				result->status.op_status_val[i] = -errno;
			}
			else {
				read(fd, result->blocks.data_blocks_val[i].data_val, fileInfo.st_size);
				result->status.op_status_val[i] = 0;
			}
			close(fd);
		}
		free(fileName);
	}
	gettimeofday(&end, NULL);
	result->get_time = time_diff(&end, &begin);
	return retval;	
}

bool_t
capfs_put_1_svc(put_req arg1, put_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1; 
	int i;
	struct timeval begin, end;

	gettimeofday(&begin, NULL);
	if (opstatus_ctor(&result->status, arg1.h.put_hashes_len) < 0) {
		return retval;
	}
	if (arg1.h.put_hashes_len != arg1.blocks.data_blocks_len) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Number of hashes [%d] != number of data blocks [%d]\n", 
				arg1.h.put_hashes_len, arg1.blocks.data_blocks_len);
		opstatus_dtor(&result->status);
		return retval;
	}
	result->bytes_done = 0;
	for (i = 0; i < arg1.h.put_hashes_len; i++) {
		int fd;
		char *fileName = NULL;

		fileName = get_fileName(arg1.h.put_hashes_val[i]);
#ifdef DEBUG 
		{
			char str[256];
			hash2str(arg1.h.put_hashes_val[i], CAPFS_MAXHASHLENGTH, str);
			LOG(stderr, DEBUG_MSG, SUBSYS_DATA, "PUT file name : %s : %s\n", fileName, str);
		}
#endif

		fd = open(fileName, O_RDWR | O_CREAT, 0700);
		if (fd < 0) {
			result->status.op_status_val[i] = -errno;
		}
		else {
			ssize_t wsize;

			//flock(fd, LOCK_EX);
			if ((wsize = write(fd, arg1.blocks.data_blocks_val[i].data_val, 
							arg1.blocks.data_blocks_val[i].data_len)) < 0) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "capfs_put: write operation on (%s,%s) error %d\n", 
						arg1.h.put_hashes_val[i], fileName, -errno);
				result->status.op_status_val[i] = -errno;
			}
			else {
				result->bytes_done += wsize;
				result->status.op_status_val[i] = wsize;
			}
			//flock(fd, LOCK_UN);
		}
		close(fd);
		free(fileName);
	}
	gettimeofday(&end, NULL);
	result->put_time = time_diff(&end, &begin);
	return retval;
}

bool_t
capfs_dstatfs_1_svc(cas_stat_resp *result, struct svc_req *rqstp)
{
	bool_t retval = 1;
	int ret;
	struct statfs sfs;

	ret = statfs(".", &sfs);
	if (ret < 0) {
		result->status = -errno;
	}
	else {
		result->status = 0;
		result->sfs.f_type = sfs.f_type;
		result->sfs.f_bsize = sfs.f_bsize;
		result->sfs.f_blocks = (uint64_t) sfs.f_blocks;
		result->sfs.f_bfree = (uint64_t) sfs.f_bfree;
		result->sfs.f_bavail =(uint64_t) sfs.f_bavail;
		result->sfs.f_files = (uint64_t) sfs.f_files;
		result->sfs.f_ffree = (uint64_t) sfs.f_ffree;
		memcpy(&result->sfs.f_fsid, &sfs.f_fsid, sizeof(uint64_t));
		result->sfs.f_namelen = sfs.f_namelen;
	}
	return retval;
}


/* Internal routines to traverse an iod data directory */

/* glibc does not seem to provide a getdents() routine, so we provide one */
_syscall3(int, getdents, uint, fd, struct dirent *, dirp, uint, count);

/*
 * NOTES: Since we want BREADTH-FIRST TRAVERSAL, we build a link list(queue).
 * If we wanted DEPTH-FIRST TRAVERSAL, then we need to build a stack here, i.e
 * we need to use list_add() function instead of list_add_tail()
 */

/* Information about each file in the hierarchy */
struct files {
	/* Full path name of the file */
	char 				  					name[PATH_MAX];
	/* The level field is used to link struct files both in the tree and in the flist */
	struct list_head 					level; 
};

static void dealloc_treelist(struct list_head *TREE)
{
	while (TREE->next != TREE) {
		struct files *filp;

		filp = list_entry(TREE->next, struct files, level);
		list_del(TREE->next);
		free(filp);
	}
	return;
}

static int path_init(struct list_head *TREE, const char *path)
{
	struct files *filp = NULL;

	filp = (struct files *)calloc(1, sizeof(struct files));
	if (!filp) {
		PERROR(SUBSYS_DATA, "do_root:calloc");
		return -1;
	}
	snprintf(filp->name, NAME_MAX + 1, "%s", path);
	/* add it to the tree of to-be visited nodes */
	list_add_tail(&filp->level, TREE);
	return 0;
}

static int path_walk(struct list_head *TREE, struct files *root_filp, int *invoke_count)
{
	int dir_fd, ret;
	struct files *filp;
	struct stat statbuf;

	ret = 0;
	memset(&statbuf, 0, sizeof(statbuf));
	/* Dequeue from the list of to-be-visited nodes */
	list_del(&root_filp->level);
	dir_fd = open(root_filp->name, O_RDONLY | O_NOFOLLOW);
	if (dir_fd < 0) {
		/* lets just stat the file here, it might be some sort of stale or special file or symbolic link */
		lstat(root_filp->name, &statbuf);
	}
	else {
		fstat(dir_fd, &statbuf);
	}
	/* Are we looking at a directory? */
	if (S_ISDIR(statbuf.st_mode)) {
		/* We do not delete the directory, but only the files underneath and that too only if we there is a file
		 * called .capfsiod file 
		 */
		off_t off = 0;
		struct dirent p;
		char buf[PATH_MAX];
		char err[ERR_MAX];

		snprintf(buf, PATH_MAX, "%s/.capfsiod", root_filp->name);
		/* Skip this directory, if there is no file called .capfsiod */
		if (stat(buf, &statbuf) < 0) {
			snprintf(err, ERR_MAX, "%s is not a valid CAPFS directory. Skipping...\n", root_filp->name);
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "%s", err);
			if (*invoke_count == 0) {
				errno = EINVAL;
				ret = -1;
			}
			else {
				ret = 0;
			}
			goto done;
		}
		/* get the contents of the directory */
		do {

			if ((ret = lseek(dir_fd, off, SEEK_SET)) < 0) {
				snprintf(err, ERR_MAX, "lseek(%d): %s", dir_fd, strerror(errno));
				PERROR(SUBSYS_DATA, err);
				break;
			}
			if ((ret = getdents(dir_fd, &p, sizeof(p))) <= 0) {
				if (ret < 0) {
					snprintf(err, ERR_MAX, "getdents(%d): %s", dir_fd, strerror(errno));
					PERROR(SUBSYS_DATA, err);
				}
				break;
			}
			/* ignore . and ..  and .capfsiod */
			else if (strcmp(p.d_name, ".") && strcmp(p.d_name, "..") && strcmp(p.d_name, ".capfsiod")) {
				filp = (struct files *)calloc(1, sizeof(struct files));
				if (!filp) {
					PERROR(SUBSYS_DATA, "filp:calloc");
					ret = -1;
					break;
				}
				snprintf(filp->name, NAME_MAX + 1, "%s/%s", root_filp->name, p.d_name);
				/* Add to the tree */
				list_add_tail(&filp->level, TREE);
			}
			off = p.d_off;
		} while (1);
	}
	else {
		/* We definitely delete such files */
#ifdef DEBUG
		LOG(stderr, DEBUG_MSG, SUBSYS_DATA, "Unlinking file %s\n", root_filp->name);
#endif
		unlink(root_filp->name);
	}
done:
	if (ret < 0) {
		/* walk through tree and free all the elements */
		int err = errno;
		dealloc_treelist(TREE);
		errno = err;
	}
	free(root_filp);
	if (dir_fd > 0) close(dir_fd);
	return ret;
}

static int do_flatten_hierarchy(char *path)
{
	struct files *filp = NULL;
	int invoke_count = 0;
	struct list_head TREE;

	INIT_LIST_HEAD(&TREE);
	/* traverse the tree and prune out all files and flatten the hierarchy */
	path_init(&TREE, path);
	/* walk the tree */
	while (TREE.next != &TREE) {
		filp = (struct files *)list_entry(TREE.next, struct files, level);
		/* Visit the node */
		if (path_walk(&TREE, filp, &invoke_count) < 0) {
			return -1;
		}
		invoke_count++;
	}
	return 0;
}

bool_t 
capfs_removeall_1_svc(removeall_req arg1, removeall_resp *result, struct svc_req *rqstp)
{
	bool_t retval = 1;
	char *dname = NULL;
	struct stat sbuf;

	/* Remove all the entries rooted at directory "arg1.name" */
	LOG(stderr, DEBUG_MSG, SUBSYS_DATA, "Removeall %s\n", arg1.name);
	dname = arg1.name;
	if (stat(dname, &sbuf) < 0) {
		result->status = -errno;
		return retval;
	}
	result->status = do_flatten_hierarchy(dname) < 0 ? -errno : 0;
	return retval;
}

int
capfs_iod_1_freeresult (SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
	xdr_free (xdr_result, result);

	return 1;
}

extern fd_set global_readsock_set;
extern fd_set aux_readsock_set;
extern pthread_spinlock_t fdset_spinlock;

static inline void fd_lock(void)
{
	pthread_spin_lock(&fdset_spinlock);
}

static inline void fd_unlock(void)
{
	pthread_spin_unlock(&fdset_spinlock);
}


/*
 * Slave thread function that services the actual requests.
 * The protocol here is that this function should
 * a) FD_CLR(sock) from aux_readsock_set in case there was/were *NO* error(s)
 * b) FD_CLR(sock) from aux_readsock_set and global_readsock_set in case of errors.
 */
void *capfs_iod_worker(void *args)
{
	int numHashes, retVal ;
	char *ptr, *fileName, *hashPtr;
	int sock, bytesDone;
	int i, fd, j, totalMessageSize;

	char *get_fileNames[CAPFS_MAXHASHES];
	
	/* reply for a put: how many bytes were written. this will be an int
	*  reply for a get: the first int is how many bytes in reply after this int
	*    first send one int, for size of each file corres to a hash == (header)
	*    then send out the files corres to the hashes themselves
	*/

	/* the first field is the number of bytes read/written
	*  the second int is the error number - 0 for none 
	*/  
	int err;

	struct stat fileInfo;
	cas_request incoming_request;
	cas_reply outgoing_reply_header;
	
	memset(&incoming_request, 0, sizeof(cas_request));
	memset(&outgoing_reply_header, 0, sizeof(cas_reply));
	outgoing_reply_header.requestID = 0;
	outgoing_reply_header.errorCode = GENERIC_ERROR;
	sock = (int) args;
	numHashes = 0;
	bytesDone = 0;

	retVal = brecv(sock, (void*)(&incoming_request.header), sizeof(cas_header));
	if (retVal != sizeof(cas_header))
	{
		LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Blocking recv failed (cas_request). Read %d "
				"instead of %d bytes on sock %d [%s]\n",
				retVal, sizeof(cas_header), sock, strerror(errno));
		outgoing_reply_header.errorCode = BLOCKING_RECV_ERROR;
		blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply));

		/* error path must close socket and return NULL right then and there */
		fd_lock();
		FD_CLR(sock, &global_readsock_set);
		FD_CLR(sock, &aux_readsock_set);
		close(sock);
		fd_unlock();

		LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [bad request header] %d\n", sock);
		return NULL;
	}
	outgoing_reply_header.requestID = incoming_request.header.requestID;
	switch (incoming_request.header.opcode)
	{
		case CAS_STATFS_REQ: 
		{
			outgoing_reply_header.opcode = CAS_STATFS_REPLY;
			retVal = statfs(".", &(outgoing_reply_header.req.cas_statfs.sfs));
			err = errno;
			if (retVal != 0)
				outgoing_reply_header.errorCode = errno;
			else
				outgoing_reply_header.errorCode = NO_ERROR;
			retVal = blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply));
			if (retVal != sizeof(cas_reply))
			{
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "bad blocking (cas_statfs_request send)\n");

				/* error path must close socket and return NULL right then and there */
				fd_lock();
				FD_CLR(sock, &global_readsock_set);
				FD_CLR(sock, &aux_readsock_set);
				close(sock);
				fd_unlock();

				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [bad statfs send reply] %d\n", sock);
				return NULL;
			}
			/* Do not close the socket. This may be reused */
			break;
		}
		case CAS_PING_REQ: 
		{
			outgoing_reply_header.errorCode = NO_ERROR;
			outgoing_reply_header.opcode = CAS_PING_REPLY;
			retVal = blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply));
			if (retVal != sizeof(cas_reply))
			{
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "bad blocking (cas_ping_request send)\n");

				/* error path must close socket and return NULL right then and there */
				fd_lock();
				FD_CLR(sock, &global_readsock_set);
				FD_CLR(sock, &aux_readsock_set);
				close(sock);
				fd_unlock();

				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [bad ping send reply] %d\n", sock);
				return NULL;
			}
			/* Do not close the socket. This may be reused */
			break;
		}
		case CAS_GET_REQ:	
		{
			struct timeval begin, end;

			gettimeofday(&begin, NULL);
			/* If this is a get, read data next*/
			numHashes = incoming_request.header.req.get.numHashes;
			outgoing_reply_header.opcode = CAS_GET_REPLY;
			if (numHashes > CAPFS_MAXHASHES)
			{
				char ch[128];
				sprintf(ch, "Client requested too many cas_get_req hashes simultaneously -- %d instead of %d(MAX)\n",
						numHashes, CAPFS_MAXHASHES);
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "%s", ch);
				outgoing_reply_header.errorCode = TOO_MANY_HASH_OPS;
				blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply));

				/* error path must close socket and return NULL right then and there */
				fd_lock();
				FD_CLR(sock, &global_readsock_set);
				FD_CLR(sock, &aux_readsock_set);
				close(sock);
				fd_unlock();

				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [invalid hashes] %d\n", sock);
				return NULL;
			}
			LOG(stderr, DEBUG_MSG, SUBSYS_DATA, "[get %d] Waiting for %d gethashes\n",
					incoming_request.header.requestID, numHashes);
			/* Now we read the requested number of hashes */
			retVal = brecv(sock, (void*) &incoming_request.req.get.hashes, numHashes * CAPFS_MAXHASHLENGTH); 
			if (retVal != numHashes * CAPFS_MAXHASHLENGTH)
			{
				char ch[256];
				sprintf(ch, "Blocking recv of cas_get [%d]  failed. Recvd %d instead of %d bytes (%d hashes)\n",
						incoming_request.header.requestID, retVal, numHashes * CAPFS_MAXHASHLENGTH, numHashes);
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "%s", ch);
				outgoing_reply_header.errorCode = BLOCKING_RECV_ERROR;
				blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply));

				/* error path must close socket and return NULL right then and there */
				fd_lock();
				FD_CLR(sock, &global_readsock_set);
				FD_CLR(sock, &aux_readsock_set);
				close(sock);
				fd_unlock();

				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [bad blocking recv on gethashes] %d\n", sock);
				return NULL;
			}
			totalMessageSize = 0;
			hashPtr = incoming_request.req.get.hashes;
			for (i = 0; i < numHashes; i++)
			{
				fileName = get_fileName(hashPtr);
				get_fileNames[i] = fileName;
				/* if all the hashes are zeroes!, 
				 * then we are sure that this is a sparse block 
				 */
				if (compare_to_zero(hashPtr, CAPFS_MAXHASHLENGTH) == 0)
				{
					incoming_request.req.get.blockSizes[i] = 0;
					hashPtr += CAPFS_MAXHASHLENGTH;
					totalMessageSize += CAPFS_CHUNK_SIZE;
					continue;
				}
				hashPtr += CAPFS_MAXHASHLENGTH;
				if (stat(fileName, &fileInfo) != 0)
				{
					char ch[128];
					sprintf(ch,"cas_get_req access failed for file %s\n", fileName);
					LOG(stderr, WARNING_MSG, SUBSYS_DATA, "%s", ch);
					outgoing_reply_header.errorCode = FILE_ERROR;
					blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply));

					/* error path must close socket and return NULL right then and there */
					fd_lock();
					FD_CLR(sock, &global_readsock_set);
					FD_CLR(sock, &aux_readsock_set);
					close(sock);
					fd_unlock();

					for (j = 0;j <=i; j++)
						free(get_fileNames[j]);
					LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [no such hashfile for get] %d\n", sock);
					return NULL;
				}
				incoming_request.req.get.blockSizes[i] = fileInfo.st_size;
				totalMessageSize += fileInfo.st_size;
			}
			gettimeofday(&end, NULL);
			outgoing_reply_header.req.get.numHashes = numHashes;
			outgoing_reply_header.errorCode = NO_ERROR;
			outgoing_reply_header.nextMessageSize = totalMessageSize; 
			outgoing_reply_header.server_time = time_diff(&end, &begin);
			retVal = blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply));
			if (retVal != sizeof(cas_reply))
			{
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "bad blocking cas_get_reply send ");

				/* error path must close socket and return NULL right then and there */
				fd_lock();
				FD_CLR(sock, &global_readsock_set);
				FD_CLR(sock, &aux_readsock_set);
				close(sock);
				fd_unlock();

				for (j = 0;j <= i; j++)
					free(get_fileNames[j]);
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [bad blocking send header on gethashes] %d\n", sock);
				return NULL;
			}
			for (i = 0; i < numHashes; i++)
			{
				if (incoming_request.req.get.blockSizes[i] == 0)
				{
					retVal = blockingSend(sock, zero_chunk, CAPFS_CHUNK_SIZE);
					if (retVal != CAPFS_CHUNK_SIZE)
					{
						char ch[128];
						sprintf(ch,"Bad blocking send of cas_get_req data\n");
						LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "%s", ch);

						/* error path must close socket and return NULL right then and there */
						fd_lock();
						FD_CLR(sock, &global_readsock_set);
						FD_CLR(sock, &aux_readsock_set);
						close(sock);
						fd_unlock();

						for (j = 0;j < numHashes; j++)
							free(get_fileNames[j]);
						LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [bad blocking send data on gethashes] %d\n", sock);
						return NULL;
					}
					continue;
				}
				fd = open(get_fileNames[i], O_RDONLY);
				if (fd < 0)
				{
					char ch[128];
					sprintf(ch,"cas_get_reply of data couldnt open file %s\n", get_fileNames[i]);
					LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "%s", ch);

					/* error path must close socket and return NULL right then and there */
					fd_lock();
					FD_CLR(sock, &global_readsock_set);
					FD_CLR(sock, &aux_readsock_set);
					close(sock);
					fd_unlock();

					for (j = 0;j < numHashes; j++)
						free(get_fileNames[j]);
					LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [could not open hash file on gethashes] %d\n", sock);
					return NULL;
				}
				retVal = blockingSendFile(fd, sock, incoming_request.req.get.blockSizes[i]);
				if (retVal != incoming_request.req.get.blockSizes[i])
				{
					char ch[128];
					sprintf(ch,"Couldnt sendfile of %s and sent %d\n", get_fileNames[i], retVal);
					LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "%s", ch);

					/* error path must close socket and return NULL right then and there */
					fd_lock();
					FD_CLR(sock, &global_readsock_set);
					FD_CLR(sock, &aux_readsock_set);
					close(sock);
					fd_unlock();

					close(fd);
					for (j = 0;j < numHashes; j++)
						free(get_fileNames[j]);
					LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [bad sendfile on gethashes] %d\n", sock);
					return NULL;
				}
				/* close the disk file */
				close(fd);
			}
			for (j = 0;j < numHashes; j++)
				free(get_fileNames[j]);
			/* Do not close the socket. This may be reused */
			break;
		}
		case CAS_PUT_REQ: 
		{
			char *data;
			struct timeval begin, end;

			gettimeofday(&begin, NULL);
			numHashes = incoming_request.header.req.put.numHashes;
			outgoing_reply_header.opcode = CAS_PUT_REPLY;
			outgoing_reply_header.nextMessageSize = 0;
			if (numHashes > CAPFS_MAXHASHES)
			{
				char ch[128];
				sprintf(ch,"cas_put_req requested to put many hashes simultaneously -- %d instead of %d(MAX)\n",
						numHashes, CAPFS_MAXHASHES);
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "%s", ch);
				outgoing_reply_header.errorCode = TOO_MANY_HASH_OPS;
				blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply));

				/* error path must close socket and return NULL right then and there */
				fd_lock();
				FD_CLR(sock, &global_readsock_set);
				FD_CLR(sock, &aux_readsock_set);
				close(sock);
				fd_unlock();

				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [invalid number of put hashes] %d\n", sock);
				return NULL;
			}
			LOG(stderr, DEBUG_MSG, SUBSYS_DATA, "[put %d] waiting for %d hashes\n", incoming_request.header.requestID, numHashes);
			/* Now receive the hashes */
			retVal = brecv(sock, (void*) &incoming_request.req.put.hashes, numHashes * CAPFS_MAXHASHLENGTH); 
			if (retVal != numHashes * CAPFS_MAXHASHLENGTH)
			{
				char ch[256];
				sprintf(ch, "Blocking recv in cas_put_req [%d] of hashes failed. Read %d instead of %d bytes (%d hashes)\n",
						incoming_request.header.requestID, retVal, numHashes * CAPFS_MAXHASHLENGTH, numHashes);
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "%s", ch);
				outgoing_reply_header.errorCode = BLOCKING_RECV_ERROR;
				blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply));

				/* error path must close socket and return NULL right then and there */
				fd_lock();
				FD_CLR(sock, &global_readsock_set);
				FD_CLR(sock, &aux_readsock_set);
				close(sock);
				fd_unlock();

				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [bad blocking recv on puthashes] %d\n", sock);
				return NULL;
			}
			hashPtr = incoming_request.req.put.hashes;
			/* first find out the total length of all the files that have been sent to me */
			totalMessageSize = numHashes * CAPFS_CHUNK_SIZE;

			/* now read the data -- the files themselves */
			data = (char*) calloc(1, totalMessageSize);
			if (data == NULL)
			{
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "calloc of %d bytes failed!\n", totalMessageSize);
				outgoing_reply_header.errorCode = -ENOMEM;
				blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply));

				/* error path must close socket and return NULL right then and there */
				fd_lock();
				FD_CLR(sock, &global_readsock_set);
				FD_CLR(sock, &aux_readsock_set);
				close(sock);
				fd_unlock();

				fprintf(stderr, "Closed socket [could not allocate memory] %d\n", sock);
				return NULL;
			}
			LOG(stderr, DEBUG_MSG, SUBSYS_DATA, "[put %d] Waiting for %d bytes of data\n",
					incoming_request.header.requestID, totalMessageSize);
			retVal = brecv(sock, (void*)data, totalMessageSize);
			if (retVal != totalMessageSize)
			{
				char ch[128];
				sprintf(ch,"blocking recv failed (cas_put_req file data). Read %d instead of %d bytes\n",
						retVal, totalMessageSize);
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "%s", ch);
				outgoing_reply_header.errorCode = BLOCKING_RECV_ERROR;
				blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply));
				free(data);

				/* error path must close socket and return NULL right then and there */
				fd_lock();
				FD_CLR(sock, &global_readsock_set);
				FD_CLR(sock, &aux_readsock_set);
				close(sock);
				fd_unlock();

				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [bad blocking recv data on put] %d\n", sock);
				return NULL;
			}
			/* now create and write out the files */
			ptr = data;
			for (i = 0;i < numHashes; hashPtr += CAPFS_MAXHASHLENGTH, i++)
			{
				fileName = get_fileName(hashPtr);
				fd = open(fileName, O_WRONLY|O_CREAT, 0700);
				if (fd < 0)
				{
					char ch[128];
					sprintf(ch,"Couldnt create file %s\n", fileName);
					LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "%s", ch);
					outgoing_reply_header.errorCode = FILE_ERROR;
					blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply));
					free(data);
					free(fileName);

					/* error path must close socket and return NULL right then and there */
					fd_lock();
					FD_CLR(sock, &global_readsock_set);
					FD_CLR(sock, &aux_readsock_set);
					close(sock);
					fd_unlock();

					LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [could not create hashfile] %d\n", sock);
					return NULL;
				}
				//flock(fd, LOCK_EX);
				if (write(fd, ptr, CAPFS_CHUNK_SIZE) != CAPFS_CHUNK_SIZE)
				{
					char ch[128];
					err = errno;
					sprintf(ch,"Couldnt write to file %s. error no %d\n", fileName, err);
					LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "%s", ch);
					outgoing_reply_header.errorCode = FILE_ERROR;
					blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply));
					free(data);
					free(fileName);

					/* error path must close socket and return NULL right then and there */
					fd_lock();
					FD_CLR(sock, &global_readsock_set);
					FD_CLR(sock, &aux_readsock_set);
					close(sock);
					fd_unlock();

					//flock(fd, LOCK_UN);
					close(fd);
					LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [could not write hashfile on put] %d\n", sock);
					return NULL;
				}
				//flock(fd, LOCK_UN);
				close(fd);
				ptr += CAPFS_CHUNK_SIZE;
				bytesDone += CAPFS_CHUNK_SIZE;
				free(fileName);
			}
			gettimeofday(&end, NULL);
			free(data);
			outgoing_reply_header.req.put.bytesDone = bytesDone;
			outgoing_reply_header.errorCode = NO_ERROR;
			outgoing_reply_header.server_time = time_diff(&end, &begin);
			if (blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply)) != sizeof(cas_reply))
			{
				/* error path must close socket and return NULL right then and there */
				fd_lock();
				FD_CLR(sock, &global_readsock_set);
				FD_CLR(sock, &aux_readsock_set);
				close(sock);
				fd_unlock();

				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [bad blocking send put hash reply] %d\n", sock);
				return NULL;
			}
			/* Do not close the socket. This may be reused */
			break;
		}
		case CAS_REMOVE_REQ:
		{
			struct stat sbuf;
			outgoing_reply_header.opcode = CAS_REMOVE_REPLY;
			if (incoming_request.header.req.remove.nameLen >= CAPFS_MAXNAMELEN)
			{
				outgoing_reply_header.errorCode = GENERIC_ERROR;
				blockingSend(sock, (void *)&outgoing_reply_header, sizeof(cas_reply));

				/* error path must close socket and return NULL right then and there */
				fd_lock();
				FD_CLR(sock, &global_readsock_set);
				FD_CLR(sock, &aux_readsock_set);
				close(sock);
				fd_unlock();

				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [invalid namelength] %d\n", sock);
				return NULL;
			}
			/* receive the file name */
			retVal = brecv(sock, incoming_request.req.remove.name, 
					incoming_request.header.req.remove.nameLen);
			if (retVal != incoming_request.header.req.remove.nameLen)
			{
				outgoing_reply_header.errorCode = GENERIC_ERROR;
				blockingSend(sock, (void *)&outgoing_reply_header, sizeof(cas_reply));

				/* error path must close socket and return NULL right then and there */
				fd_lock();
				FD_CLR(sock, &global_readsock_set);
				FD_CLR(sock, &aux_readsock_set);
				close(sock);
				fd_unlock();

				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [bad blocking recv on removereq] %d\n", sock);
				return NULL;
			}
			if (stat(incoming_request.req.remove.name, &sbuf) < 0)
			{
				outgoing_reply_header.errorCode = FILE_ERROR;
				blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply));

				/* error path must close socket and return NULL right then and there */
				fd_lock();
				FD_CLR(sock, &global_readsock_set);
				FD_CLR(sock, &aux_readsock_set);
				close(sock);
				fd_unlock();

				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [no such directory on removereq] %d\n", sock);
				return NULL;
			}
			if (do_flatten_hierarchy(incoming_request.req.remove.name) < 0) 
			{
				outgoing_reply_header.errorCode = GENERIC_ERROR;
				blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply));

				/* error path must close socket and return NULL right then and there */
				fd_lock();
				FD_CLR(sock, &global_readsock_set);
				FD_CLR(sock, &aux_readsock_set);
				close(sock);
				fd_unlock();

				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [do_flatten_hierarchy error] %d\n", sock);
				return NULL;
			}
			outgoing_reply_header.errorCode = NO_ERROR;
			if (blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply)) != sizeof(cas_reply))
			{
				/* error path must close socket and return NULL right then and there */
				fd_lock();
				FD_CLR(sock, &global_readsock_set);
				FD_CLR(sock, &aux_readsock_set);
				close(sock);
				fd_unlock();

				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [blocking send reply removereq error] %d\n", sock);
				return NULL;
			}
			/* Do not close the socket. This may be reused */
			break;
		}
		default:	
		{
			char ch[128];
			sprintf(ch,"Unknown opcode recvd: %d\n", incoming_request.header.opcode);
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "%s", ch);
			outgoing_reply_header.errorCode = BAD_OPCODE;
			outgoing_reply_header.opcode = CAS_UNKNOWN_OPCODE;
			blockingSend(sock, (void*)(&outgoing_reply_header), sizeof(cas_reply));

			fd_lock();
			FD_CLR(sock, &global_readsock_set);
			FD_CLR(sock, &aux_readsock_set);
			close(sock);
			fd_unlock();

			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Closed socket [bad opcode] %d\n", sock);
			return NULL;
		}
	}
	if (CAPFS_CAS_CACHE_HANDLES == 1)
	{
		fd_lock();
		/* Indicate to our parent thread that no one is servicing this sockfd by clearing it out from aux_readsock_set */
		FD_CLR(sock, &aux_readsock_set);
		fd_unlock();
	}
	else
	{
		close(sock);
	}
	return NULL;
}
