/*
 * Simple RPC based hash server with callbacks.
 * (C) 2005 Murali Vilayannur
 */
#include <stdio.h>
#include <pthread.h>
#include <rpc/rpc.h>
#include <string.h>
#include <rpc/svc.h>
#include <semaphore.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include "client.h"
#include "rpcutils.h"
#include "lock.h"
#include "cmgr.h"
#include "quicklist.h"
#include "mquickhash.h"
#include "hcache.h"
#include "mpi.h"

#define true 1
#define false 0

static sem_t sem;
static struct svc_info info = {
use_thread: 1,
};
static int regd_id;

static struct params {
	int id;
	char *host_name;
	unsigned short port;
} *node_config = NULL;

static int param_count = 0;

/* 
 * File format is
 * <machinename> <port> <node identifier>
 */
static int parse_file(const char *filename)
{
	int ret = 0;
	FILE *fp;
	unsigned int port, nodeid;
	static char machine[256];

	fp = (FILE *) fopen(filename, "r");
	if (fp == NULL) {
		fprintf(stderr, "Could not open file %s\n", filename);
		return -1;
	}
	while (!feof(fp)) {
		if (fscanf(fp, "%s %d %d\n", machine, &port, &nodeid) != 3) {
			errno = EINVAL;
			fprintf(stderr, "Parse error!\n");
			ret = -1;
			break;
		}
		if (port > 65535) {
			fprintf(stderr, "Invalid value of port %d (OR) nodeid %d\n", port, nodeid);
			ret = -1;
			break;
		}
		node_config = (struct params *) realloc(node_config, (param_count + 1) * sizeof(struct params));
		if (node_config == NULL) {
			fprintf(stderr, "Could not realloc\n");
			ret = -1;
			break;
		}
		node_config[param_count].id = nodeid;
		node_config[param_count].port = port;
		node_config[param_count].host_name = strdup(machine);
		param_count++;
	}
	fclose(fp);
	return ret;
}

int get_port(int node_id)
{
	if (node_id < 0 || node_id >= param_count) {
		errno = EINVAL;
		return -1;
	}
	return node_config[node_id].port;
}

char *get_host(int node_id)
{
	return node_config[node_id].host_name;
}

