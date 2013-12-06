/*
 * (C) 1995-2004 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */

/*
 * Sysctl operations for CAPFS filesystem
 * This code borrows a lot of ideas from the Coda sysctl/stats implementation.
 */
#define __NO_VERSION__
#include "capfs_kernel_config.h"
#include "ll_capfs.h"
#include "capfs_linux.h"

#include <linux/sysctl.h>
#include <linux/swapctl.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include "capfs_proc.h"

static struct ctl_table_header *fs_table_header;

#define FS_CAPFS         1       /* CAPFS file system */

#define CAPFS_DEBUG  	 			 1	 /* control debugging */
#define CAPFS_DEFAULT_IO_SIZE	 2  /* control io staging size */
#define CAPFS_STATS_ON          3
#define CAPFS_VFS 	 				 4  /* vfs statistics */
#define CAPFS_UPCALL 	 			 5  /* upcall statistics */
#define CAPFS_RWOP              6 /* Read-write statistics */
#define CAPFS_CAPFSD            7 /* capfsd stats */

struct capfs_vfs_stats		     capfs_vfs_stat;
struct capfs_upcall_stats_entry  capfs_upcall_stat[NUM_OPS];
struct capfs_upcallstats         capfs_callstat;
struct capfs_rwop_stats          capfs_rwop_stat;
int                             capfs_upcall_timestamping = 1;
int 									  capfs_rwtrace_on = 0;
int 									  capfs_collect_stats = 0;
extern int capfs_maxsz;
extern int64_t total_service_time, total_rd_time, total_wr_time;


/* FIXME: These are the bucket sizes . THink of a way to expose this to user-programs*/

int bucket_sz[] = {
	1024,
	2048,
	4096,
	8192,
	16384,
	32768,
	65536,
	131072,
	262144,
	-1
};

char *bucket_sizes[]= {
	"< 1K",
	"< 2K",
	"< 4K",
	"< 8K",
	"< 16K",
	"< 32K",
	"< 64K",
	"< 128K",
	"< 256K",
	"rest"
};

/* FIXME: keep this in sync with ll_capfs.h! */
char *capfs_upcall_names[] = {
	"totals  ",   /*  0 */
	"xxxxxx  ",   /*  1 */
	"getmeta ",   /*  2 */
	"setmeta ",   /*  3 */
	"lookup  ",   /*  4 */
	"xxxxxx  ",   /*  5 */
	"readlink",   /*  6 */
	"create  ",   /*  7 */
	"remove  ",   /*  8 */
	"rename  ",   /*  9 */
	"symlink ",   /* 10 */
	"mkdir   ",   /* 11 */
	"rmdir   ",   /* 12 */
	"getdents",   /* 13 */
	"statfs  ",   /* 14 */
	"xxxxxx  ",   /* 15 */
	"xxxxxx  ",   /* 16 */
	"xxxxxx  ",   /* 17 */
	"xxxxxx  ",   /* 18 */
	"hint    ",   /* 19 */
	"read    ",   /* 20 */
	"write   ",   /* 21 */
	"fsync   ",   /* 22 */
	"link    ",   /* 23 */
};


void reset_capfs_vfs_stats(void)
{
	memset(&capfs_vfs_stat, 0 ,sizeof(capfs_vfs_stat));
	return;
}

void reset_capfs_upcall_stats( void )
{
	memset(&capfs_upcall_stat, 0, sizeof(capfs_upcall_stat));
	return;
}

void reset_capfs_call_stats(void)
{
	memset(&capfs_callstat, 0, sizeof(capfs_callstat));
	return;
}

void reset_capfs_rwop_stats(void)
{
	memset(&capfs_rwop_stat, 0, sizeof(capfs_rwop_stat));
	capfs_rwop_stat.nbuckets = MAX_BUCKETS;
	return;
}

void do_time_stats(struct capfs_upcall_stats_entry * pentry, unsigned long runtime )
{
	unsigned long time = runtime;	/* time in us */

	if ( pentry->count == 0 ) {
		pentry->time_sum = pentry->time_squared_sum = 0;
	}
	pentry->count++;
	pentry->time_sum += time;
	pentry->time_squared_sum += time*time;
	return;
}

