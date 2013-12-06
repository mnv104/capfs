/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#include "capfs-header.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sockio.h>
#include <capfs.h>
#include <netdb.h>
#include "capfs_config.h"
#include "lib.h"
#include "mgr_prot.h"
#include "mgr.h"
#include "cas.h"

#define TRY_UDP 0
#define TRY_TCP 1

int parse_args(int argc, char **argv);
int usage(int argc, char **argv);

extern char *optarg;
extern int optind;

static char host[256] = "localhost";
static char fs[256] ="/";
static int report_space = 0, use_sockets = 0;

int capfs_mode = 1;

static int ping_udp(char *hostname, int prog_num, int version, int timeout);
static int ping_tcp(char *hostname, int prog_num, int version, int timeout);
int mgr_ping(char *host);
int capfs_iod_ping(char *host, int port_nr);
int capfs_iod_freespace(char *host, int port_nr, int index);

/* capfs_mgr_init()
 *
 * Returns a pointer to a dynamically allocated region holding
 * connection information for a given manager.
 */
static struct sockaddr *capfs_mgr_init(char *host)
{
	struct sockaddr *sp;

	sp = (struct sockaddr *)malloc(sizeof(struct sockaddr));
	if (sp == NULL) return NULL;

	/* port number is immaterial */
	if (init_sock(sp, host, 0) < 0) 
		goto init_mgr_conn_error;
	return sp;

init_mgr_conn_error:
	free(sp);
	return NULL;
}



int main(int argc, char **argv) 
{
	int i, err, iod_err;
	struct mreq req;
	struct mack ack;
	char *data;
	struct iod_info *info;
	struct sockaddr *mgr;
	struct ackdata_c ackdata;
	struct cas_options cas_options = {
doInstrumentation:0,
use_sockets:0,
	};
	struct capfs_options opt;
	
	opt.tcp = 1;
	opt.use_hcache = 0;
	
	parse_args(argc, argv);
	if (optind < argc) {
		usage(argc, argv);
	}
	cas_options.use_sockets = use_sockets;
	if ((mgr = capfs_mgr_init(host)) == NULL) {
		perror("Bad host name?\n");
		exit(1);
	}

	/*
	 * FIRST DO A SIMPLE PING OF THE MGR
	 */
	err = mgr_ping(host);
	if (err < 0) {
		printf("mgr (%s) is down.\n", host);
		printf("capfs file system %s has issues.\n", fs);
		return 1;
	}

	/*
	 * MGR IS UP AND RUNNING; NOW GET IOD INFO
	 */

	req.majik_nr   = MGR_MAJIK_NR;
	req.release_nr = CAPFS_RELEASE_NR;
	req.type       = MGR_IOD_INFO;
	req.dsize      = strlen(fs) + 1;
	data 				= (char *) malloc(CAPFS_MAXIODS * sizeof(iod_info));

	ackdata.type      = MGR_IOD_INFO;
	ackdata.u.iodinfo.niods = CAPFS_MAXIODS;
	ackdata.u.iodinfo.pinfo   = (iod_info *) data;

	/* we will use TCP service to get the iod_info */
	if ((err = send_mreq_saddr(&opt, mgr, &req, fs, &ack, &ackdata)) < 0) {
		printf("error: send_mreq_saddr: mgr (%s) is alive, but %s is not recognized as a file system? %s\n", 
			host, fs, strerror(errno));
		printf("capfs file system %s has issues.\n", fs);
		exit(1);
	}

	if (ack.status != 0) {
		printf("error: non-zero ack status: mgr (%s) is alive, but %s is not recognized as a file system? %s\n", 
			host, fs, strerror(errno));
		printf("capfs file system %s has issues.\n", fs);
		exit(1);
	}

	printf("mgr (%s) is responding.\n", host);

	/*
	 * MANAGER IS OK, NOW WE CHECK ON THE IODS
	 */

	clnt_init(&cas_options, 1, CAPFS_CHUNK_SIZE);

	info = (struct iod_info *) data;
	iod_err = 0;
	for (i=0; i < ack.ack.iod_info.nr_iods; i++) {
		static char iod_host[]="xxx.xxx.xxx.xxx\0";
		unsigned char *uc = (unsigned char *)&(info[i].addr.sin_addr);
		int iod_port;

		iod_port = ntohs(info[i].addr.sin_port);
		sprintf(iod_host, "%d.%d.%d.%d", uc[0], uc[1], uc[2], uc[3]);

		err = capfs_iod_ping(iod_host, iod_port);
		if (err < 0) {
			printf("CAPFS I/O server %d (%s:%d) is down.\n", i, iod_host,
				iod_port);
			iod_err++;
		}
		else {
			printf("CAPFS I/O server %d (%s:%d) is responding.\n", i,
				iod_host, iod_port);
			if (report_space) {
				err = capfs_iod_freespace(iod_host, iod_port, i);
				if (err < 0) {
					printf("iod %d (%s:%d) failed to report free space.\n", i, iod_host, iod_port);
					iod_err++;
				}
			}
		}
	}
	clnt_finalize();
	if (iod_err) {
		printf("CAPFS file system %s has issues.\n", fs);
		return 1;
	}
	else {
		printf("CAPFS file system %s is fully operational.\n", fs);
		return 0;
	}
}


