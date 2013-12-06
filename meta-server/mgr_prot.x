#include "../lib/capfs_config.h"

/* Are we querying hashes by name or by inode number/fsid */
enum htypeid {
	HASHBYNAME = 0, /* by name */
	HASHBYID = 1 /* by inode number etc */
};

typedef string filename<CAPFS_MAXNAMELEN>;

typedef opaque sha1_hash[CAPFS_MAXHASHLENGTH];

struct creds {
	int32_t uid;
	int32_t gid;
};

struct p_stat {
    uint64_t st_size;
    uint64_t st_ino;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
};

struct p_filestat {
	int32_t base;
	int32_t pcount;
	int32_t ssize;
};

typedef sha1_hash sha1_hashes<CAPFS_MAXHASHES>;

struct fm {
	int64_t fs_ino; /* file system root directory inode # */
	p_stat u_stat;
	p_filestat p_stat;
};

struct s_addr {
	unsigned short int sa_family;
	char sa_data[14];
};

struct i_info {
	s_addr addr;
};

typedef i_info iods_info<CAPFS_MAXIODS>;

struct opstatus {
	int32_t status;
	int32_t eno;
};

struct file {
	int64_t fs_ino;
	int64_t f_ino;
};

/* Callback Registration & Response */

struct cb_args {
	uint32_t svc_addr; 
	int32_t  svc_prog;
	int32_t  svc_vers;
	int32_t  svc_proto;
};

struct cb_resp {
	opstatus status;
	/* use the callback identifier on open/close */
	int32_t  cb_id;
};

struct chmod_args {
	creds credentials;
	filename name;
	int32_t mode;
};

struct chmod_resp {
	opstatus status;
};

struct chown_args {
	creds credentials;
	filename name;
	int32_t force_group_change; /* even if setgid bit was set on parent dir */
	int32_t owner;
	int32_t group;
};

struct chown_resp {
	opstatus status;
};

struct close_args {
	creds credentials;
	fm 	meta;
	int32_t use_hcache; /* Are we using the hcache on this f.s? */
	int32_t cb_id;
};

struct close_resp {
	opstatus status;
};

struct stat_args {
	creds credentials;
	filename name;
};

struct stat_resp {
	opstatus status;
	fm meta;
};

struct open_args {
	creds credentials;
	filename name;
	fm meta;
	int32_t flag;
	int32_t mode;
	int32_t request_hashes; /* Are we requesting hashes on this f.s.? */
	int32_t cb_id;
};

struct open_resp {
	/* status of the open operation */
	opstatus status;
	fm meta;
	int32_t cap;
	/* iod information also needs to be fed back */
	iods_info info;
	/* status of the request hashes */
	opstatus hash_status;
	sha1_hashes h;
};

struct unlink_args {
	creds credentials;
	filename name;
	int32_t  desire_hcache_coherence;
	int32_t  cb_id;
};

struct unlink_resp {
	opstatus status;
};

struct fstat_args {
	creds credentials;
	file    meta;
};

struct fstat_resp {
	opstatus status;
	fm 		meta;
};

struct rename_args {
	creds credentials;
	filename oldname;
	filename newname;
};

struct rename_resp {
	opstatus status;
};

struct iodinfo_args {
	creds credentials;
	filename name;
};

struct iodinfo_resp {
	opstatus status;
	iods_info info;
};

struct mkdir_args {
	creds credentials;
	filename name;
	int32_t mode;
};

struct mkdir_resp {
	opstatus status;
};

struct fchown_args {
	creds credentials;
	file  fhandle;
	int32_t owner;
	int32_t group;
};

struct fchown_resp {
	opstatus status;
};

struct fchmod_args {
	creds credentials;
	file  fhandle;
	int32_t mode;
};

struct fchmod_resp {
	opstatus status;
};

struct rmdir_args {
	creds credentials;
	filename name;
};

struct rmdir_resp {
	opstatus status;
};

struct access_args {
	creds credentials;
	filename name;
	int32_t mode;
	int32_t to_follow;
};

struct access_resp {
	opstatus status;
	fm 		meta;
};

struct truncate_args {
	creds credentials;
	filename name;
	int64_t length;
	int32_t cb_id;
	int32_t desire_hcache_coherence; /* Are we using hcache on this f.s? */
};

struct truncate_resp {
	opstatus status;
	int64_t  old_length;
};

struct utime_args {
	creds credentials;
	filename name;
	int64_t actime;
	int64_t modtime;
};

struct utime_resp {
	opstatus status;
};