void capfs_rwop_stats(int rw, size_t size)
{
	struct capfs_rwop_stats *pr = &capfs_rwop_stat;
	int i = 0, bucket_number = MAX_BUCKETS - 1;
	if((rw != 0 && rw != 1) || size < 0) {
		printk(KERN_INFO "Invalid value of rw %d, or size %ld\n",rw,(unsigned long)size);
		return;
	}
	for(i=0;i < MAX_BUCKETS - 1; i++) {
		if(bucket_sz[i] > size) {
			bucket_number = i;
			break;
		}
	}
	(rw == 0) ? pr->read_count[bucket_number]++: pr->write_count[bucket_number]++;
	return;
}


void capfs_upcall_stats(int opcode, long unsigned runtime) 
{
	struct capfs_upcall_stats_entry * pentry;
	if ( opcode < 0 || opcode > NUM_OPS - 1) {
		printk(KERN_INFO "Bad opcode %d passed to capfs_upcall_stats\n", opcode);
		return;
	}
	pentry = &capfs_upcall_stat[opcode];
	do_time_stats(pentry, runtime);
   /* fill in the totals */
	pentry = &capfs_upcall_stat[0];
	do_time_stats(pentry, runtime);
	return;
}

unsigned long get_time_average( const struct capfs_upcall_stats_entry * pentry )
{
	return ( pentry->count == 0 ) ? 0 : pentry->time_sum / pentry->count;
}

static inline unsigned long absolute( unsigned long x )
{
	return x >= 0 ? x : -x;
}

static unsigned long sqr_root( unsigned long x )
{
	unsigned long y = x, r;
	int n_bit = 0;
  
	if ( x == 0 )
		return 0;
	if ( x < 0)
		x = -x;

	while ( y ) {
		y >>= 1;
		n_bit++;
	}
  
	r = 1 << (n_bit/2);
  
	while ( 1 ) {
		r = (r + x/r)/2;
		if ( r*r <= x && x < (r+1)*(r+1) )
			break;
	}
  
	return r;
}

unsigned long get_time_std_deviation( const struct capfs_upcall_stats_entry * pentry )
{
	unsigned long time_avg;
  
	if ( pentry->count <= 1 )
		return 0;
  
	time_avg = get_time_average( pentry );

	return sqr_root( (pentry->time_squared_sum / pentry->count) - 
			    time_avg * time_avg );
}

/* if the user does a echo "1" > /proc/sys/capfs/vfs_stats, then we
 * reset the VFS statistics. 
 * If the user does a cat /proc/sys/capfs/vfs_stats, then we print the VFS statistics
 */
int do_reset_capfs_vfs_stats( ctl_table * table, int write, struct file * filp,
			     void * buffer, size_t * lenp )
{
	if ( write ) {
		if (*lenp > 0) {
				char c;
				if (get_user(c, (char *)buffer))
					return -EFAULT;
				if(c == '1') reset_capfs_vfs_stats();
		 }
		filp->f_pos += *lenp;
	}
	else {
		*lenp = capfs_vfs_stats_get_info(buffer, NULL, filp->f_pos, *lenp);
		filp->f_pos += *lenp;
	}
	return 0;
}

/* if the user does a echo "1" > /proc/sys/capfs/upcall_stats, then we
 * reset the upcall statistics. 
 * If the user does a cat /proc/sys/capfs/upcall_stats, then we print the upcall statistics
 */
int do_reset_capfs_upcall_stats( ctl_table * table, int write, 
				struct file * filp, void * buffer, 
				size_t * lenp )
{
	if ( write ) {
		if (*lenp > 0) {
				char c;
				if (get_user(c, (char *)buffer))
					return -EFAULT;
				capfs_upcall_timestamping = (c == '1');
				if(c == '1') reset_capfs_upcall_stats();
		 }
		 filp->f_pos += *lenp;
	} 
	else {
		*lenp = capfs_upcall_stats_get_info(buffer, NULL, filp->f_pos, *lenp);
		filp->f_pos += *lenp;
		reset_capfs_upcall_stats();
	}
	return 0;
}

/* if the user does a echo "1" > /proc/sys/capfs/rwop_stats, then we
 * reset the read/write statistics. 
 * If the user does a cat /proc/sys/capfs/rwop_stats, then we print the read-write statistics
 */
