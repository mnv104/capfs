#ifndef _CAS_H
#define _CAS_H

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/statfs.h>
#include <desc.h>

typedef enum {CAS_CLIENT = 1, CAS_REP_SERVER = 2} cas_roles_t;

struct iod_map {
	int normalized_iod; /* Relative to how many iods participate for a particular file */
	int global_iod; /* Global iod number */
};

struct cas_options {
	int use_sockets;
	int doInstrumentation;
	char *co_fname;
	cas_roles_t co_role;
};

struct dataArray {
	void* start;
	int byteCount;
};

struct cas_return {
	struct dataArray* buf;
	int count;
	int64_t server_time;
};

struct cas_iod_worker_data {
	unsigned char* hashes; /*array of hashes that will map to this iod */
	struct sockaddr* iodAddress;
	int  iodNumber; /* used for statistics */
	struct cas_return* data; /* the data and counts for job to be done on this iod*/
	int *returnValue; /* how many of the hashes were done for this iod */
};

/* REQUEST opcodes in use */
enum {
	CAS_PING_REQ=1,
	CAS_PUT_REQ=2,
	CAS_GET_REQ=3,
	CAS_STATFS_REQ=4,
	CAS_REMOVE_REQ=5,
};

typedef struct cas_header cas_header;

/* the header packet that gets sent out on the wire */
struct cas_header {
	int requestID;
	int opcode; /* what is the request */
	union {
		struct {
			int numHashes;
		}get;
		struct {
			int numHashes;
		}put;
		struct {
			int nameLen;
		}remove;
	} req;
};

typedef struct cas_request cas_request;

struct cas_request {
	cas_header header;
	union {
		struct {
			unsigned char hashes[CAPFS_MAXHASHLENGTH * CAPFS_MAXHASHES];
			int blockSizes[CAPFS_MAXHASHES];
		}get;
		struct {
			unsigned char hashes[CAPFS_MAXHASHLENGTH * CAPFS_MAXHASHES];
		}put;
		struct {
			unsigned char name[CAPFS_MAXNAMELEN];
		}remove;
	}req;
};

/* REPLY opcodes in use */
enum {
	CAS_UNKNOWN_OPCODE=0,
	CAS_PING_REPLY=1,
	CAS_PUT_REPLY=2,
	CAS_GET_REPLY=3,
	CAS_STATFS_REPLY=4,
	CAS_REMOVE_REPLY=5,
};

/* request structure for the cas-enabled client and iod*/
typedef struct cas_reply cas_reply;

#define GENERIC_ERROR                  -EINVAL
#define NO_ERROR                       0
#define BAD_OPCODE                     -EBADRQC
#define BLOCKING_RECV_ERROR            -EPIPE
#define TOO_MANY_HASH_OPS              -E2BIG
#define FILE_ERROR                     -ENOENT
#define SHA1_COLLISION                 -ENOTUNIQ

/* the header packet that is recieved from the iod */
struct cas_reply {
	int requestID; /* unused as of now - should help non-blocking */
	int opcode; /* what is the request */
	int errorCode; /* defined in above */
	int nextMessageSize; /* how many bytes client should expect after this */
	int64_t server_time;
	union {
		struct {
			int numHashes;
		}get;
		struct {
			int bytesDone;
		}put;
		struct {
			struct statfs sfs;
		}cas_statfs;
	}req;
};

struct instrumentation_s{
	int used;
	struct timeval totalTime;
	struct timeval totalTime_ts;
	struct timeval srvrTime;
	struct timeval srvrTime_ts;
	struct timeval sha1Time;
	struct timeval sha1Time_ts;
	unsigned long long bytesDone;
};

extern struct instrumentation_s put_criticalPathTime;
extern struct instrumentation_s get_criticalPathTime;

/* Functions exported by client_ops.c */
extern int clnt_init(struct cas_options *options, int num_Iods, int blkSize);
extern void clnt_finalize(void);
extern void clnt_get(int tcp, struct cas_iod_worker_data *iod_jobs, int count);
extern void clnt_put(int tcp, struct cas_iod_worker_data *iod_jobs, int count);
extern int clnt_ping(int tcp, struct sockaddr* iodAddress);
extern int clnt_statfs_req(int tcp, struct sockaddr* iodAddress, struct statfs *sfs);
extern int clnt_removeall(int tcp, struct sockaddr *serverAddress, char *dirname);
extern struct cas_iod_worker_data* convert_to_jobs(struct dataArray* da, int nChunks, struct iod_map* map,
		fdesc* desc, unsigned char* hash, int *iodCount);
extern void freeJobs(struct cas_iod_worker_data *jobs, int nIods);

#endif
