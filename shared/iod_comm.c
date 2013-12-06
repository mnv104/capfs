#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <capfs_config.h>
#include <desc.h>
#include <sockio.h>
#include <req.h>
#include <log.h>

/* invalidate_conn() - closes an iod socket and removes references to it
 *
 * Returns -1 on obvious error, 0 on success.
 */
int invalidate_conn(iod_info *info_p)
{
	if (!info_p) {
		LOG(stderr, WARNING_MSG, SUBSYS_SHARED, "NULL iod_info pointer \n");
		return(-1);
	}
	close(info_p->sock);
	info_p->sock = -1;
	return(0);
}


/* send_req() - sends a request to a number of iods
 * iod[]  - pointer to array of iod_info structures
 * iods   - number of iods in array
 * base   - base number for file
 * pcount - number of iods on which the file resides
 * req_p  - pointer to request to send to each iod
 *        this may be modified by send_req() to update values in request
 *        such as the pnum value in an OPEN request
 * data_p - pointer to any request trailing data to be sent; size is
 *        stored in req_p->dsize; may be NULL
 * ack_p  - pointer to iack structure; NOT USED...
 *
 * Returns 0 on success, non-zero on failure.
 */
int send_req(iod_info iod[], int iods, int base, int pcount, ireq_p req_p, 
				void *data_p, iack_p ack_p)
{
	int ret, i;
	int cnt=base, errs=0;

	if (!iod || !req_p) {
		LOG(stderr, WARNING_MSG, SUBSYS_SHARED, "NULL iod or req_p pointer\n");
		errno = EINVAL;
		return(-1);
	}

	if (base >= iods || pcount > iods || iods < 1) {
		LOG(stderr, WARNING_MSG, SUBSYS_SHARED, "Invalid value (base, pcount, or nr_iods)\n");
		errno = EINVAL;
		return(-1);
	}

	for (i = 0; i < pcount;
		cnt = (cnt + 1)%iods, 
		(req_p->type == IOD_OPEN) ? req_p->req.open.pnum++ : 1, /* ugly!  */
		(req_p->type == IOD_TRUNCATE) ? req_p->req.truncate.part_nr++ : 1,
		i++)
	{
		/* clear errno */
		iod[cnt].ack.eno = 0;
		iod[cnt].ack.status = 0;

		if (iod[cnt].sock < 0) /* open the connection first */ {
			/* get socket, connect */
			if ((iod[cnt].sock = new_sock()) == -1) {
				iod[cnt].ack.eno = errno;
				iod[cnt].ack.status = -1;
				PERROR(SUBSYS_SHARED, "new_sock"); 
				errs++;
				continue;
			}
#ifdef ENABLE_TRUSTED_PORTS
			/* bind to a privileged port */
			if (bind_sock(iod[cnt].sock, -1) < 0) {
				iod[cnt].ack.eno = errno;
				iod[cnt].ack.status = -1;
            LOG(stderr, WARNING_MSG, SUBSYS_SHARED, "error binding port to connect to iod %d (%s:%d)\n", cnt,
                inet_ntoa(iod[cnt].addr.sin_addr), 
                ntohs(iod[cnt].addr.sin_port));
				invalidate_conn(&iod[cnt]);
				errs++;
				continue;
			}
#endif
			/* connect */
			if (connect(iod[cnt].sock, (struct sockaddr *)&(iod[cnt].addr),
				sizeof(iod[cnt].addr)) < 0)
			{
				iod[cnt].ack.eno = errno;
				iod[cnt].ack.status = -1;
            LOG(stderr, WARNING_MSG, SUBSYS_SHARED, "error connecting to iod %d (%s:%d)\n", cnt,
                inet_ntoa(iod[cnt].addr.sin_addr), 
                ntohs(iod[cnt].addr.sin_port));
				invalidate_conn(&iod[cnt]);
				errs++;
				continue;
			}	
		}
		if ((ret = bsend(iod[cnt].sock, req_p, sizeof(ireq))) < 0) {
			/* error sending request */
			iod[cnt].ack.eno = errno;
			iod[cnt].ack.status = -1;
         LOG(stderr, WARNING_MSG, SUBSYS_SHARED, "error sending request to iod %d (%s:%d)\n", cnt,
             inet_ntoa(iod[cnt].addr.sin_addr), 
             ntohs(iod[cnt].addr.sin_port));
			invalidate_conn(&iod[cnt]);
			errs++;
			continue;
		}
		if (req_p->dsize > 0 && data_p
		&& (ret = bsend(iod[cnt].sock, data_p, req_p->dsize)) < 0) {
			/* error sending trailing data */
			iod[cnt].ack.eno = errno;
			iod[cnt].ack.status = -1;
         LOG(stderr, WARNING_MSG, SUBSYS_SHARED, "error sending trailing data to iod %d (%s:%d)\n", cnt,
             inet_ntoa(iod[cnt].addr.sin_addr), 
             ntohs(iod[cnt].addr.sin_port));
			invalidate_conn(&iod[cnt]);
			errs++;
			continue;
		}
	} /* end of forall iods */

	for (i = 0, cnt=base; i < pcount; cnt=(cnt+1)%iods, i++)
	{
		/* timeout if the ack doesn't come back relatively quickly */
		if ((ret = brecv_timeout(iod[cnt].sock, &(iod[cnt].ack), sizeof(iack), 
			REQUEST_BRECV_TIMEOUT_SECS)) < (int)sizeof(iack)) {
			/* error receiving ack */
			iod[cnt].ack.eno = errno;
			iod[cnt].ack.status = -1;
			PERROR(SUBSYS_SHARED, "brecv_timeout");
         LOG(stderr, WARNING_MSG, SUBSYS_SHARED, "error receiving ack from iod %d (%s:%d)\n", cnt,
             inet_ntoa(iod[cnt].addr.sin_addr), 
             ntohs(iod[cnt].addr.sin_port));
			invalidate_conn(&iod[cnt]);
			errs++;
			continue;
		}
   }

	for (i=0, cnt=base; i < iods; i++, cnt=(cnt+1)%iods) {
		if (iod[cnt].ack.status) {
			LOG(stderr, WARNING_MSG, SUBSYS_SHARED, "error received from iod %d: %s\n", cnt,
				strerror(iod[cnt].ack.eno));
			errno = iod[cnt].ack.eno;
			return(iod[cnt].ack.status);
		}
	}
	if (errs) {
		errno = EIO;
		return(-1);
	}
	return(0);
} /* end of send_req() */


