/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#include <netinet/in.h>

#define INBUFSZ 1024
#define MAXOPTLEN 1024

struct iod_config {
	unsigned short port;
	struct in_addr acc_addr;	/* in network byte order */
	struct in_addr acc_mask;	/* in network byte order */
	char user[MAXOPTLEN];
	char group[MAXOPTLEN];
	char rootdir[MAXOPTLEN];
	char datadir[MAXOPTLEN];
	char logdir[MAXOPTLEN];
	int access_size;
	int write_buf;
	int socket_buf;
	int log_level;
	int enable_sendfile;
	int num_threads;
};

extern struct iod_config __iod_config;
int parse_config(char *fname);
int dump_config(FILE *fp);
int set_config(void);
int get_config_port(void);
int get_config_log_level(void);
char *get_config_logdir(void);
int get_config_access_size(void);
int get_config_write_buf(void);
int get_config_socket_buf(void);
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
