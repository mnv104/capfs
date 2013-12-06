/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include "mgr_prot.h"

int parse_args(int argc, char **argv);
int usage(int argc, char **argv);

extern char *optarg;
extern int optind;

static char host[256] = "localhost";
static char fs[256] ="/";
int capfs_mode = 1;

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


int main(int argc, char **argv)
{
	int ret1, ret2;

	parse_args(argc, argv);
	if (optind < argc) {
		usage(argc, argv);
	}
	ret1 = ping_udp(host, CAPFS_MGR, mgrv1, 21);
	ret2 = ping_tcp(host, CAPFS_MGR, mgrv1, 21);

	exit(ret1+ret2);
}


int parse_args(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "h:f:")) != EOF) {
		switch(c) {
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

int usage(int argc, char **argv)
{
	fprintf(stderr, "Usage: mgr-ping [-h host] [-p port] [-f metadata_dir]\n");
	exit(1);
} /* end of USAGE() */

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
