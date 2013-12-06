#ifndef _REQ_H
#define _REQ_H

#include <stdio.h>
#include "sha.h"

typedef struct {
	char fname[256];
}mreq ;

typedef struct {
	int status;
	int num_hash;
	size_t trailer;
}mack;
/* followed by "trailer" bytes of the actual hashes themselves */

typedef struct {
	int count;
}ireq;
/* followed by "count" number of the actual hashes themselves */

typedef struct {
	int status;
	size_t total_size ;
}iack ;
/* followed by total_size bytes of file data */

#endif