int do_reset_capfs_rwop_stats( ctl_table * table, int write, 
				    struct file * filp, void * buffer, 
				    size_t * lenp )
{
	if ( write ) {
		if (*lenp > 0) {
				char c;
				if (get_user(c, (char *)buffer))
					return -EFAULT;
				if(c == '1') reset_capfs_rwop_stats();
		}
		filp->f_pos += *lenp;
	}
	else {
		*lenp = capfs_rwop_stats_get_info(buffer, NULL, filp->f_pos, *lenp);
		filp->f_pos += *lenp;
	}
	return 0;
}


/* 
 * If the user does a cat /proc/sys/capfs/capfsd_stats, then we 
 * read the capfsd statistics and then reset it as well...
 */
int do_reset_capfs_capfsd_stats(ctl_table * table, int write, 
				    struct file * filp, void * buffer, 
				    size_t * lenp )
{
	/* only reads allowed */
	if ( !write ) {
		*lenp = capfs_capfsd_stats_get_info(buffer, NULL, filp->f_pos, *lenp);
		filp->f_pos += *lenp;
	}
	return 0;
}


int capfs_capfsd_stats_get_info( char * buffer, char ** start, off_t offset,
			     int length)
{
	int len, i;
	const int LIMIT = PAGE_SIZE - 80;/* Let us not take any chances here */
	char *tmpbuf;
	struct capfs_stats cstats;

	/* First issue an upcall to the daemon and get the stats */
	if ((len = ll_capfs_hint(NULL, HINT_STATS, &cstats)) < 0) {
		PERROR("ll_capfs_hint[HINT_STATS failed] %d\n", len);
		return 0;
	}
	tmpbuf = (char *)kmalloc(PAGE_SIZE, GFP_KERNEL);
	if(!tmpbuf) {
		return 0;
	}
	len = 0;
	do {
		len += sprintf(tmpbuf, "%-79s\n",	"CAPFSD statistics");
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len,"%-79s\n",	"=====================");
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "Hcache hits:", cstats.hcache_hits);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "Hcache misses:", cstats.hcache_misses);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "Hcache fetches:", cstats.hcache_fetches);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "Hcache Invalidates:", cstats.hcache_invalidates);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "Hcache Evicts:", cstats.hcache_evicts);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "Hcache Inv RPC:", cstats.hcache_inv);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "Hcache InvR RPC:", cstats.hcache_inv_range);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "Hcache UpD RPC:", cstats.hcache_upd);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "Retries:", cstats.retries);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "Service Time:", total_service_time);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "Rd Time:", total_rd_time);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "Wr Time:", total_wr_time);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "SHA1 Time:", cstats.sha1_time);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "RPC compute time:", cstats.rpc_compute);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "RPC gethashes time:", cstats.rpc_gethashes);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "RPC get time:", cstats.rpc_get);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "RPC put time:", cstats.rpc_put);
		if(len >=  LIMIT) break;
		len += sprintf(tmpbuf + len, "%-20s %9lld\n", "RPC commit time:", cstats.rpc_commit);
		for (i = 0; i < CAPFS_STATS_MAX; i++)
		{
			if (cstats.server_get_time[i] != 0)
			{
				if(len >=  LIMIT) break;
				len += sprintf(tmpbuf + len, "%-20s %d %9lld\n", "Get time on CAS ", i, cstats.server_get_time[i]);
			}
		}
		for (i = 0; i < CAPFS_STATS_MAX; i++)
		{
			if (cstats.server_put_time[i] != 0)
			{
				if(len >=  LIMIT) break;
				len += sprintf(tmpbuf + len, "%-20s %d %9lld\n", "Put time on CAS ", i, cstats.server_put_time[i]);
			}
		}
	} while (0);
	total_service_time = 0;
	total_rd_time = 0;
	total_wr_time = 0;
	if(offset >= len) {
		kfree(tmpbuf);
		return 0;
	}
	if(offset + length > len) {
		len -= offset;
	}
	if(offset + length < len) {
		len = length;
	}
	if(len) {
		if(copy_to_user(buffer, tmpbuf, len)) {
			kfree(tmpbuf);
			return -EFAULT;
		}
	}
	kfree(tmpbuf);
	return len;
}

