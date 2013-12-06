#ifndef _MAP_CHUNK_H
#define _MAP_CHUNK_H

#include <sys/types.h>
#include <meta.h>

struct map_info {
	int chunk_size;
	struct capfs_filestat meta;
};

struct map_resp {
	int iod_number;
};
extern struct map_resp* 
	map_chunks(int64_t file_offset, int64_t count, struct map_info *info, int64_t *ch_count);

#endif
