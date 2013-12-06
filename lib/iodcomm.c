/*
 * (C) 2005 Penn State University
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */
#include <capfs-header.h>
#include <lib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sockio.h>
#include <stdlib.h>
#include <capfs_config.h>
#include <log.h>

#ifndef CAPFS_NR_OPEN
#define CAPFS_NR_OPEN (NR_OPEN)
#endif

extern fdesc_p pfds[];

/* IODINFO structure - this is used to keep up with all the IOD's in use
 *
 * DESCRIPTION:
 *
 * The idea here is to try to keep a single connection open to each
 * iod that we are communicating with.  To do this, we're going to
 * keep a little table and do our matching by comparing the port and 
 * address that we would use to connect to the manager.
 * We're going to vary dynamically the number of connections around.
 *
 */

/*
 * initial number of iod's for which we 
 * keep information about in this table
 */

enum {INITIAL_ENTRIES = 8, GROW_AMOUNT = 8};

/* Ahhhhh... the joys of single-threaded programming */
static int iodcnt = 0, initial_entries = 0, grow_amount = 0;
struct iodinfoitem {
	int      valid:1;
	unsigned short int port;
	unsigned int addr;
	int      fd;
	int 		ref_count;/* how many files are using this socket fd */
	pid_t    pid;
};
static struct iodinfoitem *iodinfo = NULL;

int init_iodtable(int _initial_entries, int _grow_amount)
{
	if(!iodinfo) {
		initial_entries = (_initial_entries <= 0) ? INITIAL_ENTRIES : _initial_entries;
		grow_amount 	 = (_grow_amount <= 0) ? GROW_AMOUNT : _grow_amount;
		iodinfo = (struct iodinfoitem *)malloc(initial_entries * sizeof(struct iodinfoitem));
		if(!iodinfo) {
			errno = ENOMEM;
			return -1;
		}
		memset(iodinfo, 0, initial_entries * sizeof(struct iodinfoitem));
		/* none of the entries are valid initially */
		return 0;
	}
	return -1;
}

void cleanup_iodtable(void)
{
	int i;
	if(!iodinfo) {
		return;
	}
	for(i=0;i < initial_entries; i++) {
		if(iodinfo[i].fd > 0) {
			close(iodinfo[i].fd);
		}
		iodinfo[i].fd = -1;
	}
	free(iodinfo);
	return;
}

static int grow_iodtable(void)
{
	size_t newsize;
	struct iodinfoitem *newiodinfo;
	newsize = (initial_entries + grow_amount) * sizeof(struct iodinfoitem);
	newiodinfo = (struct iodinfoitem *)malloc(newsize);
	if(!newiodinfo) {
		return -1;
	}
	memset(newiodinfo, 0, newsize);
	if(iodinfo) {
		memcpy(newiodinfo, iodinfo, initial_entries * sizeof(struct iodinfoitem));
		free(iodinfo);
	}
	iodinfo = newiodinfo;
	initial_entries += grow_amount;
	return 0;
}

static int search_iodtable(struct sockaddr *saddr_p, int *freeslot)
{
	int i;
	pid_t mypid = getpid();
	
	if(freeslot) {
		*freeslot = initial_entries;
	}
	for(i=0; i < initial_entries; i++) {
		/* valid entry */
		if(iodinfo[i].valid != 0) {
			/* if address and port matches we return the sock fd handle */
			if(iodinfo[i].addr == ((struct sockaddr_in *)saddr_p)->sin_addr.s_addr && iodinfo[i].port == ((struct sockaddr_in *)saddr_p)->sin_port &&
			   iodinfo[i].pid == mypid) {
				return i;
			}
		}
		/* invalid entry ==> means free slot also */
		else {
			if(freeslot) {
				if(*freeslot > i) {
					*freeslot = i;		  
				}
			}
		}
	}
	return -1;
}

/* Just return the slot for a particular host */
int find_iodtable(struct sockaddr *saddr_p)
{
	int i;
	pid_t mypid = getpid();
	if(!iodinfo) {
		errno = EINVAL;
		return -1;
	}
	for(i=0;i < initial_entries; i++) {
		if(iodinfo[i].valid != 0) {
			if(iodinfo[i].addr == ((struct sockaddr_in *)saddr_p)->sin_addr.s_addr && iodinfo[i].port == ((struct sockaddr_in *)saddr_p)->sin_port &&
			   iodinfo[i].pid == mypid) {
				return i;
			}
		}
	}
	return -1;
}

/* get the socket file descriptor for a given iod slot */
int getfd_iodtable(int slot)
{
	if(!iodinfo) {
		errno = EINVAL;
		return -1;
	}
	/* invalid slot index */
	if(slot < 0 || slot >= initial_entries) {
		errno = EINVAL;
		return -1;
	}
	/* valid slot */
	if(iodinfo[slot].valid != 0) {
		return iodinfo[slot].fd;
	}
	return -1;
}

