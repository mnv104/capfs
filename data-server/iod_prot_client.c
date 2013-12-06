#include "iod_prot.h"
#include "capfs_iod.h"
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <rpc/rpc.h>
#include <rpc/clnt.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/statfs.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <netinet/in.h>
#include "sockio.h"
#include "log.h"
#include "rpcutils.h"
#include "iod_prot_client.h"

enum {USED = 1, UNUSED = 0};

#define MAXIODS  512

static pthread_mutex_t iod_mutex = PTHREAD_MUTEX_INITIALIZER;

extern pthread_spinlock_t seq_lock;
typedef struct iod_entry iod_entry;

struct iod_entry {
	int used;
	struct sockaddr_in addr;
	int tcp_prog_number, udp_prog_number;
	int tcp_version, udp_version;
	CLIENT *clnt;
	int    sockfd;
	struct sockaddr_in our_addr;
};

static iod_entry iod_table[MAXIODS];
static int iod_count = 0;

/*
 * This variable determines whether or not CLIENT handles to IOD servers need to be cached 
 * i.e we cache handles if it is set to 1
 * and re-connect everytime if it is set to 0.
 * See comment in meta-server/iod_prot_client.c as to why it is okay
 * for client-side daemon to cache CLIENT handles to iod servers.
 * This variable is also used to determine if we cache sockets to iod
 * servers.
 */
static int cache_handle_policy = CAPFS_CAS_CACHE_HANDLES;

static int convert_to_errno(enum clnt_stat rpc_error)
{
	if (rpc_error == RPC_SUCCESS) {
		return 0;
	}
	if (rpc_error == RPC_CANTDECODEARGS
			|| rpc_error == RPC_CANTDECODEARGS
			|| rpc_error == RPC_CANTSEND
			|| rpc_error == RPC_CANTRECV
			|| rpc_error == RPC_CANTDECODERES
			|| rpc_error == RPC_AUTHERROR) {
		return EREMOTEIO;
	}
	if (rpc_error == RPC_TIMEDOUT) {
		return ETIMEDOUT;
	}
	if (rpc_error == RPC_PROGNOTREGISTERED
			|| rpc_error == RPC_PROCUNAVAIL
			|| rpc_error == RPC_VERSMISMATCH) {
		return ECONNREFUSED;
	}
	/* I know this will cause incorrect error messages to pop up,but theres no choice */
	LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "rpc error %d returning as EINVAL\n", rpc_error);
	return EINVAL;
}

static int find_id_of_host(struct sockaddr_in *raddr)
{
	int i, j;

	i = 0;
	j = 0;
	while (i < iod_count && j < MAXIODS) {
		if (iod_table[j].used == USED) {
			/* Matching the host addresses and port #s */
			if (iod_table[j].addr.sin_addr.s_addr == raddr->sin_addr.s_addr
					&& iod_table[j].addr.sin_port == raddr->sin_port) {
				return j;
			}
			i++;
		}
		j++;
	}
	return -1;
}

static int find_unused_id(void)
{
	int i;

	i = 0;
	while (i < MAXIODS) {
		if (iod_table[i].used != USED) {
			return i;
		}
		i++;
	}
	return -1;
}

