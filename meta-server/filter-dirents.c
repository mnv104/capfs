/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/types.h>
#include <linux/dirent.h>
#include <capfs_config.h>
#include <log.h>

int resv_name(char *);

int print_dirents(void *buf, size_t len)
{
	struct dirent *cur;
	size_t off_end_reclen;
	struct dirent unused;


	off_end_reclen = ((char *)&(unused.d_reclen) - (char *)&unused)
	+ sizeof(unused.d_reclen);

	cur = (struct dirent *)buf;

	while (1) {
		if ((char *)cur + off_end_reclen > (char *)buf + len) return 0;
		if ((char *)cur + cur->d_reclen > (char *)buf + len) return 0;

		LOG(stderr, DEBUG_MSG, SUBSYS_META, "n = %s, i = %ld, o = %ld, l = %d\n", 
		    cur->d_name, cur->d_ino, cur->d_off, cur->d_reclen);

		cur = (struct dirent *) ((char *) cur + cur->d_reclen);
	}
}

/* filter_dirents()
 *
 * Returns pointer to last complete entry in the buffer if there were
 * any that weren't filtered, or NULL if all entries were filtered.
 */
void *filter_dirents(void *buf, size_t buflen)
{
	struct dirent *cur, *next, *last = NULL;
	size_t off_end_reclen, filterlen;
	struct dirent unused;

	off_end_reclen = ((char *)&(unused.d_reclen) - (char *)&unused)
	+ sizeof(unused.d_reclen);


	cur = (struct dirent *) buf;

	while (1) {
		/* verify that we have the complete current dirent, if not return a
		 * pointer to the last complete entry we have worked with (or NULL)
		 */
		if ((char *)cur + off_end_reclen > (char *)buf + buflen) return last;
		if ((char *)cur + cur->d_reclen > (char *)buf + buflen) return last;

		if (!resv_name(cur->d_name)) {
			/* keep this one, increment current pointer */
			last = cur;
			cur = (struct dirent *) ((char *) cur + last->d_reclen);
		}
		else {
			/* update the offset in the last entry */
			if (last) last->d_off = cur->d_off;

			/* filter out, reduce buffer length value */
			next = (struct dirent *) ((char *) cur + cur->d_reclen);
			filterlen = cur->d_reclen;
			/* make sure we have all of the next entry */
			if ((char *)next + off_end_reclen > (char *)buf + buflen) return last;
			if ((char *)next + cur->d_reclen > (char *)buf + buflen) return last;

			/* we have the next entry */
			memmove((char *)cur, (char *)next,
				buflen - (size_t) ((char *)next - (char *)buf));
			buflen -= filterlen;
		}
	}
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
