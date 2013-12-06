#ifndef __TEST_WRITE_H
#define __TEST_WRITE_H

#define DEF_NO_WRITES               100

#define MEMORY_CHUNK_SIZE           65536
#define FILE_SIZE                   131072
#define N_MEMORY_CHUNKS             10
#define DEF_WRITE_SIZE              32768
#define TEST_PROGRESS_CKPOINTS      10

#define MEMORY_SEEK_MAX             ( MEMORY_CHUNK_SIZE - WRITE_SIZE )
#define FILE_SEEK_MAX               ( FILE_SIZE - WRITE_SIZE )

#endif