static CLIENT** get_clnt_handle(int tcp, struct sockaddr_in *iodaddr)
{
	int id = -1;
	CLIENT *clnt = NULL, **pclnt = NULL;
	static struct timeval new_iod_timeout = {IOD_CLNT_TIMEOUT, 0};

	pthread_mutex_lock(&iod_mutex);

	if ((id = find_id_of_host(iodaddr)) < 0) {
		char *iod_host = NULL;

		id = find_unused_id();
		iod_host = inet_ntoa(iodaddr->sin_addr);
		iod_table[id].used = USED;
		memcpy(&iod_table[id].addr, iodaddr, sizeof(struct sockaddr_in));
		iod_count++;
		/* fill in the program number for the given host, port number combinations */
		if ((iod_table[id].tcp_prog_number = get_program_info(iod_host, 
						IPPROTO_TCP, ntohs(iodaddr->sin_port), &iod_table[id].tcp_version)) < 0) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "Could not lookup TCP program number and version number for %s:%d\n", 
					iod_host, ntohs(iodaddr->sin_port));
			pthread_mutex_unlock(&iod_mutex);
			return NULL;
		}
		if ((iod_table[id].udp_prog_number = get_program_info(iod_host,
						IPPROTO_UDP, ntohs(iodaddr->sin_port), &iod_table[id].udp_version)) < 0) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "Could not lookup UDP program number and version number for %s:%d\n", 
					iod_host, ntohs(iodaddr->sin_port));
			pthread_mutex_unlock(&iod_mutex);
			return NULL;
		}
		/* 
		 * iod_table[id].clnt = clnt = clnt_create(iod_host, 
		 * (tcp == 1) ? iod_table[id].tcp_prog_number : iod_table[id].udp_prog_number,
		 * (tcp == 1) ? iod_table[id].tcp_version : iod_table[id].udp_version,
		 * (tcp == 1) ? "tcp" : "udp");
		 */
		iod_table[id].clnt = clnt = get_svc_handle(iod_host, 
				(tcp == 1) ? iod_table[id].tcp_prog_number : iod_table[id].udp_prog_number,
				(tcp == 1) ? iod_table[id].tcp_version : iod_table[id].udp_version, 
				(tcp == 1) ? IPPROTO_TCP : IPPROTO_UDP, IOD_CLNT_TIMEOUT,
				(struct sockaddr *)&iod_table[id].our_addr);
		if (clnt == NULL) {
			clnt_pcreateerror (iod_host);
		}
		/* set the timeout to a fairly large value... */
		else {
			if (clnt_control(clnt, CLSET_TIMEOUT, (char *)&new_iod_timeout) == 0)
			{
				LOG(stderr, WARNING_MSG, SUBSYS_DATA, "Cannot reset timeout.. continuing with 25 second timeout\n");
			}
		}
	}
	else {
		if ((clnt = iod_table[id].clnt) == NULL) {
			char *iod_host = NULL;

			iod_host = inet_ntoa(iodaddr->sin_addr);
			/*iod_table[id].clnt = clnt = clnt_create(iod_host, 
					(tcp == 1) ? iod_table[id].tcp_prog_number : iod_table[id].udp_prog_number,
					(tcp == 1) ? iod_table[id].tcp_version : iod_table[id].udp_version, 
					(tcp == 1) ? "tcp":"udp");*/
			iod_table[id].clnt = clnt = get_svc_handle(iod_host,
					(tcp == 1) ? iod_table[id].tcp_prog_number : iod_table[id].udp_prog_number,
					(tcp == 1) ? iod_table[id].tcp_version : iod_table[id].udp_version, 
					(tcp == 1) ? IPPROTO_TCP : IPPROTO_UDP, IOD_CLNT_TIMEOUT,
					(struct sockaddr *)&iod_table[id].our_addr);
			if (clnt == NULL) {
				clnt_pcreateerror (iod_host);
			}
			else {
				if (clnt_control(clnt, CLSET_TIMEOUT, (char *)&new_iod_timeout) == 0)
				{
					LOG(stderr, WARNING_MSG, SUBSYS_DATA, "Cannot reset timeout.. continuing with 25 second timeout\n");
				}
			}
		}
	}
	pclnt = &iod_table[id].clnt;
	LOG(stderr, DEBUG_MSG, SUBSYS_DATA, "My address to IOD is %s\n", inet_ntoa(iod_table[id].our_addr.sin_addr));
	pthread_mutex_unlock(&iod_mutex);
	return pclnt;
}

static void put_clnt_handle(CLIENT **pclnt, int force_put)
{
	if (pclnt == NULL || 
			*pclnt == NULL) {
		return;
	}
	if (force_put) {
		clnt_destroy(*pclnt);
		/* make it reconnect next time around */
		*pclnt = NULL;
		return;
	}
	if (cache_handle_policy == 0) {
		clnt_destroy(*pclnt);
		/* make it reconnect */
		*pclnt = NULL;
	}
	return;
}