int parse_args(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "h:f:ds")) != EOF) {
		switch(c) {
			case 's':
				use_sockets = 1;
				break;
			case 'd':
				report_space = 1;
				break;
			case 'h':
				strncpy(host, optarg, 255);
				host[255] = 0;
				break;
			case 'f':
				strncpy(fs, optarg, 255);
				break;
			case '?':
				usage(argc, argv);
				break;
		}
	}
	return 1;
} /* end of PARSE_ARGS() */

/* mgr_ping(host) - does a blocking ping to a mgr
 *
 * Returns 0 on success, -1 on failure.
 */
int mgr_ping(char *host)
{
	return (ping_udp(host, CAPFS_MGR, mgrv1, 21) + ping_tcp(host, CAPFS_MGR, mgrv1, 21));
}

/* capfs_iod_freespace(host, port_nr)
 *
 * Returns 0 on success, -1 on failure.
 */
int capfs_iod_freespace(char *host, int port_nr, int index)
{
	struct sockaddr iodAddr;
	struct statfs sfs;

	if (init_sock(&iodAddr, host, port_nr) < 0) {
		fprintf(stderr, "No such host : %s? %s\n", host, strerror(errno));
		goto oops;
	}
	if (clnt_statfs_req(TRY_TCP, &iodAddr, &sfs) < 0) {
			goto oops;
	}
	printf("iod %d (%s:%d): total space = %Ld Mbytes, free space = %Ld Mbytes\n",
			 index, host, port_nr,
			 (int64_t) ((int64_t) sfs.f_blocks * (int64_t) sfs.f_bsize) / 1048576,
			 (int64_t) ((int64_t) sfs.f_bavail * (int64_t) sfs.f_bsize) / 1048576);
	return 0;
oops:
	return -1;
}

/* capfs_iod_ping(host, port) - does a blocking ping to an capfs I/O server
 *
 * Returns 0 on success, -1 on failure.
 */
int capfs_iod_ping(char *host, int port_nr)
{
	struct sockaddr iodAddr;
	if (init_sock(&iodAddr, host, port_nr) < 0) {
		fprintf(stderr, "No such host : %s? %s\n", host, strerror(errno));
		goto oops;
	}
	if (clnt_ping(TRY_TCP, &iodAddr) == 0) {
		goto oops;
	}
	return 0;
oops:
	return -1;
}

int usage(int argc, char **argv)
{
	fprintf(stderr, "Usage: capfs-ping [-h host] [-p port] [-f metadata_dir] [-s] [-d]\n");
	exit(1);
} /* end of USAGE() */

static int ping_udp(char *hostname, int prog_num, int version, int timeout)
{
	struct sockaddr_in addr;
	struct hostent *hent;
	CLIENT *clnt;
	struct timeval val;
	enum clnt_stat ans;
	int s = RPC_ANYSOCK, ret;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = 0;
	hent = gethostbyname(hostname);
	if (hent != NULL) {
		memcpy(&addr.sin_addr.s_addr, hent->h_addr_list[0], hent->h_length);
	}
	else {
		herror("Could not lookup hostname:");
		return -1;
	}
	val.tv_sec = timeout;
	val.tv_usec = 0;
	if ((clnt = clntudp_create(&addr, prog_num, version, val, &s)) == NULL) {
		clnt_pcreateerror("Could not create udp client handle");
		return -1;
	}
	ans = clnt_call(clnt, 0, (xdrproc_t) xdr_void, 0, (xdrproc_t) xdr_void, 0, val);
	if (ans != RPC_SUCCESS) {
		ret = -1;
		clnt_perror(clnt, "No answer from meta-server on UDP service\n");
	}
	else {
		ret = 0;
		printf("Meta-data service is ready and alive on UDP\n");
	}
	clnt_destroy(clnt);
	return ret;
}

static int ping_tcp(char *hostname, int prog_num, int version, int timeout)
{
	struct sockaddr_in addr;
	struct hostent *hent;
	CLIENT *clnt;
	struct timeval val;
	enum clnt_stat ans;
	int s = RPC_ANYSOCK, ret;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = 0;
	hent = gethostbyname(hostname);
	if (hent != NULL) {
		memcpy(&addr.sin_addr.s_addr, hent->h_addr_list[0], hent->h_length);
	}
	else {
		herror("Could not lookup hostname:");
		return -1;
	}
	val.tv_sec = timeout;
	val.tv_usec = 0;
	clnt = clnttcp_create(&addr, prog_num, version, &s, 0, 0);
	if (clnt == NULL) {
		clnt_pcreateerror("Could not create tcp client handle");
		return -1;
	}
	ans = clnt_call(clnt, 0, (xdrproc_t) xdr_void, 0, (xdrproc_t) xdr_void, 0, val);
	if (ans != RPC_SUCCESS) {
		ret = -1;
		clnt_perror(clnt, "No answer from meta-server on TCP\n");
	}
	else {
		ret = 0;
		printf("Meta-data service is ready and alive on TCP\n");
	}
	clnt_destroy(clnt);
	return ret;
}


/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 

