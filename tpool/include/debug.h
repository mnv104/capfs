#ifndef _DEBUG_H
#define _DEBUG_H
#include <stdio.h>
#include <pthread.h>

extern int tp_debug;
/* DEBUGGING VALUES */
enum {
	D_IMPL = 1,
	D_POOL = 2,
	D_PROTO = 4,
};

#define PDEBUG(mask, format...)                                \
  do {                                                            \
    if (tp_debug & mask) {                                      \
			fprintf(stderr,"(%s, %d): ",  __FILE__, __LINE__);                  \
			fprintf(stderr,format);                                       \
			fflush(stderr); \
    }                                                             \
  } while (0) ;

#define PERROR(format...)                                      \
  do {                                                            \
    fprintf(stderr,"(%s, %d): ",  __FILE__, __LINE__);                    \
    fprintf(stderr,format);                                         \
	 fflush(stderr);\
  } while (0) ;

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