static int* get_clnt_sock(struct sockaddr_in *addr)
{
	int id = -1;
	int sockNum, *psock = NULL;

	pthread_mutex_lock(&iod_mutex);
	if ((id = find_id_of_host(addr)) < 0)
	{
		sockNum = socket(AF_INET, SOCK_STREAM,0);
		if (sockNum < 0)
		{
			pthread_mutex_unlock(&iod_mutex);
			return NULL;
		}
		if (!nb_connect(sockNum, (struct sockaddr *) addr))
		{
			close(sockNum);
			pthread_mutex_unlock(&iod_mutex);
			return NULL;
		}
		if (CAPFS_CAS_CACHE_HANDLES == 1)
		{
			fprintf(stderr, "connect to %s:%d through socket %d\n", 
					inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), sockNum); 
		}
		id = find_unused_id();
		iod_table[id].used = USED;
		iod_table[id].sockfd = sockNum;
		memcpy(&iod_table[id].addr, addr, sizeof(struct sockaddr_in));
		iod_count++;
		set_sockopt(sockNum, SO_REUSEADDR, 1);
	}
	else {
		if ((sockNum = iod_table[id].sockfd) < 0)
		{
			sockNum = socket(AF_INET, SOCK_STREAM,0);
			if (sockNum < 0)
			{
				pthread_mutex_unlock(&iod_mutex);
				return NULL;
			}
			if (!nb_connect(sockNum, (struct sockaddr *) addr))
			{
				close(sockNum);
				pthread_mutex_unlock(&iod_mutex);
				return NULL;
			}
			if (CAPFS_CAS_CACHE_HANDLES == 1)
			{
				fprintf(stderr, "Connect to %s:%d through socket %d\n", 
						inet_ntoa(addr->sin_addr), ntohs(addr->sin_port), sockNum);
			}
			iod_table[id].sockfd = sockNum;
			set_sockopt(sockNum, SO_REUSEADDR, 1);
		}
	}
	psock = &iod_table[id].sockfd;
	pthread_mutex_unlock(&iod_mutex);
	return psock;
}

static void put_clnt_sock(int *psock, int force_put)
{
	if (psock == NULL || *psock < 0) {
		return;
	}
	if (force_put) {
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "Closing socket (force put) %d\n", *psock);
		close(*psock);
		/* make it reconnect next time around */
		*psock = -1;
		return;
	}
	if (cache_handle_policy == 0) {
		LOG(stderr, DEBUG_MSG, SUBSYS_META, "Closing socket (don't cache handles) %d\n", *psock);
		close(*psock);
		/* make it reconnect */
		*psock = -1;
	}
	/* cache it otherwise */
	return;
}

int cas_ping(int use_sockets, int tcp, struct sockaddr_in *addr)
{
	/* use the cleaner RPC interfaces */
	if (use_sockets == 0)
	{
		CLIENT **clnt = NULL;
		struct timeval TV = {25, 0};
		enum clnt_stat result;

		clnt = get_clnt_handle(tcp, addr);
		if (clnt == NULL) {
			errno = EINVAL;
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "cas_ping: No registered CAS RPC service on the specified port!\n");
			return -1;
		}
		if (*clnt == NULL) {
			errno = ECONNREFUSED;
			return -1;
		}
		result = clnt_call(*clnt, 0,
				(xdrproc_t) xdr_void, (caddr_t) NULL,
				(xdrproc_t) xdr_void, (caddr_t) NULL, TV);
		if (result != RPC_SUCCESS) {
			/* make it reconnect */
			put_clnt_handle(clnt, 1);
			errno = convert_to_errno(result);
			return -1;
		}
		put_clnt_handle(clnt, 0);
		return 0;
	}
	/* or use the higher performance socket libraries */
	else
	{
		int *psock = NULL;
		int numSent;
		cas_header header;
		cas_reply reply_header;

		/* We will use TCP only in case of sockets regardless */
		psock = get_clnt_sock(addr);
		if (psock == NULL)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "cas_ping: No registered CAS listener "
					"on the specified port! %s\n", strerror(errno));
			return -1;
		}
		header.requestID = 0;
		header.opcode = CAS_PING_REQ;
		numSent = blockingSend(*psock, &header, sizeof(cas_header));
		if(numSent != sizeof(cas_header))
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "sent %d instead of %d\n",numSent,sizeof(cas_header));
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		numSent = brecv(*psock, &reply_header, sizeof(cas_reply));
		if (numSent != sizeof(cas_reply))
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "recvd %d instead of %d\n", numSent, sizeof(cas_reply));
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		if (reply_header.opcode != CAS_PING_REPLY)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "bad opcode %d instead of %d\n", reply_header.opcode, CAS_PING_REPLY);
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		put_clnt_sock(psock, 0);
		return 0;
	}
}

