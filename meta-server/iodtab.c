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
 *
 * Separate functions are used for reading the configuration file and
 * setting up the resulting environment.
 *
*/

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iodtab.h>
#include <arpa/inet.h>
#include <capfs_config.h>
#include <log.h> 

#define INBUFSZ 1024

static struct iodtabinfo iods;

struct iodtabinfo *parse_iodtab(char *fname)
{
	FILE *cfile;
	char inbuf[INBUFSZ], *entry, *port;
	struct hostent *hep;

	/* open file */
	if (!(cfile = fopen(fname, "r"))) {
		PERROR(SUBSYS_META,"fopen");
		return(NULL);
	}

	iods.nodecount = 0;

	while (fgets(inbuf, INBUFSZ, cfile)) {
		if (iods.nodecount >= CAPFS_MAXIODS) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_META, "CAPFS_MAXIODS exceeded!\n");
			return(NULL);
		}
		/* standard comments get skipped here */
		if (*inbuf == '#') continue;

		/* blank lines get skipped here */
		if (!(entry = strtok(inbuf, "#\n"))) continue;

		for (port = entry; *port && *port != ':'; port++);
		if (*port == ':') /* port number present */ {
			char *err;
			int portnr;

			portnr = strtol(port+1, &err, 10);
			if (err == port+1) /* ack, bad port */ {
				LOG(stderr, CRITICAL_MSG, SUBSYS_META, "parse_iodtab: bad port\n");
				return(NULL);
			}
			iods.iod[iods.nodecount].sin_port = htons(portnr);
		}
		else /* use default port number */ {
			iods.iod[iods.nodecount].sin_port = htons(IOD_REQ_PORT);
		}
		*port = 0;
		if (!inet_aton(entry, &iods.iod[iods.nodecount].sin_addr)) {
			if (!(hep=gethostbyname(entry)))
				bzero((char *)&iods.iod[iods.nodecount].sin_addr,
					sizeof(struct in_addr));
			else
				bcopy(hep->h_addr,(char *)&iods.iod[iods.nodecount].sin_addr,
					hep->h_length);
		}
		iods.iod[iods.nodecount].sin_family = AF_INET;
		iods.nodecount++;
	}
	fclose(cfile);
	return(&iods);
} /* end of parse_config() */

int dump_iodtab(FILE *fp)
{
	int i;
	char *outp;

	fprintf(fp, "# IODTAB FILE -- AUTOMATICALLY GENERATED\n");
	for (i=0; i < iods.nodecount; i++) {
		outp = inet_ntoa(iods.iod[i].sin_addr);
		fprintf(fp, "%s:%d\n", outp, ntohs(iods.iod[i].sin_port));
	}
	return(0);
} /* end of dump_config() */

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