/* given the socket file descriptor, what is the iod slot number */
int getslot_iodtable(int sockfd)
{
	int i;
	pid_t mypid = getpid();
	if(!iodinfo) {
		errno = EINVAL;
		return -1;
	}
	for(i=0; i < initial_entries; i++) {
		if(iodinfo[i].valid != 0) {
			if(iodinfo[i].fd == sockfd && iodinfo[i].pid == mypid) {
				return i;
			}
		}
	}
	return -1;
}

/* Just add the iod information if necessary. Dont try to connect. Return the slot information */
int instantiate_iod_entry(struct sockaddr *saddr_p)
{
	int slot, free_slot = initial_entries;
	if(!iodinfo) {
		if(init_iodtable(0,0) < 0) {
			errno = ENOMEM;
			return -1;
		}
	}
	/* Try to search for the host's slot in our table */
repeat:
	/* Did not find it in the table */
	if((slot = search_iodtable(saddr_p, &free_slot)) < 0) {
		if(free_slot < initial_entries) {
			iodinfo[free_slot].valid = 1;
			iodinfo[free_slot].addr = ((struct sockaddr_in *) saddr_p)->sin_addr.s_addr;
			iodinfo[free_slot].port = ((struct sockaddr_in *) saddr_p)->sin_port;
			iodinfo[free_slot].fd = -1;
			iodinfo[free_slot].pid = getpid();
			iodcnt++;
		}
		/* No free space in the table */
		else {
			if(grow_iodtable() < 0) {
				errno = ENOMEM;
				return -1;
			}
			goto repeat;
		}
		slot = free_slot;
	}
	return slot;
}


/* Either add iod information or retrieve the sock fd already stored. */
int add_iodtable(struct sockaddr *saddr_p)
{
	int free_slot = initial_entries, slot;
	if(!iodinfo) {
		if(init_iodtable(0,0) < 0) {
			errno = ENOMEM;
			return -1;
		}
	}
	/* Try to search for the host's slot in our table */
repeat:
	/* Did not find it in the table */
	if((slot = search_iodtable(saddr_p, &free_slot)) < 0) {
		if(free_slot < initial_entries) {
			iodinfo[free_slot].valid = 1;
			iodinfo[free_slot].addr = ((struct sockaddr_in *) saddr_p)->sin_addr.s_addr;
			iodinfo[free_slot].port = ((struct sockaddr_in *) saddr_p)->sin_port;
			iodinfo[free_slot].fd = -1;
			iodinfo[free_slot].pid = getpid();
			iodcnt++;
		}
		/* No free space in the table */
		else {
			if(grow_iodtable() < 0) {
				errno = ENOMEM;
				return -1;
			}
			goto repeat;
		}
		slot = free_slot;
	}
	/* connect if necessary */
	if(iodinfo[slot].fd < 0) {
		int fd, err;
		struct ireq req;
		struct iack ack;
		fd = iodinfo[slot].fd = new_sock();
		if(fd < 0) {
			return -1;
		}
		if(connect_timeout(fd, saddr_p, sizeof(struct sockaddr),IODCOMM_CONNECT_TIMEOUT_SECS) < 0) {
			close(fd);
			iodinfo[slot].fd = -1;
			return -1;
		}
		LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "Connected to 0x%x:%d on %d\n",((struct sockaddr_in *)saddr_p)->sin_addr.s_addr, ((struct sockaddr_in *)saddr_p)->sin_port, fd);
		req.majik_nr   = IOD_MAJIK_NR;
		req.release_nr = CAPFS_RELEASE_NR;
		req.type       = IOD_NOOP;
		req.dsize      = 0;
		err = bsend(fd, &req, sizeof(req));
		if (err < 0) {
			LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " unable to send request to 0x%x:%d on %d\n", ((struct sockaddr_in *)saddr_p)->sin_addr.s_addr, ((struct sockaddr_in *)saddr_p)->sin_port, fd);
			close(fd);
			iodinfo[slot].fd = -1;
			return -1;
		}
		err = brecv_timeout(fd, &ack, sizeof(ack), IODCOMM_BRECV_TIMEOUT_SECS);
		if(err < 0) {
			LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " unable to receive ack from 0x%x:%d on %d within timeout\n", ((struct sockaddr_in *)saddr_p)->sin_addr.s_addr, ((struct sockaddr_in *)saddr_p)->sin_port, fd);
			close(fd);
			iodinfo[slot].fd = -1;
			return -1;
		}
		if (ack.majik_nr != IOD_MAJIK_NR) {
			LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " Nonsense ack from 0x%x:%d on %d \n", ((struct sockaddr_in *)saddr_p)->sin_addr.s_addr, ((struct sockaddr_in *)saddr_p)->sin_port, fd);
			close(fd);
			iodinfo[slot].fd = -1;
			return -1;
		}
		if(ack.release_nr != CAPFS_RELEASE_NR) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_LIB,  
			    "release nr from 0x%x:%d on %d is (%d.%d.%d) does not match release nr built into "              "binary (%d.%d.%d)\n", 
			    ((struct sockaddr_in *)saddr_p)->sin_addr.s_addr, 
			     ((struct sockaddr_in *)saddr_p)->sin_port, 
			     fd, 
			     ack.release_nr / 10000, 
			     (ack.release_nr / 100) % 10, 
			     ack.release_nr % 10, 
			     CAPFS_RELEASE_NR / 10000, 
			     (CAPFS_RELEASE_NR / 100) % 10, 
			     CAPFS_RELEASE_NR % 10);
			close(fd);
			iodinfo[slot].fd = -1;
			return -1;
		}
		/* update fd table */
		if (pfds[fd]) {
			LOG(stderr, DEBUG_MSG, SUBSYS_LIB,  "fd %d already in use?!?\n", fd);
		}
		pfds[fd] = (fdesc_p)malloc(sizeof(fdesc));
		if(!pfds[fd]) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_LIB,  " Could not allocate memory\n");
			close(fd);
			iodinfo[slot].fd = -1;
			errno = ENOMEM;
			return -1;
		}
		memset(pfds[fd], 0, sizeof(fdesc));
		pfds[fd]->fs = FS_RESV;
		pfds[fd]->fd.ref = -1;
	}
	return(iodinfo[slot].fd);
}

