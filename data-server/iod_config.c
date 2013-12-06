/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/*
 * These functions parse the config file for the I/O daemon and set
 * up the daemon's environment based on the specified configuration.
 * 
 * File format is simple:
 * 
 * 1) Blank lines are ignored
 * 2) Lines starting with '#' are ignored
 * 3) All other lines are of the format <option> <value>, where value
 *    is an arbitrary string ended by a carriage return or '#'
 * 4) In most cases only the first whitespace separated item in the
 *    value field is used for setting the option (eg. port).
 * 
 * SAMPLE CONFIG FILE:
 * 
 * #
 * # IOD configuration file
 * #
 * 
 * port 6969
 * accept_addr 192.168.4.0
 * accept_mask 255.255.255.0
 * user capfs
 * rootdir /
 * datadir /var/capfs-data
 * logdir /tmp
 * log_level 0
 * write_buf 512
 * access_size 512
 * socket_buf 64
 * 
 * END OF SAMPLE CONFIG FILE
 *
 * Separate functions are used for reading the configuration file and
 * setting up the resulting environment.
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <capfs_config.h>
#include <ctype.h>
#include <iod_config.h>
#include <log.h>

/* CAPFS_LOG_DIR overrides IOD_LOGDIR */
#ifdef CAPFS_LOG_DIR
#undef  IOD_LOGDIR
#define IOD_LOGDIR CAPFS_LOG_DIR
#endif

struct iod_config  __iod_config = {
	CAPFS_IOD_BASE_PORT,
	{0},
	{0},
	IOD_USER,
	IOD_GROUP, 
	IOD_ROOTDIR,
	IOD_DATADIR,
	IOD_LOGDIR,
	IOD_ACCESS_PAGE_SIZE,
	IOD_WRITE_BUFFER_SIZE,
	IOD_SOCKET_BUFFER_SIZE,
	CRITICAL_MSG | WARNING_MSG /* default log_level */,
	0 /* enable sendfile */,
	DEFAULT_THREADS
};

