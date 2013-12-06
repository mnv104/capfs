#include "../lib/capfs_config.h"

typedef string fname<CAPFS_MAXNAMELEN>;
typedef opaque __sha1[CAPFS_MAXHASHLENGTH];

enum filebyid {
	FILEBYNAME = 0, /* identify by name */
	FILEBYHANDLE = 1 /* identify by fsid, file inode */
};

struct Handle {
	int64_t fs_ino;
	int64_t f_ino;
};

union identify switch(filebyid type) {
	case FILEBYNAME:
		fname  name;
	default:
		Handle fhandle;
};

struct _sha1 {
	__sha1 h;
};

typedef _sha1 sha1_list<CAPFS_MAXHASHES>;

struct inv_args {
	identify id;
	int64_t begin_chunk;
	int64_t nchunks;
};

struct inv_resp {
	int   status;
};

struct upd_args {
	identify id;
	int64_t  begin_chunk;
	sha1_list		hashes;
};

struct upd_resp {
	int      status;
};

program CAPFS_CAPFSD {
	version clientv1 {
		inv_resp CAPFSD_INVALIDATE(inv_args) = 1;
		upd_resp CAPFSD_UPDATE(upd_args) = 2;
	} = 1;
} = 0x20000002;