int cas_statfs(int use_sockets, int tcp, struct sockaddr_in *addr, struct statfs *sfs)
{
	if (use_sockets == 0)
	{
		CLIENT **clnt = NULL;
		enum clnt_stat result;
		cas_stat_resp resp;

		memset(&resp, 0, sizeof(resp));
		clnt = get_clnt_handle(tcp, addr);
		if (clnt == NULL) {
			errno = EINVAL;
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "cas_statfs: No registered CAS RPC service on the specified port!\n");
			return -1;
		}
		if (*clnt == NULL) {
			errno = ECONNREFUSED;
			return -1;
		}
		result = capfs_dstatfs_1(&resp, *clnt);
		if (result != RPC_SUCCESS) {
			/* make it reconnect */
			put_clnt_handle(clnt, 1);
			errno = convert_to_errno(result);
			return -1;
		}
		if (resp.status) {
			errno = -resp.status;
			return -1;
		}
		sfs->f_type = resp.sfs.f_type;
		sfs->f_bsize = resp.sfs.f_bsize;
		sfs->f_blocks = (uint64_t) resp.sfs.f_blocks;
		sfs->f_bfree = (uint64_t) resp.sfs.f_bfree;
		sfs->f_bavail = (uint64_t) resp.sfs.f_bavail;
		sfs->f_files = (uint64_t) resp.sfs.f_files;
		sfs->f_ffree = (uint64_t) resp.sfs.f_ffree;
		memcpy(&sfs->f_fsid, &resp.sfs.f_fsid, sizeof(resp.sfs.f_fsid));
		sfs->f_namelen = resp.sfs.f_namelen;
		put_clnt_handle(clnt, 0);
		return 0;
	}
	else
	{
		int *psock = NULL;
		int numSent;
		cas_header header;
		cas_reply reply_header;

		/* We will use TCP only in case of sockets regardless */
		psock = get_clnt_sock(addr);
		if (psock == NULL)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "cas_statfs: No registered CAS listener "
					"on the specified port! %s\n", strerror(errno));
			return -1;
		}
		header.requestID = 0;
		header.opcode = CAS_STATFS_REQ;
		numSent = blockingSend(*psock, &header, sizeof(cas_header));
		if(numSent != sizeof(cas_header))
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "sent %d instead of %d\n", numSent, sizeof(cas_header));
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		numSent = brecv(*psock, &reply_header, sizeof(cas_reply));
		if (numSent != sizeof(cas_reply))
		{		
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "recvd %d instead of %d\n", numSent, sizeof(cas_reply));
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		if ((reply_header.opcode != CAS_STATFS_REPLY) )
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "bad opcode %d instead of %d\n", reply_header.opcode, CAS_STATFS_REPLY);
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		if (reply_header.errorCode != NO_ERROR)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "error code %d\n", reply_header.errorCode);
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		memcpy(sfs, &(reply_header.req.cas_statfs.sfs), sizeof(struct statfs));
		put_clnt_sock(psock, 0);
		return 0;
	}
}

int cas_removeall(int use_sockets, int tcp, struct sockaddr_in *addr, char *dirname)
{
	if (use_sockets == 0)
	{
		CLIENT **clnt = NULL;
		enum clnt_stat result;
		removeall_req  req;
		removeall_resp resp;
		int ret = 0;

		memset(&req, 0, sizeof(req));
		memset(&resp, 0, sizeof(resp));
		req.name = (char *) calloc(sizeof(char), CAPFS_MAXNAMELEN);
		if (req.name == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Could not allocate\n");
			return -1;
		}
		snprintf(req.name, CAPFS_MAXNAMELEN, dirname);
		clnt = get_clnt_handle(tcp, addr);
		if (clnt == NULL) {
			free(req.name);
			errno = EINVAL;
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "cas_remove: No registered CAS RPC service on the specified port!\n");
			return -1;
		}
		if (*clnt == NULL) {
			free(req.name);
			errno = ECONNREFUSED;
			return -1;
		}
		result = capfs_removeall_1(req, &resp, *clnt);
		if (result != RPC_SUCCESS) {
			free(req.name);
			/* make it reconnect */
			put_clnt_handle(clnt, 1);
			errno = convert_to_errno(result);
			return -1;
		}
		free(req.name);
		put_clnt_handle(clnt, 0);
		if (resp.status) {
			errno = -resp.status;
			ret = -1;
		}
		return ret;
	}
	else
	{
		int *psock = NULL;
		int numSent;
		cas_header header;
		cas_reply reply_header;

		/* We will use TCP only in case of sockets regardless */
		psock = get_clnt_sock(addr);
		if (psock == NULL)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "cas_remove: No registered CAS listener "
					"on the specified port! %s\n", strerror(errno));
			return -1;
		}
		header.requestID = 0;
		header.opcode = CAS_REMOVE_REQ;
		header.req.remove.nameLen = strlen(dirname);
		numSent = blockingSend(*psock, &header, sizeof(cas_header));
		if(numSent != sizeof(cas_header))
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "sent %d instead of %d\n", numSent, sizeof(cas_header));
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		numSent = blockingSend(*psock, dirname, header.req.remove.nameLen);
		if (numSent != header.req.remove.nameLen)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "sent %d instead of %d\n", numSent, header.req.remove.nameLen);
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		numSent = brecv(*psock, &reply_header, sizeof(cas_reply));
		if (numSent != sizeof(cas_reply)) 
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "recvd %d instead of %d\n", numSent, sizeof(cas_reply));
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		if (reply_header.opcode != CAS_REMOVE_REPLY)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "bad opcode %d instead of %d\n", reply_header.opcode, CAS_REMOVE_REPLY);
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		if (reply_header.errorCode != NO_ERROR)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "error code %d\n", reply_header.errorCode);
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		put_clnt_sock(psock, 0);
		return 0;
	}
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
		blocks_dtor(blocks, 0);
		return -ENOMEM;
	}
	for (i = 0; i < len; i++) {
		blocks->data_blocks_val[i].data_len = CAPFS_CHUNK_SIZE;
		blocks->data_blocks_val[i].data_val = (char *) calloc(CAPFS_CHUNK_SIZE, sizeof(char));
		if (blocks->data_blocks_val[i].data_val == NULL) {
			break;
		}
	}
	if (i != len) {
		blocks_dtor(blocks, i);
		return -ENOMEM;
	}
	return 0;
}

