/*
 * mount.capfs.c copyright (c) 1997 Clemson University, all rights reserved.
 *
 * Modified for CAPFS by Murali Vilayannur (vilayann@cse.psu.edu). 
 *
 * Written by Rob Ross, based somewhat on nfs mount code by Rick Sladkey
 * <jrs@world.std.com>.
 *
 * Modified by Dan Nurmi <nurmi@mcs.anl.gov> to get /etc/mtab right.
 * Option parsing code based somewhat on smb mount code.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Contact:  Murali Vilayannur (vilayann@cse.psu.edu)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/mount.h>
#include <sys/poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <mntent.h>
#include <sys/time.h>
#include <fcntl.h>
#include <rpc/rpc.h>
#include <stdio.h>
#include <rpc/xdr.h>
#include <errno.h>
#include <sys/utsname.h>
#include "capfs_mount.h"
#include "capfs_config.h"
#include "mgr_prot.h"

#define debug 0
#define dprintf(args...) if (debug) printf(args);

struct capfs_mount_clargs {
	char *hostname;
	char *dirname;
	char *special;
	char *mountpoint;
	char *amode;
	int port;
	int tcp;
	int intr;
	char *cons;
	int hcache;
	int dcache;
};

static int usage(int argc, char **argv);
static int do_mtab(struct mntent *);
static int setup_mntent(struct mntent *, const char *, const char *, 
const char *, const char *, int, int);
static int parse_args(int argc, char *argv[], struct capfs_mount_clargs *);
static int resolve_addr(char *name, struct in_addr *addr);

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
	}
	clnt_destroy(clnt);
	return ret;
}

static inline int check_kernel_version(struct utsname *utsbuf)
{
	char *ptr = strchr(utsbuf->release, '.');
	if (ptr == NULL)
	{
		return -1;
	}
	ptr++;
	if (*ptr != '4' && *ptr != '6')
	{
		return -1;
	}
	return (*ptr == '4') ? 0 : 1;
}

int main(int argc, char **argv)
{
	int ret, flags = 0;
	char type[] = "capfs";
	char *host_p;
	struct capfs_mount data;
	struct capfs_mount_clargs clargs;
	struct mntent myment;
	struct in_addr haddr;
	struct stat sb, mntpt;
	struct utsname  utsbuf;
	int is26 = 0;
  
	if (uname(&utsbuf) < 0) {
		fprintf(stderr, "mount.capfs: uname failed %s\n", strerror(errno));
		exit(1);
	}
	if ((is26 = check_kernel_version(&utsbuf)) < 0)
	{
		fprintf(stderr, "mount.capfs: Unrecognized kernel version %s\n", utsbuf.release);
		exit(1);
	}
	/* gobble up some memory */
	clargs.hostname = malloc(sizeof(char[CAPFS_MGRLEN]));
	clargs.dirname = malloc(sizeof(char[PATH_MAX]));
	clargs.special = malloc(sizeof(char[CAPFS_MGRLEN + PATH_MAX + 1]));
	clargs.mountpoint = malloc(sizeof(char[PATH_MAX]));
	clargs.amode = malloc(sizeof(char[3]));
	clargs.cons = malloc(sizeof(char[CAPFS_CONSLEN]));
	/* port number is a legacy thing. We just set it to 0 always */
	clargs.port = 0;
	clargs.intr = 0;

	if (argc < 3) {
		fprintf(stderr, "mount.capfs: too few arguments\n");
		usage(argc, argv);
		return(1);
	}

	if ((ret = parse_args(argc, argv, &clargs))) {
		fprintf(stderr, "ERROR: problem parsing cmdline args\n");
		return(ret);
	}

	/* look up the hostname (could use DNS) */
	if ((ret = resolve_addr(clargs.hostname, &haddr))) {
		return(ret);
	}

	/* convert address back to dotted decimal notation to pass into kernel */
	host_p = inet_ntoa(haddr);
	strncpy(clargs.hostname, host_p, CAPFS_MGRLEN);

	dprintf("hostname after transform | hostname=%s\n", clargs.hostname);

	data.info_magic = 0;
	data.flags   = 0;
	data.port = clargs.port;
	data.tcp = clargs.tcp;
	/* consistency semantics string */
	strncpy(data.cons, clargs.cons, CAPFS_CONSLEN);
	strncpy(data.mgr, clargs.hostname, CAPFS_MGRLEN);
	strncpy(data.dir,  clargs.dirname,  CAPFS_DIRLEN);
	/* Add the intr flag */
	if (clargs.intr == 1) {
		data.flags |= CAPFS_MOUNT_INTR;
	}
	/* enable hcache */
	if (clargs.hcache == 1) {
		data.flags |= CAPFS_MOUNT_HCACHE;
	}
	if (clargs.dcache == 1) {
		data.flags |= CAPFS_MOUNT_DCACHE;
	}
	/* Enable tcp by default */
	data.flags |= CAPFS_MOUNT_TCP;
	if (clargs.tcp == 0) {
		/* Undo it if udp was asked */
		data.flags &= ~CAPFS_MOUNT_TCP;
	}
	

	flags |= 0xC0ED0000;

	if (strcmp(clargs.amode, "ro") == 0)
	{
		flags |= MS_RDONLY;
	}
	/* Ugly kludge necessary for FC3 kernels. This is a seriously weird kernel behavior */
	if (is26)
	{
		flags |= data.flags;
		sprintf(clargs.special, "%s:%d_%s:%s", data.mgr, data.flags, data.cons, data.dir);
	}
	
	/* verify that local mountpoint exists and is a directory */
	if (stat(clargs.mountpoint, &mntpt) < 0 || !S_ISDIR(mntpt.st_mode)) {
		fprintf(stderr, "mount.capfs: invalid mount point %s (should be a local directory)\n",
				  clargs.mountpoint);
		return 1;
	}

	/* ping the manager before trying to mount */
	if (data.tcp == 1) {
		if (ping_tcp(clargs.hostname, CAPFS_MGR, mgrv1, 21) != 0) {
			fprintf(stderr, "mount.capfs: mgr server %s not responding on tcp service"
					"giving up\n", data.mgr);
			return 1;
		}
	}
	else {
		if (ping_udp(clargs.hostname, CAPFS_MGR, mgrv1, 21) != 0) {
			fprintf(stderr, "mount.capfs: mgr server %s not responding on udp service"
					"giving up\n", data.mgr);
			return 1;
		}
	}
	dprintf("calling mount(special = %s, mountpoint = %s, type = %s, tcp = %s cons = %s"
			  " flags = %x, data.mgr = %s, data.cons = %s, data.dir = %s)\n", 
			  clargs.special, clargs.mountpoint, type, (data.tcp == 1) ? "tcp ": "udp ",
			  data.cons, flags, data.mgr, data.cons, data.dir);
	if ((ret = mount(clargs.special, clargs.mountpoint, type, flags, (void *) &data)) < 0)
	{
		perror("mount.capfs");
		fprintf(stderr, "mount.capfs: server %s alive, but mount failed (invalid metadata directory name? invalid consistency policy?)\n",
				  data.mgr);
		return (ret);
	}

	if ((ret = setup_mntent(&myment, clargs.special, clargs.mountpoint, type, clargs.amode, 0, 0))) {
		return(ret);
	}

   /* Leave mtab alone if it is a link */
	if (lstat(MTAB, &sb) == 0 && S_ISLNK(sb.st_mode)) return ret;
	
	ret = do_mtab(&myment);

	return(ret);
}

