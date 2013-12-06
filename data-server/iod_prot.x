#include "../lib/capfs_config.h"

typedef int    op_status<CAPFS_MAXHASHES>;
typedef opaque sha1hash[CAPFS_MAXHASHLENGTH];
typedef string dirname<CAPFS_MAXNAMELEN>;

typedef sha1hash get_hashes<CAPFS_MAXHASHES>;
typedef sha1hash put_hashes<CAPFS_MAXHASHES>;

struct get_req {
	get_hashes h;
};

typedef opaque data<CAPFS_CHUNK_SIZE>;
typedef data   data_blocks<CAPFS_MAXHASHES>;

struct get_resp {
	op_status    status;
	data_blocks  blocks;
	int64_t      get_time;
};

struct put_req {
	put_hashes h;
	data_blocks blocks;
};

struct put_resp {
	op_status    status;
	int			bytes_done;
	int64_t      put_time;
};

struct iod_statfs {
    int f_type;
    int f_bsize;
    uint64_t f_blocks;
    uint64_t f_bfree;
    uint64_t f_bavail;
    uint64_t f_files;
    uint64_t f_ffree;
    uint64_t f_fsid;
    int f_namelen;
};

struct cas_stat_resp {
	int         status;
	iod_statfs sfs;
};

/* 
 * PLEASE DO NOT USE THE REMOVEALL RPC request, unless you know what you are doing!!! 
 * This will delete all the hashes rooted at the top-level directories named by "name".
 * This will be phased out once Jason's garbage collection code is done and well-tested.
 */

struct removeall_req {
	dirname     name;
};

struct removeall_resp {
	int 			status;
};

program CAPFS_IOD {
	version iodv1 {
		get_resp CAPFS_GET(get_req) = 1;
		put_resp CAPFS_PUT(put_req) = 2;
		cas_stat_resp CAPFS_DSTATFS(void) = 3;
		removeall_resp CAPFS_REMOVEALL(removeall_req) = 4;
	} = 1;
} = 0x20000003;