struct getdents_args {
	creds credentials;
	filename name;
	int64_t offset;
	int64_t length;
};

struct dentry {
	uint64_t handle;
	uint64_t off;
	filename entry;
};

typedef struct dentry dentries<CAPFS_MAXDENTRY>;

struct getdents_resp {
	opstatus status;
	int64_t  offset;
	dentries entries;
};

struct statfs_args {
	creds credentials;
	filename name;
};

struct statfs_resp {
	opstatus status;
	int64_t tot_bytes;
	int64_t free_bytes;
	int32_t tot_files;
	int32_t free_files;
	int32_t namelen;
};

struct lookup_args {
	creds credentials;
	filename name;
};

struct lookup_resp {
	opstatus status;
	fm meta;
};

struct ctime_args {
	creds credentials;
	filename name;
	int64_t createtime;
};

struct ctime_resp {
	opstatus status;
};

struct link_args {
	creds credentials;
	filename link_name;
	filename target_name;
	int32_t  soft;
	fm   meta;
};

struct link_resp {
	opstatus status;
};

struct readlink_args {
	creds credentials;
	filename link_name;
};

struct readlink_resp {
	opstatus status;
	filename link_name;
};

union hbytype switch(htypeid type) {
	case HASHBYNAME:
		filename name;
	default:
		file     fhandle;
};

struct gethashes_args {
	hbytype  type;
	/* This is chunk value i.e. byte offset divided by the chunk_size */
	int64_t  begin_chunk;
	/* Number of chunks (i.e. in units of chunk_size) */
	int64_t  nchunks;
	/* cb_id */
	int32_t  cb_id;
};

struct gethashes_resp {
	opstatus status;
	sha1_hashes h;
	fm				meta;
};

struct wcommit_args {
	int32_t  cb_id;
	hbytype type;
	/* This is chunk value, i.e byte offset divided by the chunk size */
	int64_t  begin_chunk;
	/* Number of chunks (i.e. in units of chunk_size) */
	int64_t  write_size;
	/* Set of old and new hashes */
	sha1_hashes old_hashes;
	sha1_hashes new_hashes;
	/* Invalidate the hcaches */
	int32_t     desire_hcache_coherence;
	/* Force wcommit */
	int32_t     force_wcommit;
};

struct wcommit_resp {
	opstatus status;
	/* set of hashes that the meta-data server has for optimization sake */
	sha1_hashes current_hashes;
	/* meta data of the file */
	fm meta;
};

program CAPFS_MGR {
	version mgrv1 {
		cb_resp    CAPFS_CBREG(cb_args) = 1;
		chmod_resp CAPFS_CHMOD(chmod_args) = 2;
		chown_resp CAPFS_CHOWN(chown_args) = 3;
		close_resp CAPFS_CLOSE(close_args) = 4;
		stat_resp  CAPFS_LSTAT(stat_args)   = 5;
		open_resp  CAPFS_OPEN(open_args)   = 6;
		unlink_resp CAPFS_UNLINK(unlink_args) = 7;  
		void       CAPFS_SHUTDOWN(void)    = 8;
		fstat_resp  CAPFS_FSTAT(fstat_args)  = 9;
		rename_resp CAPFS_RENAME(rename_args) = 10;
		iodinfo_resp CAPFS_IODINFO(iodinfo_args) = 11;
		mkdir_resp CAPFS_MKDIR(mkdir_args) = 12;
		fchown_resp CAPFS_FCHOWN(fchown_args) = 13;
		fchmod_resp CAPFS_FCHMOD(fchmod_args) = 14;
		rmdir_resp  CAPFS_RMDIR(rmdir_args) = 15;
		access_resp CAPFS_ACCESS(access_args) = 16;
		truncate_resp CAPFS_TRUNCATE(truncate_args) = 17;
		utime_resp  CAPFS_UTIME(utime_args) = 18;
		getdents_resp CAPFS_GETDENTS(getdents_args) = 19;
		statfs_resp CAPFS_STATFS(statfs_args) = 20;
		lookup_resp CAPFS_LOOKUP(lookup_args) = 21;
		ctime_resp  CAPFS_CTIME(ctime_args) = 22;
		link_resp   CAPFS_LINK(link_args) = 23;
		readlink_resp CAPFS_READLINK(readlink_args) = 24;
		stat_resp  CAPFS_STAT(stat_args)   = 25;
		gethashes_resp CAPFS_GETHASHES(gethashes_args) = 26;
		wcommit_resp CAPFS_WCOMMIT(wcommit_args) = 27;
	} = 1;
} = 0x20000001;