int parse_config(char *fname)
{
	FILE *cfile;
	char inbuf[INBUFSZ], *option, *value;

	__iod_config.acc_addr.s_addr = htonl(ACC_ADDR);
	__iod_config.acc_mask.s_addr = htonl(ACC_MASK);

	/* open file */
	if (!(cfile = fopen(fname, "r"))) {
		return(-1);
	}

	while (fgets(inbuf, INBUFSZ, cfile)) {
		/* comments get skipped here */
		if (*inbuf == '#') continue;

		/* blank lines get skipped here */
		if (!(option = strtok(inbuf, " \t\n"))) continue;

		if (!(value = strtok(NULL, "#\n"))) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "parse_config: error tokenizing value\n");
			return(-1);
		}
		/* PORT (eg. "port 7000") */
		if (!strcasecmp("port", option)) {
			char *err;
			if (!(strtok(value, " \t\n#"))) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "parse_config: error reducing string");
				continue;
			}
			while (isspace(*value)) value++;
			__iod_config.port = strtol(value, &err, 10);
			if (*err) /* bad character in value */ {
				LOG(stderr, WARNING_MSG, SUBSYS_DATA,  "trailing character(s) in port\n");
			}
		}
		/* ACC_ADDR (eg. "accept_addr 192.168.4.0") */
		else if (!strcasecmp("accept_addr", option)) {
			if (!(strtok(value, " \t\n#"))) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "parse_config: error reducing string");
				continue;
			}
			while (isspace(*value)) value++;
			if (inet_aton(value,&__iod_config.acc_addr) == 0) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "parse_config: accept_addr %s is invalid\n", value);
				return(-1);
			}
		}
		/* ACC_MASK (eg. "accept_mask 255.255.255.0") */
		else if (!strcasecmp("accept_mask", option)) {
			if (!(strtok(value, " \t\n#"))) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "parse_config: error reducing string");
				continue;
			}
			while (isspace(*value)) value++;
			if (inet_aton(value,&__iod_config.acc_mask) == 0) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "parse_config: accept_mask %s is invalid\n", value);
				return(-1);
			}
		}
		/* USER (eg. "user nobody") */
		else if (!strcasecmp("user", option)) {
			if (!(strtok(value, " \t\n#"))) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "parse_config: error reducing string");
				continue;
			}
			while (isspace(*value)) value++;
			strncpy(__iod_config.user, value, MAXOPTLEN);
		}
		/* GROUP (eg. "group bin") */
		else if (!strcasecmp("group", option)) {
			if (!(strtok(value, " \t\n#"))) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "parse_config: error reducing string");
				continue;
			}
			while (isspace(*value)) value++;
			strncpy(__iod_config.group, value, MAXOPTLEN);
		}
		/* ROOTDIR (eg. "rootdir /") */
		else if (!strcasecmp("rootdir", option)) {
			if (!(strtok(value, " \t\n#"))) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "parse_config: error reducing string");
			}
			while (isspace(*value)) value++;
			strncpy(__iod_config.rootdir, value, MAXOPTLEN);
		}
		/* DATADIR (eg. "datadir /tmp") */
		else if (!strcasecmp("datadir", option)) {
			if (!(strtok(value, " \t\n#"))) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "parse_config: error reducing string");
			}
			while (isspace(*value)) value++;
			strncpy(__iod_config.datadir, value, MAXOPTLEN);
		}
		/* LOGDIR (eg. "logdir /tmp") */
		else if (!strcasecmp("logdir", option)) {
			if (!(strtok(value, " \t\n#"))) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "parse_config: error reducing string");
			}
			while (isspace(*value)) value++;
			strncpy(__iod_config.logdir, value, MAXOPTLEN);
		}
		/* ACCESS_SIZE ( eg access_size 512 ) */
		else if (!strcasecmp("access_size", option)) {
			char *err;
			if (!(strtok(value, " \t\n#"))) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "parse_config: error reducing string");
			}
			while (isspace(*value)) value++;
			__iod_config.access_size = strtol(value, &err, 10)*1024; /* in KiB */
			if (*err) /* bad character in value */ {
				LOG(stderr, WARNING_MSG, SUBSYS_DATA,  "trailing character(s) in access_size\n");
			}
		}
		/* WRITE_BUF ( eg write_buf 512 ) */
		else if (!strcasecmp("write_buf", option)) {
			char *err;
			if (!(strtok(value, " \t\n#"))) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "parse_config: error reducing string");
			}
			while (isspace(*value)) value++;
			__iod_config.write_buf = strtol(value, &err, 10)*1024; /* in KiB */
			if (*err) /* bad character in value */ {
				LOG(stderr, WARNING_MSG, SUBSYS_DATA,  "trailing character(s) in write_buf\n");
			}
		}
		/* SOCKET_BUF( eg socket_buf 64) */
		else if (!strcasecmp("socket_buf", option)) {
			char *err;
			if (!(strtok(value, " \t\n#"))) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "parse_config: error reducing string");
			}
			while (isspace(*value)) value++;
			__iod_config.socket_buf = strtol(value, &err, 10)*1024; /* in KiB */
			if (*err) /* bad character in value */ {
				LOG(stderr, WARNING_MSG, SUBSYS_DATA,  "trailing character(s) in socket_buf\n");
			}
		}
		/* log_level (eg. "log_level 4") */
		else if (!strcasecmp("log_level", option)) {
			char *err;
			if (!(strtok(value, " \t\n#"))) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "Unable to reduce the string for log_level\n");
			}
			while (isspace(*value)) value++;
			__iod_config.log_level = strtol(value, &err, 10);
			if (*err) /* bad character in value */ {
				LOG(stderr, WARNING_MSG, SUBSYS_DATA,  "trailing character(s) in log_level\n");
			}
		}
		/* ENABLE_SENDFILE (eg. "enable_sendfile 1" to enable) */
		else if (!strcasecmp("enable_sendfile", option)) {
			char *err;
			if (!(strtok(value, " \t\n#"))) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "parse_config: error reducing string");
			}
			while (isspace(*value)) value++;
			__iod_config.enable_sendfile = strtol(value, &err, 10);
			if (*err) /* bad character in value */ {
				LOG(stderr, WARNING_MSG, SUBSYS_DATA,  "trailing character(s) in enable_sendfile\n");
			}
		}
		/* NUM_THREADS (eg. "num_threads 5") */
		else if (!strcasecmp("num_threads", option)) {
			char *err;
			if (!(strtok(value, " \t\n#"))) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "parse_config: error reducing string for num_threads");
			}
			while (isspace(*value)) value++;
			__iod_config.num_threads = strtol(value, &err, 10);
			if (*err) /* bad character in value */ {
				LOG(stderr, WARNING_MSG, SUBSYS_DATA,  "trailing character(s) in num_threads\n");
			}
		}
		else {
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "unknown option: %s\n", option);
		}
	}
	fclose(cfile);
	return(0);
} /* end of parse_config() */