int badiodfd(int fd)
{
	int i;
	if(!iodinfo) {
		errno = EINVAL;
		return -1;
	}
	for (i = 0; i < initial_entries; i++) {
		if (fd == iodinfo[i].fd) {
			iodinfo[i].ref_count = 0; /* anyways since the sock fd is closed, we can reset the ref count */
			close(fd);
			iodinfo[i].fd = -1;
			/* update fd table */
			if (!pfds[fd] || pfds[fd]->fs != FS_RESV) {
				errno = EINVAL;
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " fd %d was not reserved?!?\n", fd);
				return(-1);
			}

			/* The following two cases should never happen.  Getting here
				indicates memory corruption or the fdesc semantics being
				broken */
			if (pfds[fd]->part_p != NULL)
			{
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " Reserved fd %d had a part_p?!?\n", fd);
				free(pfds[fd]->part_p);
				pfds[fd]->part_p = NULL;
			}

			if (pfds[fd]->fn_p != NULL)
			{
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " Reserved fd %d had a fn_p?!?\n", fd);
				free(pfds[fd]->fn_p);
				pfds[fd]->fn_p = NULL;
			}

  			free(pfds[fd]);
			pfds[fd] = NULL;
			return 0;
		}
	}
	return -1;
}

/* This is really a stupid name.. */
int shutdown_alliods(void)
{
	int i;
	if(!iodinfo) {
		errno = EINVAL;
		return -1;
	}
	for(i=0;i < initial_entries; i++) {
		int fd;
		if(iodinfo[i].valid != 0 && ((fd=iodinfo[i].fd) >= 0)) {
			/* Do a sanity check */
			if(iodinfo[i].ref_count != 0) {
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " Hmm..slot %d had a non-zero(%d) ref count\n", i, iodinfo[i].ref_count);
			}
			close(fd);
			/* update fd table */
			if (!pfds[fd] || pfds[fd]->fs != FS_RESV) {
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " fd %d was not reserved?!?\n", fd);
				continue;
			}

			/* The following two cases should never happen.  Getting here
				indicates memory corruption or the fdesc semantics being
				broken */
			if (pfds[fd]->part_p != NULL)
			{
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " Reserved fd %d had a part_p?!?\n", fd);
				free(pfds[fd]->part_p);
				pfds[fd]->part_p = NULL;
			}

			if (pfds[fd]->fn_p != NULL)
			{
				LOG(stderr, WARNING_MSG, SUBSYS_LIB,  " Reserved fd %d had a fn_p?!?\n", fd);
				free(pfds[fd]->fn_p);
				pfds[fd]->fn_p = NULL;
			}

			free(pfds[fd]);
			pfds[fd] = NULL;
			iodinfo[i].fd = -1;
		}
	}
	return 0;
}

int inc_ref_count(int slot)
{
	if(!iodinfo) {
		errno = EINVAL;
		return -1;
	}
	if(slot < 0 || slot >= initial_entries) {
		errno = EINVAL;
		return -1;
	}
	/* valid slot */
	if(iodinfo[slot].valid != 0) {
		iodinfo[slot].ref_count++;
		return 0;
	}
	errno = EINVAL;
	return -1;
}

int dec_ref_count(int slot)
{
	if(!iodinfo) {
		errno = EINVAL;
		return -1;
	}
	if(slot < 0 || slot >= initial_entries) {
		errno = EINVAL;
		return -1;
	}
	/* valid slot */
	if(iodinfo[slot].valid != 0) {
		iodinfo[slot].ref_count--;
		return 0;
	}
	errno = EINVAL;
	return -1;
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
