/*
 * (C) 2005 Penn State University 
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#ifndef CAPFS_CONFIG_H
#define CAPFS_CONFIG_H

/* CAPFS_CONFIG.H - SYSTEM CONFIGURATION PARAMETERS */

/* Release number:
 *
 * This is a base-10, 5 digit number, with one digit for the most
 * significant version number and two for the last two (e.g. 0.4.0 => 00400)
 */
#define CAPFS_RELEASE_NR (CAPFS_VERSION_MAJOR * 10000 + CAPFS_VERSION_MINOR * 100 + CAPFS_VERSION_SUB) 

/* Capabilities:
 * These defines can be used to determine if the installed CAPFS version
 * has certain capabilities:
 * HAVE_MGR_LOOKUP - mgr understands a lookup operation, which returns
 *                   most metadata but doesn't calculate file size
 * HAVE_CAPFS_LISTIO - iods understand list i/o operations, and list i/o
 *                    functionality exists in the library.  this is used
 *                    to obtain higher performance in noncontiguous
 *                    access.
 * HAVE_MGR_CTIME - mgr understands a ctime operation, which can be used
 *                  modify the ctime on an existing file
 */
#define HAVE_MGR_LOOKUP
#define HAVE_CAPFS_LISTIO
#define HAVE_MGR_CTIME

/* __RANDOM_NEXTSOCK__ turns on randomization in the socket selection
 * routines of nextsock(), defined in shared/sockset.c.  This hasn't
 * seemed to make a difference in any situations, so it's off by
 * default.
 */
/* #undef __RANDOM_NEXTSOCK__ */

/* __RANDOM_BASE__ turns on random selection of base nodes on the
 * manager.  This is VERY useful for groups that are using CAPFS to store
 * large numbers of small files.  In the default case this sort of
 * situation will tend to result in I/O node 0 getting a lot of data
 * while the other nodes are mostly empty.
 *
 * On the other hand, it makes it more trouble to know where your data
 * is...and it makes it impossible to add more I/O nodes to an existing
 * configuration.
 */
#define __RANDOM_BASE__ 

/* __ALWAYS_CONN__ turns on the O_CONN behavior all the time; sets up
 * connections to all I/O daemons when capfs_open() is called.  This
 * makes the first read/write calls not include connection time, which
 * makes performance testing a little more straightforward.
 *
 * However, this feature has been found to expose a bug in the Intel
 * Etherexpress Pro fast ethernet card, causing it to hang and spit out
 * nasty messages.  So we've turned it off by default.
 */
/* #undef __ALWAYS_CONN__ */

/* __FS_H_IS_OK__  controls whether or not /linux/fs.h is included in the CAPFS
 * manager and io daemon code.  One thing fs.h does is define NR_OPEN
 * to the kernel's default value, rather than capfs's relatively small
 * default value of 256.  This controls how many files may be open
 * simultaneously by the file system.
 */
#define __FS_H_IS_OK__

/* CAPFS_NR_OPEN controls how many open files a CAPFS process can reliably
 * handle, including sockets and non-CAPFS FDs.  This might need to be changed
 * if you get an error from capfs_open().
 */
#define CAPFS_NR_OPEN 1024

/* CAPFSTAB FILE LOCATION */
#define CAPFSTAB_PATH "/etc/capfstab"
/* CAPFSTAB ENVIRONMENT VARIABLE FOR OVERRIDING IT */
#define CAPFSTAB_ENV "CAPFSTAB_FILE"

/* OUR FS MAGIC NUMBER...returned by statfs() */
#define CAPFS_SUPER_MAGIC 0x0872

/* CLIENT_SOCKET_BUFFER_SIZE - clients will try to set send and receive
 * buffer sizes to this value
 */
#define CLIENT_SOCKET_BUFFER_SIZE (65535)

/* timeout value used when trying to connect to manager (see mgrcomm.c),
 * and when trying to brecv() the ack.
 * Not useful in capfs at all. Need to remove this someday.
 */
#define	MGRCOMM_CONNECT_TIMEOUT_SECS 300
#define	MGRCOMM_BRECV_TIMEOUT_SECS 300
#define	IODCOMM_CONNECT_TIMEOUT_SECS 300
#define	IODCOMM_BRECV_TIMEOUT_SECS 300
/* timeout used for brecv_timeout when trying to pull in requests and
 * acknowledgments.  This affects the iod and the mgr.
 * Not anymore. THis is not useful also.
 */
#define	REQUEST_BRECV_TIMEOUT_SECS 90
/* timeout used for breaking out of infinite selects in check_socks to
 * manually poke the read sockets- this is a workaround for cases in
 * which select hangs indefinitely when sockets really have data ready
 * or have reached an error state.
 */