static void puthashes_dtor(put_hashes *h)
{
	free(h->put_hashes_val);
	h->put_hashes_val = NULL;
	h->put_hashes_len = 0;
	return;
}

static int puthashes_ctor(put_hashes *h, int count, unsigned char *hashes)
{
	int i;

	h->put_hashes_len = count;
	h->put_hashes_val = (sha1hash *) calloc(count, sizeof(sha1hash));
	if (h->put_hashes_val == NULL) {
		puthashes_dtor(h);
		return -ENOMEM;
	}
	for (i = 0; i < count; i++) {
		memcpy(h->put_hashes_val[i], hashes + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH);
	}
	return 0;
}

static void put_req_dtor(put_req *req, int count)
{
	puthashes_dtor(&req->h);
	blocks_dtor(&req->blocks, count);
	return;
}

static int put_req_ctor(put_req *req, int count, unsigned char *hashes)
{
	if (puthashes_ctor(&req->h, count, hashes) < 0) {
		return -1;
	}
	if (blocks_ctor(&req->blocks, count) < 0) {
		puthashes_dtor(&req->h);
		return -ENOMEM;
	}
	return 0;
}

static void put_resp_dtor(put_resp *resp)
{
	opstatus_dtor(&resp->status);
	return;
}

static int put_resp_ctor(put_resp *resp, int count)
{
	if (opstatus_ctor(&resp->status, count) < 0) {
		return -ENOMEM;
	}
	return 0;
}

static int check_response(put_resp *resp)
{
	int i, ret = 0;
	
	if (resp->status.op_status_len == 0) {
		return -ENOMEM;
	}
	for (i = 0; i < resp->status.op_status_len; i++) {
		if ((ret = resp->status.op_status_val[i]) < 0) {
			LOG(stderr, DEBUG_MSG, SUBSYS_DATA, "check_response got error on %d: %d\n", i, ret);
			return ret;
		}
	}
	return 0;
}

/* Copy from the user-specified pointers to the RPC structures */
static int copy_from_job(put_req *req, struct cas_return *job)
{
	int i;

	if (job->count != req->blocks.data_blocks_len) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "job count [%d] != blocks_len [%d]\n",
				job->count, req->blocks.data_blocks_len);
		return -ENOMEM;
	}
	//LOG(stderr, DEBUG_MSG, SUBSYS_DATA, "job->count = %d\n", job->count);
	for (i = 0; i < job->count; i++) 
	{
		/*LOG(stderr, DEBUG_MSG, SUBSYS_DATA, "job->buf[i].byteCount = %d, start = %p\n", 
				job->buf[i].byteCount, job->buf[i].start); */
		assert(job->buf[i].byteCount > 0 && job->buf[i].byteCount <= CAPFS_CHUNK_SIZE);
		req->blocks.data_blocks_val[i].data_len = job->buf[i].byteCount;
		memcpy(req->blocks.data_blocks_val[i].data_val, job->buf[i].start, job->buf[i].byteCount);
	}
	return 0;
}

static inline void lock_seq(void)
{
	pthread_spin_lock(&seq_lock);
}

