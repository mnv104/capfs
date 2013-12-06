#include <rpc/rpc.h>
#include <stdio.h>
#include <rpc/xdr.h>
#include <rpc/xdr.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

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
		printf("No answer from the specified program's UDP service\n");
	}
	else {
		ret = 0;
		printf("Specified program's UDP service is alive\n");
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
		printf("No answer from the specified program's TCP service\n");
	}
	else {
		ret = 0;
		printf("Specified program's TCP service is alive\n");
	}
	clnt_destroy(clnt);
	return ret;
}

int main(int argc, char *argv[])
{
	char *hostname;
	int prog_num, vers_num, ret1, ret2, timeout = 21;

	if (argc != 4 && argc != 5) {
		fprintf(stderr, "Usage: %s <hostname> <program number> <version> <timeout>\n", argv[0]);
		exit(1);
	}
	if (argc == 5) {
		timeout = atoi(argv[4]);
	}
	hostname = argv[1];
	prog_num = atoi(argv[2]);
	vers_num = atoi(argv[3]);
	ret1 = ping_udp(hostname, prog_num, vers_num, timeout);
	ret2 = ping_tcp(hostname, prog_num, vers_num, timeout);
	return (ret1 + ret2);
}