int dump_config(FILE *fp)
{
	fprintf(fp,  "# I/O DAEMON CONFIG FILE -- AUTOMATICALLY GENERATED\n");
	fprintf(fp,  "port %d\n", __iod_config.port);
	fprintf(fp,  "accept_addr %s\n", inet_ntoa(__iod_config.acc_addr));
	fprintf(fp,  "accept_mask %s\n", inet_ntoa(__iod_config.acc_mask));
	fprintf(fp,  "user %s\n", __iod_config.user);
	fprintf(fp,  "group %s\n", __iod_config.group);
	fprintf(fp,  "rootdir %s\n", __iod_config.rootdir);
	fprintf(fp,  "datadir %s\n", __iod_config.datadir);
	fprintf(fp,  "logdir %s\n", __iod_config.logdir);
	fprintf(fp,  "write_buf %d\n", (__iod_config.write_buf)/1024);
	fprintf(fp,  "access_size %d\n", (__iod_config.access_size)/1024);
	fprintf(fp,  "socket_buf %d\n", (__iod_config.socket_buf)/1024);
	fprintf(fp,  "log_level %d\n", __iod_config.log_level);
	fprintf(fp,  "enable_sendfile %d\n", __iod_config.enable_sendfile);
	return(0);
} /* end of dump_config() */

/* SET_CONFIG() - set environment using configuration parameters
 *
 * First we determine if we are running as root.
 *
 * Steps (if root):
 * 1) find gid, uid
 * 2) chroot() to rootdir
 * 3) Set current working directory to datadir
 * 4) set gid, uid
 *
 * Steps (if not root):
 * 1) Set current working directory to datadir
 *
 * Returns 0 on success, -1 on failure.
 */
int set_config(void)
{
	int uid=-1, gid=-1; 
	struct group *grp_p;
	struct passwd *pwd_p;
	char im_root;

	im_root = (!geteuid() || !getuid()) ? 1 : 0;

	if (im_root) {
#ifdef HAVE_SCYLD
		/* Always run as root on scyld slave nodes.
		 * No other users exist there.
		 */
		gid = 0;
		uid = 0;
#else
		if (!(grp_p = getgrnam(__iod_config.group))) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "set_config: getgrnam error. "
			    "make sure the group id specified in iod.conf exists in /etc/group.\n");
			return(-1);
		}
		gid = grp_p->gr_gid;

		if (!(pwd_p = getpwnam(__iod_config.user))) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_DATA,  "setconfig: getpwnam error. "
			    "make sure the user id specified in iod.conf exists in /etc/passwd.\n");
			PERROR(SUBSYS_DATA,"set_config: getpwnam");
			return(-1);
		}
		uid = pwd_p->pw_uid;
#endif

		if (chroot(__iod_config.rootdir) < 0) {
			PERROR(SUBSYS_DATA,"set_config: chroot");
			return(-1);
		}
	}

	if (chdir(__iod_config.datadir) < 0) {
		PERROR(SUBSYS_DATA,"set_config: chdir");
		return(-1);
	}


	if (im_root) {
		if (setgid(gid) < 0) {
			PERROR(SUBSYS_DATA,"set_config: setgid");
			return(-1);
		}

		if (setuid(uid) < 0) {
			PERROR(SUBSYS_DATA,"set_config: setuid");
			return(-1);
		}
	}
	return(0);
} /* end of set_config() */

/* these 'accessor' functions are not necessary now that the iod_config 
 * structure is a global, but it's easier to leave them here for now */
int get_config_port(void)
{
	return(__iod_config.port);
}

int get_config_log_level(void)
{
	return(__iod_config.log_level);
}

char *get_config_logdir(void)
{
	return(__iod_config.logdir);
}

int get_config_access_size(void)
{
	return(__iod_config.access_size);
}

int get_config_write_buf(void)
{
	return(__iod_config.write_buf);
}

int get_config_socket_buf(void)
{
	return(__iod_config.socket_buf);
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