static inline void unlock_seq(void)
{
	pthread_spin_unlock(&seq_lock);
}

int cas_put(int use_sockets, int tcp, struct sockaddr_in *addr, unsigned char *hashes, struct cas_return *job)
{
	if (use_sockets == 0)
	{
		put_req req;
		put_resp resp;
		CLIENT **clnt = NULL;
		enum clnt_stat result;
		int ret = 0;

		memset(&req, 0, sizeof(req));
		memset(&resp, 0, sizeof(resp));
		if (job == NULL || hashes == NULL || job->count <= 0) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "Invalid parameter to cas_put\n");
			errno = EFAULT;
			return -1;
		}
		/* Construct the arguments */
		if (put_req_ctor(&req, job->count, hashes) < 0) 
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "could not construct request to cas_put\n");
			return -1;
		}
		/* Copy the job data to the RPC arguments */
		copy_from_job(&req, job);
		/* Allocate space for the response */
		if (put_resp_ctor(&resp, job->count) < 0) 
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "could not construct resp to cas_put\n");
			put_req_dtor(&req, job->count);
			return -1;
		}
		clnt = get_clnt_handle(tcp, addr);
		if (clnt == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "cas_put: No registered CAS RPC service on the specified port!\n");
			put_resp_dtor(&resp);
			put_req_dtor(&req, job->count);
			errno = EINVAL;
			return -1;
		}
		if (*clnt == NULL) 
		{
			put_req_dtor(&req, job->count);
			put_resp_dtor(&resp); 
			errno = ECONNREFUSED;
			return -1;
		}
		result = capfs_put_1(req, &resp, *clnt);
		if (result != RPC_SUCCESS) {
			put_req_dtor(&req, job->count);
			put_resp_dtor(&resp); 
			/* make it reconnect */
			put_clnt_handle(clnt, 1);
			errno = convert_to_errno(result);
			return -1;
		}
		/* check response and see if anything failed */
		ret = check_response(&resp);
		/* Convert to errno */
		if (ret < 0) {
			errno = -ret;
			ret = -1;
		}
		put_req_dtor(&req, job->count);
		put_resp_dtor(&resp); 
		put_clnt_handle(clnt, 0);
		job->server_time = resp.put_time;
		if (ret < 0) {
			return ret;
		}
		return resp.bytes_done;
	}
	else
	{
		int *psock = NULL;
		int i, total_msg_size = 0, numSent = 0;
		cas_header header;
		cas_reply reply_header;
		char *data = NULL, *ptr = NULL;
		static int put_id;

		errno = EIO;
		psock = get_clnt_sock(addr);
		if (psock == NULL)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "cas_put: No registered CAS listener "
					"on the specified port %s\n", strerror(errno));
			return -1;
		}
		lock_seq();
		header.requestID = put_id++;
		unlock_seq();
		header.opcode = CAS_PUT_REQ;
		header.req.put.numHashes = job->count;
		numSent = blockingSend(*psock, &header, sizeof(cas_header));
		if (numSent != sizeof(cas_header))
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "sent %d instead of %d\n", numSent, sizeof(cas_header));
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		numSent = blockingSend(*psock, hashes, job->count * CAPFS_MAXHASHLENGTH);
		if (numSent != (job->count * CAPFS_MAXHASHLENGTH))
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "sent %d instead of %d\n", numSent, (job->count * CAPFS_MAXHASHLENGTH));
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		data = (char *) calloc(job->count,  CAPFS_CHUNK_SIZE);
		if (data == NULL)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "malloc failed!\n");
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		ptr = data;
		total_msg_size = 0;
		for (i = 0; i < job->count; i++)
		{
			memcpy(ptr, job->buf[i].start, job->buf[i].byteCount);
			ptr += job->buf[i].byteCount;
			total_msg_size += job->buf[i].byteCount;
			if (job->buf[i].byteCount != CAPFS_CHUNK_SIZE)
			{
				LOG(stderr, WARNING_MSG, SUBSYS_DATA, "[put] sending %d bytes rather than chunk_size(%d)\n",
						job->buf[i].byteCount, CAPFS_CHUNK_SIZE);
			}
		}
		if (total_msg_size > (job->count * CAPFS_CHUNK_SIZE))
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Total job size is greater than allocated memory size\n");
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			free(data);
			return -1;
		}
		numSent = blockingSend(*psock, data, total_msg_size);
		if (numSent != total_msg_size)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "sent %d instead of %d\n", numSent, total_msg_size);
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			free(data);
			return -1;
		}
		free(data);
		LOG(stderr, DEBUG_MSG, SUBSYS_DATA, "[put %d] Waiting for reply on socket %d\n", 
				header.requestID, *psock);
		numSent = brecv(*psock, &reply_header, sizeof(cas_reply));
		if (numSent != sizeof(cas_reply))
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "brecv (put) did not receive response! "
					"%d bytes instead of %d: %s\n", numSent, sizeof(cas_reply), 
					(numSent < 0) ? strerror(errno) : "timed out");
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		if (reply_header.opcode != CAS_PUT_REPLY)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Invalid put reply opcode (%d) instead of %d\n", 
					reply_header.opcode, CAS_PUT_REPLY);
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		if (reply_header.errorCode != NO_ERROR) 
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "Put error on server (%d)\n", reply_header.errorCode);
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		if (reply_header.req.put.bytesDone != total_msg_size)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "bytesPut on server %d != expected total_msg_size %d\n",
					reply_header.req.put.bytesDone, total_msg_size);
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		job->server_time = reply_header.server_time;
		put_clnt_sock(psock, 0);
		return reply_header.req.put.bytesDone;
	}
}

