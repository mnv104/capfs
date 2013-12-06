#include <stdio.h>
#include <pthread.h>
#include <rpc/rpc.h>
#include <string.h>
#include <rpc/svc.h>
#include <semaphore.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include "rpcutils.h"

#define testv1 1

static struct svc_info info = {
	use_thread: 1,
};

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
			return 1;
		}
		default:
		{
			svcerr_noproc(xprt);
			return 1;
		}
	}
}

static void my_shutdown(int sig, siginfo_t *pinfo, void *unused)
{
	cleanup_service(&info);
	exit(1);
}

int main(int argc, char *argv[])
{
	struct sigaction handler;
	int use_thread = 0;

	if (argc > 1) {
		use_thread = atoi(argv[1]);
	}
	info.use_thread = use_thread;
	memset(&handler, 0, sizeof(handler));
	handler.sa_flags = SA_SIGINFO;
	handler.sa_sigaction = my_shutdown;
	sigaction(SIGINT, &handler, NULL);
	sigaction(SIGTERM, &handler, NULL);
	setup_service(-1, testv1, -1, -1, (void (*)(struct svc_req *, SVCXPRT *))svc_try_handle, &info);
	if (info.use_thread) {
		pthread_join(info.tid, NULL);
	}
	return 0;
}
