const MAXNAMELEN=256;
const HASHLENGTH=20;
const MAXHASHES=1024;

typedef string fname<MAXNAMELEN>;


struct callback_args {
	int prog;
	int vers;
	int proto;
};

struct hash_args {
	int   cbid;
	fname name;
};

typedef string hash<HASHLENGTH>;

struct hash_res {
	hash  digest;
};

typedef hash_res hash_resp<MAXHASHES>;

program locksvc {
	version v1 {
		int       register_cb(callback_args) = 1;
		hash_resp get_hashes(hash_args) = 2;
		int       put_hashes(fname name, hash_resp) = 3;
	} = 1;
} = 0x20000001;