static void gethashes_dtor(get_hashes *h)
{
	free(h->get_hashes_val);
	h->get_hashes_val = NULL;
	h->get_hashes_len = 0;
	return;
}

static int gethashes_ctor(get_hashes *h, int count, unsigned char *hashes)
{
	int i;

	h->get_hashes_len = count;
	h->get_hashes_val = (sha1hash *) calloc(count, sizeof(sha1hash));
	if (h->get_hashes_val == NULL) {
		gethashes_dtor(h);
		return -ENOMEM;
	}
	for (i = 0; i < count; i++) {
		memcpy(h->get_hashes_val[i], hashes + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH);
	}
	return 0;
}

static int get_resp_ctor(get_resp *resp, int count)
{
	if (opstatus_ctor(&resp->status, count) < 0) {
		return -ENOMEM;
	}
	if (blocks_ctor(&resp->blocks, count) < 0) {
		opstatus_dtor(&resp->status);
		return -ENOMEM;
	}
	return 0;
}

static void get_resp_dtor(get_resp *resp, int count)
{
	opstatus_dtor(&resp->status);
	blocks_dtor(&resp->blocks, count);
	return;
}

static int copy_to_job(struct cas_return *job, get_resp *resp)
{
	int i, ret = 0, total = 0;
	
	/* must be a ENOMEM situation at the server side */
	if (job->count != resp->blocks.data_blocks_len
			|| job->count != resp->status.op_status_len) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "job count [%d] != data_blocks_len [%d] != op_status_len [%d]\n",
				job->count, resp->blocks.data_blocks_len, resp->status.op_status_len);
		return -ENOMEM;
	}
	for (i = 0; i < job->count; i++) {
		if (resp->status.op_status_val[i]) {
			ret = resp->status.op_status_val[i];
		}
		else {
			job->buf[i].byteCount = resp->blocks.data_blocks_val[i].data_len;
			assert(job->buf[i].byteCount > 0);
			total += job->buf[i].byteCount;
			memcpy(job->buf[i].start, resp->blocks.data_blocks_val[i].data_val, job->buf[i].byteCount);
		}
	}
	/* time taken at the server */
	job->server_time = resp->get_time;
	return (ret == 0) ? total : ret;
}

