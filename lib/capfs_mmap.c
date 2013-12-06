/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */

#include <lib.h>

#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/mman.h>

/* MAP_ANONYMOUS - not defined in IRIX */
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0
#endif

extern fdesc_p pfds[];

struct mmap_info {
	void *start;
	size_t length;
	int prot, flags, fd, offset;
};

static struct mmap_info *mmap_list = 0;
static int mmap_cnt = 0;
static int mmap_max = 0;
#define NR_MI_MALLOC 4

/* CAPFS_MMAP() - tries its best to handle mmapping CAPFS files and hands
 * off all other requests to the real system call
 */
void *capfs_mmap(void *start, size_t length, int prot, int flags, int fd,
	off_t offset)
{
	void *addr;
	int saved_off;

	/* three cases here where we call the system call:
	 * 1) the file descriptor doesn't make any sense to us
	 * 2) the file is obviously a UNIX file (via fs field)
	 * 3) an anonymous mapping is requested
	 */
	if (fd < 0 || fd >= CAPFS_NR_OPEN
	    || !pfds[fd] || pfds[fd]->fs==FS_UNIX
	    || pfds[fd]->fs == FS_PDIR
	    || (flags & MAP_ANONYMOUS))
	{

		return mmap(start, length, prot, flags, fd, offset);
	}

	/* don't map our reserved fds (they're sockets anyway <grin>) */
	if (pfds[fd]->fs == FS_RESV) {
		errno = EBADF;
		return((void *)-1);
	}
	
	/* we don't do MAP_SHARED, only MAP_PRIVATE */
	if ((flags & MAP_SHARED) || !(flags & MAP_PRIVATE)) {
		errno = EINVAL;
		return((void *)-1);
	}

	/* emulate private mmap() for a CAPFS file */
	/* build up anonymous mmap args to hold data */
	if ((addr = mmap(start, length, prot | PROT_WRITE, MAP_ANONYMOUS |
	MAP_PRIVATE | (flags & MAP_FIXED), -1, 0)) == (void *)-1) {
		return((void *)-1);
	}

	/* copy data into the mmapped region */

	/* we're going to use the CAPFS calls here even though it's a little
	 * less efficient so that this function isn't so dependent on our
	 * file descriptor info...
	 */
	/* save file offset, read in data from requested offset!, reset offset */
	if (((saved_off = capfs_lseek(fd, 0, SEEK_CUR)) < 0)
	|| (capfs_lseek(fd, offset, SEEK_SET) < 0)
	|| (capfs_read(fd, addr, length) < 0)
	|| (capfs_lseek(fd, saved_off, SEEK_SET) < 0))
	{
		munmap(addr, length); /* dump our region */
		errno = EBADF;
		return((void *)-1);
	}
	
	/* if PROT_WRITE wasn't in prot, redo the protection on the region */
	if (!(prot & PROT_WRITE)
	&& mprotect(addr, length, prot) < 0)
	{
		munmap(addr, length); /* dump our region */
		errno = EINVAL; /* I guess... */
		return((void *)-1);
	}

	/* save info on this mapping */
	if (!mmap_list) /* haven't allocated space yet */ {
		if (!(mmap_list = (struct mmap_info *)malloc(sizeof(struct
		mmap_info) * NR_MI_MALLOC)))
		{
			munmap(addr, length);
			errno = ENOMEM;
			return((void *)-1);
		}
		mmap_cnt=0;
		mmap_max=NR_MI_MALLOC;
	}
	if (mmap_cnt == mmap_max) /* need more space */ {
		if (!(mmap_list = (struct mmap_info *)malloc(sizeof(struct
		mmap_info) * (mmap_max + NR_MI_MALLOC))))
		{
			munmap(addr, length);
			errno = ENOMEM;
			return((void *)-1);
		}
		mmap_max+=NR_MI_MALLOC;
	}
	mmap_list[mmap_cnt].start  = addr;
	mmap_list[mmap_cnt].length = length;
	mmap_list[mmap_cnt].prot   = prot;
	mmap_list[mmap_cnt].flags  = flags;
	mmap_list[mmap_cnt].fd     = fd;
	mmap_list[mmap_cnt].offset = offset;
	mmap_cnt++;

	return(addr);
}

int capfs_munmap(void *start, size_t length)
{
	int i, ret;

	if (!mmap_list) /* we haven't mmapped anything, hand off to syscall */ {
		return munmap(start, length);
	}

	/* search our list of mmapped regions */
	for (i=0; i < mmap_cnt && mmap_list[i].start != start; i++);

	if (i == mmap_cnt) /* not in our list, hand off to syscall */ {
		return munmap(start, length);
	}

	if (mmap_list[i].length != length) {
		LOG(stderr, WARNING_MSG, SUBSYS_LIB,  "capfs_munmap: length value mismatch\n");
	}
	ret = munmap(mmap_list[i].start, mmap_list[i].length);

	/* remove entry from list of regions */
	if (i == (mmap_cnt-1)) /* last one */ mmap_cnt--;
	else /* not last one */ mmap_list[i] = mmap_list[--mmap_cnt];
	return(ret);
}

void *capfs_mremap(void *old_address, size_t old_size, size_t new_size,
	unsigned long flags)
{
	int i;

	if (!mmap_list) {
		return (void *) mremap(old_address, old_size, new_size,
			flags);
	}

	/* search our list of mmapped regions */
	for (i=0; i < mmap_cnt && mmap_list[i].start != old_address; i++);

	if (i == mmap_cnt) /* not in our list, hand off to syscall */ {
		return (void *) mremap(old_address, old_size, new_size, flags);
	}

	errno = ENOSYS;
	return((void *)-1);
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