int capfs_vfs_stats_get_info( char * buffer, char ** start, off_t offset,
			     int length)
{
	struct capfs_vfs_stats * ps = & capfs_vfs_stat;
	int len;
	const int LIMIT = PAGE_SIZE - 80;/* Let us not take any chances here */
	char *tmpbuf;
	tmpbuf = (char *)kmalloc(PAGE_SIZE, GFP_KERNEL);
	if(!tmpbuf) {
		return 0;
	}
	len = 0;
	do {
		len += sprintf( tmpbuf, "%-79s\n", "CAPFS VFS statistics");
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-79s\n\n", "===================");
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len,	"%-79s\n", "File Operations:");
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "open:", ps->open);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "lseek:", ps->lseek);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "release:",ps->release);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "mmap:",ps->mmap);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "fsync:",ps->fsync);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "read:",ps->read);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n\n", "write:",ps->write);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-79s\n", "Dir Operations:");
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n\n", "readdir:",ps->readdir);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-79s\n", "Inode Operations:");
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "create:",ps->create);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "lookup:",ps->lookup);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "link:",ps->link);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "symlink:",ps->symlink);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "readlink:",ps->readlink);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "unlink:",ps->unlink);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "mkdir:",ps->mkdir);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "rmdir:",ps->rmdir);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "rename:",ps->rename);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "truncate:",ps->truncate);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "revalidate:",ps->revalidate);
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len, "%-12s %9d\n", "readpage:",ps->readpage);
	}while(0);
	if(offset >= len) {
		kfree(tmpbuf);
		return 0;
	}
	if(offset + length > len) {
		len -= offset;
	}
	if(offset + length < len) {
		len = length;
	}
	if(len) {
		if(copy_to_user(buffer, tmpbuf, len)) {
			kfree(tmpbuf);
			return -EFAULT;
		}
	}
	kfree(tmpbuf);
	return len;
}

int capfs_upcall_stats_get_info( char * buffer, char ** start, off_t offset, int length)
{
	int i,len;
	const int LIMIT = PAGE_SIZE - 80;
	char *tmpbuf = NULL,buf[80];
	tmpbuf = (char *)kmalloc(PAGE_SIZE, GFP_KERNEL);
	if(!tmpbuf) {
		return 0;
	}
	len = 0;
	do {
		len += sprintf(tmpbuf, "%-79s\n",	"CAPFS upcall statistics");
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len,"%-79s\n",	"======================");
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len,"%-79s\n",	"upcall              count       avg time(us)    std deviation(us)");
		if(len >=  LIMIT) break;
		len += sprintf( tmpbuf + len,"%-79s\n",	"------              -----       ------------    -----------------");
		if(len >=  LIMIT) break;
		for ( i = 0 ; i < NUM_OPS ; i++ ) {
			sprintf(buf, "%s    %9d       %10ld      %10ld", 
					  capfs_upcall_names[i],
					  capfs_upcall_stat[i].count, 
					  get_time_average(&capfs_upcall_stat[i]),0UL);
					  //get_time_std_deviation(&capfs_upcall_stat[i]));
			len += sprintf(tmpbuf + len, "%-79s\n", buf);
			if(len >=  LIMIT) break;
		}
	}while(0);
	if(offset >= len) {
		kfree(tmpbuf);
		return 0;
	}
	if(offset + length > len) {
		len -= offset;
	}
	if(offset + length < len) {
		len = length;
	}
	if(len) {
		if(copy_to_user(buffer, tmpbuf + offset, len)) {
			kfree(tmpbuf);
			return -EFAULT;
		}
	}
	kfree(tmpbuf);
	return len;
}

int capfs_rwop_stats_get_info( char * buffer, char ** start, off_t offset, int length)
{
	int i, len;
	/* There can only be 10 buckets, so we have to have a buffer of length 40
	 * or more 
	 */
	const int LIMIT = PAGE_SIZE - 80;
	struct capfs_rwop_stats * ps = &capfs_rwop_stat;
	char *tmpbuf = NULL, buf[80];
  
	tmpbuf = (char *)kmalloc(PAGE_SIZE,GFP_KERNEL);
	if(!tmpbuf) {
		return 0;
	}
	len = 0;
	do {
		len += sprintf( tmpbuf,
				"CAPFS Read/Write statistics\n"
				"==========================\n"
				"nbuckets = %9d\n",
				ps->nbuckets);
		if(len >= LIMIT) break;
		for ( i = 0 ; i < MAX_BUCKETS ; i++ ) {
			sprintf(buf, "Bucket Number %2d (Size [%10s]) -> read[%9d] -> write[%9d]",(i+1),bucket_sizes[i], ps->read_count[i], ps->write_count[i]);
			len += sprintf(tmpbuf + len, "%-79s\n", buf);
			if(len >=  LIMIT) break;
		}
	}while(0);
	if(offset >= len) {
		kfree(tmpbuf);
		return 0;
	}
	if(offset + length > len) {
		len -= offset;
	}
	if(offset + length < len) {
		len = length;
	}
	if(len) {
		if(copy_to_user(buffer, tmpbuf, len)) {
			kfree(tmpbuf);
			return -EFAULT;
		}
	}
	kfree(tmpbuf);
	return len;
}


