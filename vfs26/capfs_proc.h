/*
 * capfs_proc.h
 * 
 * CAPFS operation statistics. Based heavily on the CODA operation statistics file
 * See include/linux/coda_proc.h.
 *
 */

#ifndef _CAPFS_PROC_H
#define _CAPFS_PROC_H

void capfs_sysctl_init(void);
void capfs_sysctl_clean(void);
void capfs_upcall_stats(int opcode, unsigned long jiffies);

#include "ll_capfs.h"
#include <linux/sysctl.h>

/* these files are presented to show the result of the statistics:
 *
 *	/proc/fs/capfs/vfs_stats
	 *		      upcall_stats
	 *		      io_traces
 *
 * these files are presented to reset the statistics to 0:
 *
 *	/proc/sys/capfs/vfs_stats
 *		       upcall_stats
 *		       io_traces
 */

/* VFS operation statistics */

/* All the meta data operations */
struct capfs_vfs_stats 
{
	/* file operations */
	int open;
	int lseek;
	int release;
	int mmap;
	int fsync;
	int read; 
	int write;

	/* dir operations */
	int readdir;
  
	/* inode operations */
	int create;
	int lookup;
	int link;
	int symlink;
	int readlink;
	int unlink;
	int mkdir;
	int rmdir;
	int rename;
	int truncate;
	int revalidate;
	int readpage;
};

/* This counts the number of upcalls and the rough amount of time spent
 * in the queues by the upcalls
 * Unfortunately floating point operations are not recommended inside
 * the kernel. Hence the unsigned long on the time quantities.
 */
struct capfs_upcall_stats_entry 
{
  int count;
  unsigned long time_sum;
  unsigned long time_squared_sum;
};

struct capfs_upcallstats {
	int total; /* total number of upcalls */
	int failures; /* total number of failed upcalls */
};

/* We essentially maintain a count of the read-write operations which fit under a certain bucket
 * size, What is exposed to the user-program,as a configurable parameter is the number of buckets,
 * individual bucket sizes. The maximum 
 */
enum {MAX_BUCKETS = 10, MAX_FILE_NAME_LENGTH = 128};

struct capfs_rwop_stats {
	int nbuckets; /* nbuckets will be silently changed to MAX_BUCKETS if it is greater */
	int read_count[MAX_BUCKETS];
	int write_count[MAX_BUCKETS];
};

/* Update upcall stats from ll_capfs.c */
void capfs_upcall_stats(int opcode, long unsigned runtime);
extern struct capfs_vfs_stats capfs_vfs_stat;
extern struct capfs_upcallstats capfs_callstat;
void capfs_rwop_stats(int rw, size_t size); 
extern int capfs_upcall_timestamping, capfs_collect_stats, capfs_rwtrace_on;


/* reset statistics to 0 */
void reset_capfs_vfs_stats( void );
void reset_capfs_upcall_stats( void );
void reset_capfs_rwop_stats( void );

void do_time_stats(struct capfs_upcall_stats_entry * pentry, unsigned long jiffy );
unsigned long get_time_average( const struct capfs_upcall_stats_entry * pentry );
unsigned long get_time_std_deviation( const struct capfs_upcall_stats_entry * pentry );
int do_reset_capfs_vfs_stats(ctl_table * table, int write, struct file * filp,
			     void __user * buffer, size_t * lenp, loff_t *ppos);
int do_reset_capfs_upcall_stats(ctl_table * table, int write,
		struct file *filp, void __user * buffer,
		size_t * lenp, loff_t *ppos);
int do_reset_capfs_rwop_stats(ctl_table *table, int write, 
				 struct file *filp, void __user *buffer,
				 size_t *lenp, loff_t *ppos);
int do_reset_capfs_capfsd_stats(ctl_table *table, int write, 
				struct file *filp, void __user *buffer,
				size_t *lenp, loff_t *ppos);
/* these functions are called to form the content of /proc/fs/capfs/... files */
int capfs_vfs_stats_get_info( char __user * buffer, char ** start, off_t offset, int length);
int capfs_upcall_stats_get_info( char __user * buffer, char ** start, off_t offset, int length);
int capfs_rwop_stats_get_info(char __user *buffer, char **start, off_t offset, int length);
int capfs_capfsd_stats_get_info(char __user *buffer, char **start, off_t offset, int length);

#endif /* _CAPFS_PROC_H */

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 


