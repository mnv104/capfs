/*
 * Copyright (C) 2005
 * Murali Vilayannur 
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include "log.h"
#include "map_chunk.h"

struct map_resp* map_chunks(int64_t file_offset, int64_t count, struct map_info *info, int64_t *ch_count)
{
	int64_t begin_chunk = 0, end_chunk = 0, chunk_count = 0, i, off = 0;
	struct map_resp *resp = NULL;

	if (info == NULL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT,  "Invalid NULL pointer\n");
		return NULL;
	}
	else {
		if (info->meta.ssize <= 0) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT,  "Invalid stripe size (%d) < 0\n", info->meta.ssize);
			return NULL;
		}
		if (info->chunk_size <= 0) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT,  "Invalid chunk size (%d) < 0\n", info->chunk_size);
			return NULL;
		}
		if (info->chunk_size > info->meta.ssize) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT,  "Invalid value of chunk size. Cannot be (%d) > stripe size (%d)\n",
						info->chunk_size, info->meta.ssize);
				return NULL;
			}
		if (info->meta.ssize % info->chunk_size != 0) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT,  "Stripe size (%d) needs to be a multiple of chunk size (%d)\n",
					info->meta.ssize, info->chunk_size);
			return NULL;
		}
		if (info->meta.pcount <= 0) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT,  "Invalid value of pcount(%d) < 0\n", info->meta.pcount);
			return NULL;
		}
		/* can be -ve */
		if (info->meta.base < 0) {
			info->meta.base = 0;
		}
	}
	begin_chunk = (file_offset / info->chunk_size);
	end_chunk = (file_offset + count - 1) / info->chunk_size;
	chunk_count = (end_chunk - begin_chunk + 1);
	resp = (struct map_resp *) calloc(chunk_count, sizeof(struct map_resp));
	if (resp == NULL) {
		return NULL;
	}
	off = (begin_chunk * info->chunk_size);
	for (i = 0; i < chunk_count; i++) {
		if (info->meta.pcount == 1) {
			resp[i].iod_number = info->meta.base;
		}
		else {
			resp[i].iod_number = (info->meta.base + (off / info->meta.ssize)) % info->meta.pcount;
		}
		off += info->chunk_size;
	}
	*ch_count = chunk_count;
	return resp;
} 


/*
int main(void)
{
	struct map_info info;
	struct map_resp *resp; 
	int64_t file_offset, count, i, chunk_count;

	srand(time(NULL));
	info.chunk_size = 16;
	info.meta.ssize = 32;
	info.meta.pcount = 4;
	info.meta.base = 0;

	file_offset = rand() % 1000;
	count = rand() % 1000;
	printf("ssize = %d, pcount = %d, base = %d, chunk_size = %d\n",
			info.meta.ssize, info.meta.pcount, info.meta.base, info.chunk_size);
	resp = map_chunks(file_offset, count, &info, &chunk_count);
	if (resp) {
		printf("file offset = %Ld, bytes count = %Ld, chunk count = %Ld\n",
				file_offset, count, chunk_count);
		for (i = 0; i < chunk_count; i++) {
			printf("Chunk %Ld -> Iod %d\n", (file_offset / info.chunk_size) + i, resp[i].iod_number);
		}
	}
	free(resp);
	return 0;
}

*/

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 *
 * vim: ts=3
 */

