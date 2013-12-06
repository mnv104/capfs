/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <sockio.h>
#include <capfs.h>
#include <cas.h>

int parse_args(int argc, char **argv);
int usage(int argc, char **argv);

extern char *optarg;
extern int optind;

static char host[256] = "localhost";
static int port_nr = CAPFS_IOD_BASE_PORT;

static int use_sockets = 0;
int capfs_mode = 1;
#define TRY_UDP 0
#define TRY_TCP 1

int main(int argc, char **argv)
{
	struct sockaddr iodAddr;
	struct cas_options cas_options = {
doInstrumentation:0,
use_sockets:0,
	};

	parse_args(argc, argv);
	if (optind < argc) {
		usage(argc, argv);
	}
	cas_options.use_sockets = use_sockets;
	clnt_init(&cas_options, 1, CAPFS_CHUNK_SIZE);
	if (init_sock(&iodAddr, host, port_nr) < 0) {
		fprintf(stderr, "No such host : %s? %s\n", host, strerror(errno));
		goto oops;
	}
	if (clnt_ping(TRY_TCP, &iodAddr) == 0) {
		goto oops;
	}
	clnt_finalize();
	printf("CAPFS I/O server on %s:%d is up and listening.\n", host, port_nr);
	exit(0);
oops:
	clnt_finalize();
	printf("CAPFS I/O server on %s:%d is down.\n", host, port_nr);
	exit(1);
}


int parse_args(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "h:p:s")) != EOF) {
		switch(c) {
			case 's':
				use_sockets = 1;
				break;
			case 'h':
				strncpy(host, optarg, 255);
				host[255] = 0;
				break;
			case 'p':
				port_nr = atoi(optarg);
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
	fprintf(stderr, "Usage: capfs-iod-ping [-h host] [-p port] [-s]\n");
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