/* 
 * parse_args() 
 *
 * function takes argc/argv pair and a pointer to a struct capfs_mount_clargs.
 * The struct contains variables that need to be filled with information from
 * the commandline to be later used back in main().  
 */
static int parse_args(int argc, char *argv[], 
struct capfs_mount_clargs *in_clargs) 
{
	int opt, count;
	char *pindex,
		*mopts = malloc(sizeof(char[255])),
		*hostdir = NULL;

	/* Set default values */
	in_clargs->tcp = 1;
	strcpy(in_clargs->amode, "rw");
	/* disable the hcache and dcache */
	in_clargs->dcache = 0;
	in_clargs->hcache = 0;
	strncpy(in_clargs->cons, "posix", 5);
  
	/* Start parsage */
	while ((opt = getopt (argc, argv, "o:")) != EOF)
	{
		switch (opt)
		{
		case 'o':
			strcpy(mopts, optarg);
			count = 0;

			pindex = (char *)strtok(mopts, ",");
			while(pindex != NULL) {
				/* 
				 * Start ifelse that sets internal variables 
				 * equal to commandline arguments of the form 
				 * 'val' 
				 */
				if (!strcmp(pindex, "rw")) {
					strcpy(in_clargs->amode, "rw");
				} 
				else if (!strcmp(pindex, "ro")) {
					strcpy(in_clargs->amode, "ro");
				}
				else if (!strcmp(pindex, "intr")) {
					in_clargs->intr = 1;
				}
				else if (!strcmp(pindex, "udp")) {
					in_clargs->tcp = 0;
				}
				else if (!strcmp(pindex, "tcp")) {
					in_clargs->tcp = 1;
				}
				else if (!strcmp(pindex, "hcache")) {
					in_clargs->hcache = 1;
				}
				else if (!strcmp(pindex, "dcache")) {
					in_clargs->dcache = 1;
				}
				else if (!strncmp(pindex, "cons=", 5)) {
					char *subopt = NULL, *cons_policy = NULL;
					subopt = strchr(pindex, '=');
					cons_policy = subopt + 1;
					/* we are just copying consistency policy here. Actual checking for support is done internally at mount time */
					strncpy(in_clargs->cons, cons_policy, CAPFS_CONSLEN);
				}
				pindex = (char *) strtok(NULL, ",");
			}
			break;
		default:
			free(mopts);
			return 1;
		}
	}

	if(optind != (argc-2))
	{
		fprintf(stderr, "mount.capfs: argument format is incorrect.\n");
		usage(argc, argv);
		return(1);
	}

