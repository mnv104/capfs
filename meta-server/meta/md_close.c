/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


/*This file contains the close metadata file call for the CAPFS*/
/*File assumes request has been allocated */

#include "capfs-header.h"
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <linux/types.h>
#include <sys/timeb.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <meta.h>
#include <req.h>
#include "metaio.h"
#include <log.h>

#ifdef MGR_USE_CACHED_FILE_SIZE

#include <fslist.h>
int md_close(mreq_p req, char *fname, fsinfo_p fs_p)
#else 
int md_close(mreq_p req, char *fname)
#endif
{
	time_t ctime;
	fmeta meta;
	int fd;

#ifdef MGR_USE_CACHED_FILE_SIZE
	int ret, i;
	ireq iodreq;
	
	/* variables used for calculating correct file size */
	uint64_t real_file_sz, tmp_file_sz, iod_file_sz;
	uint64_t strip_sz, stripe_sz, nr_strips, leftovers;
#endif	

	/* get fd for meta file */
	if ((fd = meta_open(fname, O_RDWR)) < 0) {
		PERROR(SUBSYS_META,"md_close: meta_open");
		return(-1);
	}
	
	/* get metadata */
	if (meta_read(fd, &meta) < 0) {
		PERROR(SUBSYS_META,"md_close: meta_read");
		return(-1);
	}

	/* seek back to position 0 */
	if (lseek(fd, 0, SEEK_SET) < 0) return(-1);

	/* get current time for meta stats */
	if ((ctime = time(NULL)) < 0) {
		PERROR(SUBSYS_META,"md_close: time");
		return(-1);
	}

	/* update the modification and access times to whatever the mgr tells
	 * us
	 */
	meta.u_stat.atime = req->req.close.meta.u_stat.atime;
	meta.u_stat.mtime = req->req.close.meta.u_stat.mtime;
	/* don't change the creation time. */

#ifdef MGR_USE_CACHED_FILE_SIZE
	if (S_ISREG(meta.u_stat.st_mode)){
		iodreq.majik_nr       = IOD_MAJIK_NR;
		iodreq.release_nr     = CAPFS_RELEASE_NR;
		iodreq.type 			 = IOD_STAT;
		iodreq.dsize 			 = 0;
		iodreq.req.stat.f_ino = meta.u_stat.st_ino;
		
		if ((ret = send_req(fs_p->iod, fs_p->nr_iods, 
								  meta.p_stat.base, meta.p_stat.pcount, 
								  &iodreq, fname, NULL)) < 0)
			{
				PERROR(SUBSYS_META,"md_close: send_req");
				return -1;
			}	
		
			strip_sz = meta.p_stat.ssize;
			stripe_sz = meta.p_stat.ssize * 
				meta.p_stat.pcount;
			real_file_sz = 0;
			
			for (i = 0; i < meta.p_stat.pcount; i++) {
				/* i gives us the index into the iods, the ith iod in order */
				iod_file_sz = fs_p->iod[(i + meta.p_stat.base) %
												fs_p->nr_iods].ack.ack.stat.fsize;
				if (iod_file_sz == 0) continue;
				
					/* the plan is to calculate the file size based on what
					 * this particular iod has.  the largest of these sizes
					 * is the actual file size, taking into account sparse
					 * issues.
					 *
					 * there are four components to the calculation:
					 *
					 * soff = should be 0 since it isn't implemented :)
					 *
					 * nr_strips * stripe_sz = this is a calculation of how many
					 *   complete stripes are present.  note that any partial strip
					 *   or the last complete strip (if there are no partials)
					 *   may not be part of a whole stripe (thus the if() below).
					 *
					 * i * strip_sz = this accounts for full strips that should be
					 *   present on iods that come before us in the iod ordering
					 *
					 * leftovers = the last remaining bytes
					 */
				nr_strips = iod_file_sz / strip_sz;
				leftovers = iod_file_sz % strip_sz;
				
				if (leftovers == 0) {
					nr_strips--;
					leftovers += strip_sz;
				}
				tmp_file_sz = nr_strips * stripe_sz + i * strip_sz +
					leftovers;
				if (tmp_file_sz > real_file_sz) real_file_sz = tmp_file_sz;
			}
			
			/* we're adding a 64-bit size field in addition to the st_size 
			 * later i'll remove the addition into st_size probably
			 * i'm leaving it for now
			 */
				
				
			meta.u_stat.st_size = real_file_sz;
	}

#endif
	
	/* Write metadata back */
	if (meta_write(fd, &meta) < 0) {
		PERROR(SUBSYS_META,"md_close: meta_write");
		return(-1);
	}	

/* Close metadata file */
	if (meta_close(fd) < 0) {
		PERROR(SUBSYS_META, "File close");
		return(-1);
	}
	
	return (0);

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