static void register_with_hash_server(int prog, int vers, int proto)
{
	/* send a rpc call to hash server */
	CLIENT *clnt;
	enum clnt_stat ans;
	struct timeval tv;
	callback_args cbargs;
	struct sockaddr our_addr;

	clnt = get_svc_handle("sk01.cse.psu.edu", locksvc, 1, IPPROTO_TCP, 21, &our_addr);
	if (clnt == NULL) {
		clnt_pcreateerror ("");
		return;
	}
	else {
		cbargs.prog = prog;
		cbargs.vers = vers;
		cbargs.proto = proto;
		ans = register_cb_1(cbargs, &regd_id, clnt);
		if (ans != RPC_SUCCESS) {
			clnt_perror(clnt, "call failed to register\n");
		}
		else {
			printf("Registered mount id = %d\n", regd_id);
		}
		clnt_destroy(clnt);
	}
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

enum {H_READ = 0, H_WRITE = 1};

struct user_ptr 
{
	struct handle*    p;
	int 				mode;
	int				nframes;
	char 			  **buffers;
	size_t		  *sizes;
	int64_t		  *offsets;
	int			  *completed;
};

/*
* NOTE that we dont free the ->completed integer pointer
* since that is returned to the cache manager
*/
static void dealloc_user_ptr(struct user_ptr *ptr)
{
	if (ptr)
	{
		free(ptr);
	}
}

static struct user_ptr* alloc_user_ptr(void* p, int mode, int nframes,
		char **buffers, size_t *sizes, int64_t *offsets)
{
		struct user_ptr *ptr = NULL;

		ptr = (struct user_ptr *) calloc(1, sizeof(struct user_ptr));
		if (ptr)
		{
			ptr->p = p;
			ptr->mode = mode;
			ptr->nframes = nframes;
			ptr->buffers = buffers;
			ptr->sizes = sizes;
			ptr->offsets = offsets;
			/* We try to allocate this at the last, so 
			 * that if we had to call dealloc_user_ptr()
			 * we still would not have to free this!
			 */
			ptr->completed = (int *) 
				calloc(nframes, sizeof(int));
			if (!ptr->completed)
			{
				goto error_exit;
			}
		}
		return ptr;
error_exit:
		dealloc_user_ptr(ptr);
		return NULL;
}

static struct user_ptr* 
post_io(void* p, int nframes, char **buffers,
		size_t *sizes, int64_t *offsets, int mode, int *error)
{
		struct user_ptr *uptr = NULL;

		/* try to allocate the user ptr to keep track of the state */
		uptr = alloc_user_ptr(p, mode, nframes, buffers, sizes, offsets);
		if (!uptr)
		{
			*error = -ENOMEM;
			return NULL;
		}
		return uptr;
}

static int push_hashes(fname name)
{
	int res, i;
	CLIENT *clnt;
	struct timeval tv;
	hash_resp resp;
	enum clnt_stat ans;
	struct sockaddr our_addr;

	resp.hash_resp_len = 3;
	resp.hash_resp_val = (hash_res *) calloc(resp.hash_resp_len * sizeof(hash_res), 1);
	for (i = 0; i < resp.hash_resp_len; i++) {
		resp.hash_resp_val[i].digest = (hash) calloc(1, HASHLENGTH + 1);
		sprintf(resp.hash_resp_val[i].digest, "murali%d\n", i);
		print((unsigned char *)resp.hash_resp_val[i].digest, HASHLENGTH);
	}
	clnt = get_svc_handle("sk01.cse.psu.edu", locksvc, 1, IPPROTO_TCP, 21, &our_addr);
	if (clnt == NULL) {
		clnt_pcreateerror ("");
		return -1;
	}
	ans = put_hashes_1(name, resp, &res, clnt);
	if (ans != RPC_SUCCESS) {
		clnt_perror(clnt, "Rank 1 call failed to put hashes to the server\n");
		return -1;
	}
	printf("Rank 1 successfull in pushing hashes\n");
	return res;
}

static int fetch_hashes(struct user_ptr *uptr)
{
	hash_args hargs;
	enum clnt_stat ans;
	CLIENT* clnt;
	struct timeval tv;
	hash_resp resp;
	char *name;
	struct sockaddr addr;

	name = ((struct handle *) uptr->p)->name;
	hargs.cbid = regd_id;
	hargs.name = name;
	clnt = get_svc_handle("sk01.cse.psu.edu", locksvc, 1, IPPROTO_TCP, 21, &our_addr);
	if (clnt == NULL) {
		clnt_pcreateerror ("");
		return -1;
	}
	else {
		int i, j;
		resp.hash_resp_val = (hash_res *) calloc(MAXHASHES, sizeof(hash_res));
		for (i = 0; i < MAXHASHES; i++) {
			resp.hash_resp_val[i].digest = (char *) calloc(HASHLENGTH + 1, sizeof(char));
		}
		ans = get_hashes_1(hargs, &resp, clnt);
		if (ans != RPC_SUCCESS) {
			clnt_perror(clnt, "call failed to fetch hashes\n");
			return -1;
		}
		else {
			int    start_chunk;

			start_chunk = (uptr->offsets[0] / uptr->sizes[0]);
			/*
			printf("hashes for file %s: %d\n", name, resp.hash_resp_len);
			for (i = 0; i < resp.hash_resp_len; i++) {
				print(resp.hash_resp_val[i].digest, HASHLENGTH);
			}
			*/
			for (j = start_chunk; j < (start_chunk + uptr->nframes); j++) {
				if (j < resp.hash_resp_len) {
					memcpy(uptr->buffers[j - start_chunk], resp.hash_resp_val[j].digest, HASHLENGTH);
					uptr->completed[j] = HASHLENGTH;
				}
				else {
					uptr->completed[j] = 0;
					memset(uptr->buffers[j - start_chunk], 0, HASHLENGTH);
				}
			}
			return resp.hash_resp_len;
		}
		clnt_destroy(clnt);
	}
}

/*
 * readpage_begin() routine must return an opaque handle
 * on success and negative error code on failure.
 * This routine is invoked only on a cache miss.
 * Use this to just setup information for computing hashes if need be
 */ 
static long hash_buffered_read_begin(void* p, 
		int number, cm_buffer_t *buffers, size_t *sizes, int64_t *offsets)
{
		int ret;
		struct user_ptr *uptr = NULL;

		/* Actually the bulk of the work is done only at the time of the complete routine */
		if ((uptr = post_io(p, number, (char **) buffers,
						sizes, offsets, H_READ, &ret)) == NULL)
		{
			panic("hash_buffered_read: could not post read %d\n", ret);
			return ret;
		}
		return (long) uptr;
}

/*
 * Complete the I/O operation that was posted asynchronously
 * earlier. We return a pointer to an array of integers
 * that indicate the error codes in case of failed I/O
 * or amount of I/O completed.
 * Callers responsibility to free it.
 */
static int* hash_buffered_read_complete(long _uptr)
{
		struct user_ptr *uptr = NULL;
		int *completed = NULL, i;

		uptr = (struct user_ptr *) _uptr;
		completed = (uptr->completed);
		/*
		 * this is an RPC call, 
		 */
		fetch_hashes(uptr);
		/* Deallocate the user pointer */
		dealloc_user_ptr(uptr);
		return completed;
}

/*
 * writepage_begin() routine must return an opaque handle
 * on success and negative error code on failure.
 * This routine is invoked only on a cache miss.
 * There is no need to WB crypto-hashes!!
 * So these will be simple no-ops.
 */ 
static long hash_buffered_write_begin(cm_handle_t p, 
		int number, cm_buffer_t *buffers, size_t *sizes, int64_t *offsets)
{
		int ret;
		struct user_ptr *uptr = NULL;

		if ((uptr = post_io(p, number, (char **) buffers,
						sizes, offsets, H_WRITE, &ret)) == NULL)
		{
			panic("hash_buffered_write: could not post write %d\n", ret);
			return ret;
		}
		return (long) uptr;
}

/*
 * We don't have to writeback the hashes from the cache.
 */
static int* hash_buffered_write_complete(long _uptr)
{
		struct user_ptr *uptr = NULL;
		int *completed = NULL, i;

		uptr = (struct user_ptr *) _uptr;
		completed = (uptr->completed);
		for (i = 0; i < uptr->nframes; i++) {
			completed[i] = uptr->sizes[i];
		}
		/* Deallocate the user pointer */
		dealloc_user_ptr(uptr);
		return completed;
}

static int put_my_hashes(put_hashes_1_argument *argp, void *result, struct svc_req *rqstp)
{
	char* fname = argp->name;
	hash_resp resp = argp->arg2;
	int i, ret;

	for (i = 0; i < resp.hash_resp_len; i++) {
		char *ptr;

		ptr = resp.hash_resp_val[i].digest;
		printf("Trying to put hash ");
		print((unsigned char *)ptr, HASHLENGTH);
		hcache_put(fname, i, 1, ptr);
	}
	*(int *) result = 0;
	return 1;
}

/*
 * Handling RPC calls here
 */
int svc_try_handle(struct svc_req * request, SVCXPRT *xprt)
{
	fflush(stdout);
	switch (request->rq_proc) {
		case 0:
		{
			/* Null ping response */
			if (!svc_sendreply(xprt, (xdrproc_t) xdr_void, (caddr_t)NULL)) {
				fprintf(stderr, "Could not send reply! %s\n", strerror(errno));
				return 1;
			}
			return 0;
		}
		case REVOKE:
		{
			int handle;
			enum clnt_stat ans;

			/* we get the slot number from a client */
			if (!svc_getargs(xprt, (xdrproc_t) xdr_int, (caddr_t) &handle)) {
				svcerr_decode(xprt);
				return 1;
			}
			/* unlock the file associated with handle locally */
			if (!svc_sendreply(xprt, (xdrproc_t) xdr_void, (caddr_t)NULL)) {
				fprintf(stderr, "Could not send reply! %s\n", strerror(errno));
				return 1;
			}
			return 0;
		}
		case UPDATE:
		{
			xdrproc_t _xdr_argument, _xdr_result;
			put_hashes_1_argument putargs;
			int result, retval, r;

			_xdr_argument = (xdrproc_t) xdr_put_hashes_1_argument;
			_xdr_result = (xdrproc_t) xdr_int;
	
			MPI_Comm_rank(MPI_COMM_WORLD, &r);
			printf("Rank %d Got a callback update\n", r);
			memset(&putargs, 0, sizeof(putargs));
			if (!svc_getargs(xprt, (xdrproc_t) _xdr_argument, (caddr_t) &putargs)) {
				svcerr_decode(xprt);
				return 1;
			}
			/* call a local put_hash function */
			retval = put_my_hashes((void *)&putargs, (void *)&result, request);
			if (retval > 0 && !svc_sendreply(xprt, (xdrproc_t) _xdr_result, (caddr_t)&result)) {
				fprintf(stderr, "Could not send reply! %s\n", strerror(errno));
				svcerr_systemerr(xprt);
			}
			if (!svc_freeargs (xprt, (xdrproc_t) _xdr_argument, (caddr_t) &putargs)) {
				fprintf (stderr, "%s", "unable to free arguments");
				exit (1);
			}
			return 0;
		}
		default:
		{
			svcerr_noproc(xprt);
			return 1;
		}
	}
}

static int get_my_hashes(char *name)
{
	int result, i;
	char phash[60];

	result = hcache_get(name, 0, 3, -1, phash);
	for (i = 0; i < 3; i++) {
		print((unsigned char *)(phash + i * HASHLENGTH), HASHLENGTH);
	}
	printf("Rank 0 done getting hashes\n");
	return 0;
}

/*
 * This thread sends out messages to the server and 
 * tries to obtain hashes of the files 
 */
void * clnt_func(void *args)
{
	int i;
	struct t_args *targs = (struct t_args *) args;
	int svc_prog = targs->ta_prog_number, svc_vers = targs->ta_version;
	fname name;
	int result1;
	int rank = 0;

	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	sem_post(&sem);
	
	name = (fname) malloc(256);
	strcpy(name, "/tmp/newfixup.c");
	if (rank == 0) {
		printf("Rank 0 First time\n");
		result1 = get_my_hashes(name);
		fflush(stdout);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	if (rank == 1) {
		printf("Rank 1 trying to push its hashes\n");
		result1 = push_hashes(name);
		fflush(stdout);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	if (rank == 0) {
		printf("Rank 0 Second time\n");
		result1 = get_my_hashes(name);
		fflush(stdout);
	}
	MPI_Barrier(MPI_COMM_WORLD);
	return NULL;
}

static struct hcache_options opt = {
organization: CAPFS_HCACHE_SIMPLE,
	hr_begin: hash_buffered_read_begin,
	hr_complete: hash_buffered_read_complete,
	hw_begin: hash_buffered_write_begin,
	hw_complete: hash_buffered_write_complete,
};


int main(int argc, char *argv[])
{
	pthread_t t1[2];
	struct t_args *args[2];

	MPI_Init(&argc, &argv);
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <filename> <node identifier>\n", argv[0]);
		MPI_Finalize();
		exit(1);
	}
	if (parse_file(argv[1]) != 0) {
		fprintf(stderr, "Could not parse config file %s\n", argv[1]);
		MPI_Finalize();
		exit(1);
	}
	hcache_init(&opt);
	setup_service(-1, v1, -1, -1, (void (*)(struct svc_req *, SVCXPRT *)) svc_try_handle, &info);

	/* register a callback with the hash server and get an identifier for the callback */
	register_with_hash_server(info.svc_tcp_prog, v1, IPPROTO_TCP);
	MPI_Barrier(MPI_COMM_WORLD);
	args[1] = (struct t_args *) calloc(1, sizeof(struct t_args));
	args[1]->ta_fname = argv[1];
	args[1]->ta_id    = atoi(argv[2]);
	args[1]->ta_status = 0;
	args[1]->ta_prog_number = locksvc;
	args[1]->ta_version = v1;
	sem_init(&sem, 0, 0);
	pthread_create(&t1[1], NULL, clnt_func, args[1]);
	sem_wait(&sem);
	if (args[1]->ta_status != 0) {
		errno = args[1]->ta_status;
		fprintf(stderr, "Could not start client thread: %s\n", strerror(errno));
	}
	else {
		pthread_join(t1[1], NULL);
	}
	hcache_finalize();
	cleanup_service(&info);
	free(args[1]);

	MPI_Finalize();
	return 0;
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 *
 * vim: ts=3
 */