	/* separate hostname and dirname from 'hostname:dirname' format */
	hostdir = malloc(sizeof(char[strlen(argv[optind])+1]));
	strcpy(hostdir, argv[optind]);
	if ((pindex = (strchr(hostdir, ':')))) {
		*pindex = '\0';

		strcpy(in_clargs->hostname, hostdir);

		pindex++;
		strcpy(in_clargs->dirname, pindex);

	}  else /* not good */ {
		fprintf(stderr, 
				  "mount.capfs: directory to mount not in host:dir format\n");
		free(mopts);
		free(hostdir);
		return(1);
	}

	strcpy(in_clargs->special, argv[optind]);
	strcpy(in_clargs->mountpoint, argv[optind+1]);


	free(mopts);
	free(hostdir);

	dprintf("Parsed Args: hostname=%s, remote_dirname=%s, local_mountpoint=%s, special=%s, amode=%s\n",
			in_clargs->hostname, in_clargs->dirname, in_clargs->mountpoint, in_clargs->special, in_clargs->amode);
	return 0;
}

static int usage(int argc, char **argv)
{
	fprintf(stderr, "mount.capfs: [-o <options>] mgr_host:metadata_dir directory\n");
	fprintf(stderr, "Recognized <options> are rw/ro/intr/udp/tcp/cons=<posix|session|imm|trans|force|pvfs>/hcache/dcache\n");
	return 0;
}

/* do_mtab - Given a pointer to a filled struct mntent,
 * add an entry to /etc/mtab.
 *
 */
static int do_mtab(struct mntent *myment) 
{
	struct mntent *ment;
	FILE *mtab;
	FILE *tmp_mtab;

	mtab = setmntent(MTAB, "r");
	tmp_mtab = setmntent(TMP_MTAB, "w");

	if (mtab == NULL) {
		fprintf(stderr, "ERROR: couldn't open "MTAB" for read\n");
		endmntent(mtab);
		endmntent(tmp_mtab);
		return 1;
	} else if (tmp_mtab == NULL) {
		fprintf(stderr, "ERROR: couldn't open "TMP_MTAB" for write\n");
		endmntent(mtab);
		endmntent(tmp_mtab);
		return 1;
	}

	while((ment = getmntent(mtab)) != NULL) {
		if (strcmp(myment->mnt_dir, ment->mnt_dir) != 0) {
			if (addmntent(tmp_mtab, ment) == 1) {
				fprintf(stderr, "ERROR: couldn't add entry to"TMP_MTAB"\n");
				endmntent(mtab);
				endmntent(tmp_mtab);
				return 1;
			}
		}
	}

	endmntent(mtab);

	if (addmntent(tmp_mtab, myment) == 1) {
		fprintf(stderr, "ERROR: couldn't add entry to "TMP_MTAB"\n");
		return 1;
	}

	endmntent(tmp_mtab);

	if (rename(TMP_MTAB, MTAB)) {
		fprintf(stderr, "ERROR: couldn't rename "TMP_MTAB" to "MTAB"\n");
		return 1;
	}

	if (chmod(MTAB, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) < 0) {
		if (errno != EROFS) {
			int errsv = errno;
			fprintf(stderr, "mount: error changing mode of %s: %s",
					  MTAB, strerror (errsv));
		}
		return(1);
	}
	return 0;
} /* end of do_mtab */


/* setup_mntent - allocate space for members of mntent, fill in the data
 * structure
 */
static int setup_mntent(struct mntent *myment, const char *fsname, 
const char *dir, const char *type, const char *opts, int freq, int passno)
{
	myment->mnt_fsname = malloc(sizeof(char[strlen(fsname)+1]));
	myment->mnt_dir = malloc(sizeof(char[strlen(dir)+1]));
	myment->mnt_type = malloc(sizeof(char[strlen(type)+1]));
	myment->mnt_opts = malloc(sizeof(char[strlen(opts)+1]));

	if ((myment->mnt_fsname == NULL) || ( myment->mnt_dir == NULL)
		 || (myment->mnt_type == NULL) || (myment->mnt_opts == NULL))
	{
		fprintf(stderr, "ERROR: cannot allocate memory\n");
		return 1;
	}

	strcpy(myment->mnt_fsname, fsname);
	strcpy(myment->mnt_dir, dir);
	strcpy(myment->mnt_type, type);
	strcpy(myment->mnt_opts, opts);
	myment->mnt_freq = freq;
	myment->mnt_passno = passno;
  
	return 0;
} /* end setup_mntent */

/* resolve_addr() 
 *
 * returns struct in_addr holding address of a host specified by the character
 * string name, which can either be a host name or IP address in dot notation.
 */
static int resolve_addr(char *name, struct in_addr *addr)
{
   struct hostent *hep;

   if (!inet_aton(name, addr)) {
		if (!(hep = gethostbyname(name))) {
			fprintf(stderr, "ERROR: cannot resolve remote hostname\n");
			return(1);
		} else {
			bcopy(hep->h_addr, (char *)addr, hep->h_length);
			return(0);
		}
   }
   return(0);
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