#define	CHECK_SOCKS_SELECT_HANG_TIMEOUT 21 /* seconds */
/* CAPFS_LIST_IO_CUTOFF - maximum number of noncontiguous regions to use 
 * in a listio request
 */
#define	CAPFS_LIST_IO_CUTOFF 64

/* MANAGER STUFF */
#define MGR_CLNT_TIMEOUT 90
#define IOD_CLNT_TIMEOUT 90
#define CB_CLNT_TIMEOUT  90
#define MGR_NUM_THREADS 2
#define MGR_REQ_PORT 	3000
#define CAPFS_MAXIODS 	512
#define CAPFS_BACKLOG 		256
/* File name restrictions imposed both at the RPC layer and md server disk-side protocol */
#define CAPFS_MAXNAMELEN 1024
#define CAPFS_MAXDENTRY  1024 
/* Size of the hash used. In our code, we use SHA-1, and hence it is 20 bytes */
#define CAPFS_MAXHASHLENGTH 20
/* Parameters for the RPC communication used by meta-server and data-server */
#define CAPFS_MAXHASHES  16384 

/* Should we use UDP/TCP? Library uses UDP by default */
#define MGR_USE_TCP 0
/* The Chunk size is critical both for performance & correctness. Everybody has to agree on
*  this, (hcache, dcache,.... )
*  Currently, this is set to 16KB.
*/
#define CAPFS_CHUNK_SIZE  	16384  /* this is the units of chunking */
#define CAPFS_HCACHE_COUNT 131072 /* i.e. it has a capacity of 131072 hashes (131072 * 20 bytes = 2.5 MB hcache) */
#define CAPFS_DCACHE_BSIZE CAPFS_CHUNK_SIZE /* dcache also needs to know the chunk_size */
#define CAPFS_DCACHE_COUNT 16384 /* i.e. the data cache has a capacity of 16384 data blocks (16384 * 16384 = 256 MB dcache) */

/* cache client/socket handles policy */
#define CAPFS_MGR_CACHE_HANDLES 		  1
#define CAPFS_CALLBACK_CACHE_HANDLES  0
#define CAPFS_CAS_CACHE_HANDLES       1

/* default stripe size */
#define DEFAULT_SSIZE 65536

/* not currently used... */
#define DEFAULT_BSIZE 0

/* inactivity timeout, ms */
#define SOCK_TIME_OUT 300000

/* TCP_CORK - should be easier to get; in /usr/include/linux/socket.h */
#ifndef TCP_CORK
#define TCP_CORK 3
#endif

/* SOL_TCP - not defined on IRIX */
#ifndef SOL_TCP
#define SOL_TCP 6
#endif

/* IOD STUFF */
#define IOD_REQ_PORT 7000
#define IOD_BASE_PORT IOD_REQ_PORT
#define IOD_BACKLOG 256
#define DEFAULT_THREADS 5
#define CAPFS_IOD_BASE_PORT 8765

/* this determines how many directories the iod splits files into;
 * 101, 199, 499, 997 are all decent choices. */
 
#define IOD_DIR_HASH_SZ 101 

#define IOD_USER "nobody"
#define IOD_GROUP "nobody"
#define IOD_ROOTDIR "/"
#define IOD_DATADIR "/capfs-data"
#define IOD_LOGDIR "/tmp"

/* IOD_ACCESS_PAGE_SIZE - memory map region size (must be power of 2)
 *   - previously this value was 128K (for smaller machines)
 * IOD_WRITE_BUFFER_SIZE - size of buffer used for writing data
 *   - previously this value was 64K (for smaller machines)
 * IOD_SOCKET_BUFFER_SIZE - size of IOD socket buffers, recv and xmit
 *   - this value used to be 65535 by default, but admins can change it.
 */

#define IOD_ACCESS_PAGE_SIZE (512*1024)
#define IOD_WRITE_BUFFER_SIZE (512*1024)
#define IOD_SOCKET_BUFFER_SIZE (65535)

/* IOCTL DEFINES - COULDN'T FIND A BETTER PLACE TO PUT THEM... */
/* These are just arbitrary #s that linux doesn't seem to use. */
#define GETPART     0x5601
#define SETPART     0x5602
#define GETMETA     0x5603

/* more arbitrary #s; these are flags for capfs_open() */
/* using lowest 4 bits of highest byte in the integer */
#define CAPFSMASK 03700000000
#define O_META   00100000000
#define O_PART   00200000000
#define O_GSTART 00400000000
#define O_GADD   01000000000
#define O_CONN   02000000000

/* syslog facility to use for logging invalid access attempts. */
#define LOG_ACC_FACILITY LOG_LOCAL7

#define ACC_ADDR       INADDR_ANY
#define ACC_MASK       ((in_addr_t)0)
#endif
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