#ifdef CONFIG_PROC_FS

/*
 target directory structure:
   /proc/fs  (see linux/fs/proc/root.c)
   /proc/fs/capfs
   /proc/fs/capfs/{vfs_stats,rw_stats,debug,upcall_stats,io_size,capfsd_stats}
*/

struct proc_dir_entry* proc_fs_capfs;

#endif

/* these will be child proc entries under /proc/sys/capfs/ */
static ctl_table capfs_table[] = {
	{CAPFS_DEBUG, "debug", &capfs_debug, sizeof(int), 0644, NULL, &proc_dointvec},
	{CAPFS_DEFAULT_IO_SIZE, "io_size", &capfs_maxsz, sizeof(int), 0644, NULL, &proc_dointvec},
	{CAPFS_STATS_ON, "collect_stats",&capfs_collect_stats, sizeof(int), 0644, NULL, &proc_dointvec},
 	{CAPFS_VFS, "vfs_stats", NULL, 0, 0644, NULL, &do_reset_capfs_vfs_stats},
 	{CAPFS_UPCALL, "upcall_stats", NULL, 0, 0644, NULL, &do_reset_capfs_upcall_stats},
	{CAPFS_RWOP, "rw_stats", NULL, 0, 0644, NULL, &do_reset_capfs_rwop_stats},
	{CAPFS_CAPFSD, "capfsd_stats", NULL, 0, 0644, NULL, &do_reset_capfs_capfsd_stats},
	{0}
};

/* this will form the root of the proc hierarchy */
static ctl_table fs_table[] = {
       {FS_CAPFS, "capfs",    NULL, 0, 0555, capfs_table},
       {0}
};

#define capfs_proc_create(name,get_info) \
	create_proc_info_entry(name, 0, proc_fs_capfs, get_info)

void capfs_sysctl_init()
{
	struct proc_dir_entry *entry = NULL;
	memset(&capfs_callstat, 0, sizeof(capfs_callstat));
	reset_capfs_vfs_stats();
	reset_capfs_upcall_stats();
	reset_capfs_rwop_stats();

#ifdef CONFIG_PROC_FS
	proc_fs_capfs = proc_mkdir("capfs", proc_root_fs);
	if (proc_fs_capfs) {
		proc_fs_capfs->owner = THIS_MODULE;
		if((entry = capfs_proc_create("vfs_stats", capfs_vfs_stats_get_info)) == NULL) {
			printk(KERN_INFO "Could not create vfs_stats in proc\n");
		}
		if((entry = capfs_proc_create("upcall_stats", capfs_upcall_stats_get_info)) == NULL) {
			printk(KERN_INFO "Could not create upcall_stats in proc\n");
		}
		if((entry = capfs_proc_create("rw_stats", capfs_rwop_stats_get_info)) == NULL) {
			printk(KERN_INFO "Could not creat rw_stats in proc\n");
		}
		if((entry = capfs_proc_create("capfsd_stats", capfs_capfsd_stats_get_info)) == NULL) {
			printk(KERN_INFO "Could not creat capfsd_stats in proc\n");
		}
	}
#endif

#ifdef CONFIG_SYSCTL
	if ( !fs_table_header )
		fs_table_header = register_sysctl_table(fs_table, 0);
#endif 
}

void capfs_sysctl_clean() 
{
#ifdef CONFIG_SYSCTL
	if ( fs_table_header ) {
		unregister_sysctl_table(fs_table_header);
		fs_table_header = NULL;
	}
#endif
#if CONFIG_PROC_FS
	remove_proc_entry("capfsd_stats", proc_fs_capfs);
	remove_proc_entry("rw_stats", proc_fs_capfs);
	remove_proc_entry("upcall_stats", proc_fs_capfs);
	remove_proc_entry("vfs_stats", proc_fs_capfs);
	remove_proc_entry("capfs", proc_root_fs);
#endif 
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

