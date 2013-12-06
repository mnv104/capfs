#ifndef _CMGR_CONSTANTS_H
#define _CMGR_CONSTANTS_H

#include <stdio.h>
#include <assert.h>
#include <pthread.h>

extern FILE *output_fp, *error_fp;

enum {
		CM_MAGIC = 0x12345678, /* magic number */
		CM_BSIZE = 16384, /* in bytes */
		CM_BCOUNT = 1024, /* Number of blocks */
		CM_TABLE_SIZE = 1023, /* hash table prime number */	
		CM_GCLOCK_REF = 10, /* reference clock */
		CM_GCLOCK_AGE = 10, /* Ageing of the reference clock */
		CM_HANDLE_SIZE = 64, /* Size of handle in bytes */
};

/* can control the urgency of the harvester thread invocations */

/* 
 * Some notes about these 2 parameters.
 * a) The Low water Mark is a fraction that dictates when the harvester
 * thread is invoked to either free up some pages or writeback some
 * dirty ones. If it is set too high, then the harvester thread will be invoked
 * more aggressively when the # of free pages is fairly high so that it can
 * either try and writeback more pages or will try and release a few pages
 * into the free list. 
 * b) The high-water Mark is a fraction that dictates how long the harvester
 * thread will be active once it is woken up, since that is an expensive operation.
 * The tradeoffs in choosing this value is that if it is set too low then
 * the harvester thread will do very little work in each invocation/wake cycle.
 * If it is set too high, then it will probably be very aggressive and compete for
 * CPU cycles from the main client threads that will result in net-drop
 * in performance potentially. 
 * c) CM_BATCH_RATIO dictates the ratio that decides when the thread should yield.
 */
#define CM_LOW_WATER  0.5 /* low water mark */
#define CM_HIGH_WATER 0.7 /* high water mark */
#define CM_BATCH_RATIO 0.1 /* ratio that dictates when to yield */

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

#define lock_my_printf(x,y,z...)\
	if (getenv("CMGR_LOCK_DEBUG") != NULL) {\
		fprintf(x,"[%lu] -> "y, pthread_self(), ##z);\
		fflush(x);\
	}

#define my_printf(x,y,z...) \
	if (getenv("CMGR_DEBUG") != NULL) {\
		fprintf(x,"[%lu] -> "y, pthread_self(), ##z);\
		fflush(x);\
	}

#ifdef LOCK_DEBUG
#define lock_printf(x,y...) lock_my_printf(output_fp,x,##y)
#else
#define lock_printf(x,y...)
#endif

#ifdef DEBUG
#define dprintf(x,y...) my_printf(output_fp, x, ##y)
#else
#define dprintf(x,y...)
#endif

#define Assert(x)			assert(x)
#define panic(x,y...) 	fprintf(error_fp, "[%lu] -> "x, pthread_self(),  ##y)

#define bh_hash_shift 15
#define _hashfn(machine_number,inode_number,pagenumber) \
		  ((((1) << (bh_hash_shift - 6)) ^ ((inode_number) << (bh_hash_shift - 9))) ^ \
													 (((pagenumber)  << (bh_hash_shift - 6))  ^ \
							((pagenumber)  >> 13) ^ ((pagenumber) << (bh_hash_shift - 12))))


#define hash1(x)  	_hashfn(0,(x),0)
#define hash2(x, y)  _hashfn(0, (x), (y))

#endif
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 *
 * vim: ts=3
 */