int cas_get(int use_sockets, int tcp, struct sockaddr_in *addr, unsigned char *hashes, struct cas_return *job)
{
	if (use_sockets == 0)
	{
		get_req req;
		get_resp resp;
		CLIENT **clnt = NULL;
		enum clnt_stat result;
		int ret;

		memset(&req, 0, sizeof(req));
		memset(&resp, 0, sizeof(resp));
		if (job == NULL || hashes == NULL || job->count <= 0) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "Invalid parameter to cas_get\n");
			errno = EFAULT;
			return -1;
		}
		/* Construct the arguments */
		if (gethashes_ctor(&req.h, job->count, hashes) < 0) {
			return -1;
		}
		/* Allocate space for the response */
		if (get_resp_ctor(&resp, job->count) < 0) {
			gethashes_dtor(&req.h);
			return -1;
		}
		clnt = get_clnt_handle(tcp, addr);
		if (clnt == NULL) {
			get_resp_dtor(&resp, job->count);
			gethashes_dtor(&req.h);
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "cas_get: No registered CAS RPC service on the specified port!\n");
			errno = EINVAL;
			return -1;
		}
		if (*clnt == NULL) {
			errno = ECONNREFUSED;
			get_resp_dtor(&resp, job->count);
			gethashes_dtor(&req.h);
			return -1;
		}
		result = capfs_get_1(req, &resp, *clnt);
		if (result != RPC_SUCCESS) {
			get_resp_dtor(&resp, job->count);
			gethashes_dtor(&req.h);
			/* make it reconnect */
			put_clnt_handle(clnt, 1);
			errno = convert_to_errno(result);
			return -1;
		}
		/* Copy the response data to the job pointers. even if 1 get failed, this will return an error! */
		ret = copy_to_job(job, &resp);
		/* Convert it to an errno */
		if (ret < 0) {
			errno = -ret;
			ret = -1;
		}
		get_resp_dtor(&resp, job->count);
		gethashes_dtor(&req.h);
		put_clnt_handle(clnt, 0);
		return ret;
	}
	else
	{
		int *psock = NULL;
		int i, total_msg_size = 0, numSent = 0;
		cas_header header;
		cas_reply reply_header;
		char *data = NULL, *ptr = NULL;
		static int get_id = 0;

		errno = EIO;
		psock = get_clnt_sock(addr);
		if (psock == NULL)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "cas_get: No registered CAS listener "
					"on the specified port %s\n", strerror(errno));
			return -1;
		}
		lock_seq();
		header.requestID = get_id++;
		unlock_seq();
		header.opcode = CAS_GET_REQ;
		header.req.get.numHashes = job->count;
		numSent = blockingSend(*psock, &header, sizeof(cas_header));
		if (numSent != sizeof(cas_header))
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "sent %d instead of %d\n",
					numSent, sizeof(cas_header));
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		numSent = blockingSend(*psock, hashes, job->count * CAPFS_MAXHASHLENGTH);
		if (numSent != (job->count * CAPFS_MAXHASHLENGTH))
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "sent %d instead of %d\n",
					numSent, (job->count * CAPFS_MAXHASHLENGTH));
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_DATA, "[get %d] Waiting for reply on socket %d\n", 
				header.requestID, *psock);
		/* wait for header */
		numSent = brecv(*psock, &reply_header, sizeof(cas_reply));
		if (numSent != sizeof(cas_reply))
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "brecv (get) did not receive response! "
					"%d bytes instead of %d: %s\n", numSent, sizeof(cas_reply), 
					(numSent < 0) ? strerror(errno) : "timed out");
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		if (reply_header.opcode != CAS_GET_REPLY)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "(get) Bad opcode %d instead of %d\n",
					reply_header.opcode, CAS_GET_REPLY);
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		if (reply_header.errorCode != NO_ERROR)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "(get) Error code on server %d\n", reply_header.errorCode);
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		if (reply_header.req.get.numHashes != job->count)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "(get) Number of hashes = %d should have been %d\n",
					reply_header.req.get.numHashes, job->count);
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		if (reply_header.nextMessageSize != job->count * CAPFS_CHUNK_SIZE)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, " (get) Next message size = %d, should have been %d\n",
					reply_header.nextMessageSize, job->count * CAPFS_CHUNK_SIZE);
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		data = (char *) calloc(reply_header.nextMessageSize, 1);
		if (data == NULL)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "(get) calloc of %d failed!\n", reply_header.nextMessageSize);
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_DATA, "[get %d] Waiting for data on socket %d\n", header.requestID, *psock);
		numSent = brecv(*psock, data, reply_header.nextMessageSize);
		if (numSent != reply_header.nextMessageSize)
		{
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA, "brecv (get) did not receive data! "
					"%d bytes instead of %d: %s\n", numSent, reply_header.nextMessageSize, 
					(numSent < 0) ? strerror(errno) : "timed out");
			free(data);
			/* make it reconnect */
			put_clnt_sock(psock, 1);
			return -1;
		}
		total_msg_size = 0;
		/* copy to user pointers */
		ptr = data;
		for (i = 0; i < job->count; i++)
		{
			job->buf[i].byteCount = CAPFS_CHUNK_SIZE;
			memcpy(job->buf[i].start, ptr, CAPFS_CHUNK_SIZE);
			ptr += CAPFS_CHUNK_SIZE;
			total_msg_size += CAPFS_CHUNK_SIZE;
		}
		job->server_time = reply_header.server_time;
		put_clnt_sock(psock, 0);
		free(data);
		return total_msg_size;
	}
}
