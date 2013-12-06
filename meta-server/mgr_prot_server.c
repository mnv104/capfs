
#include "capfs-header.h"
#include "mgr_prot.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <req.h>
#include <capfs_config.h>
#include <meta.h>
#include <desc.h>
#include "log.h"
#include "mgr.h"
#include "sha.h"
#include "mgr_prot_common.h"

/* callback registration (server) returns the callback identifiers */
extern int cbreg_svc(cb_args *cb, struct sockaddr_in *raddr);
/* callback client handle stuff */
extern CLIENT** get_cb_handle(int cb_id);
extern void put_cb_handle(CLIENT **pclnt, int force_put);

static inline void init_defaults(mreq *req, int type, creds *credentials)
{
	req->majik_nr = MGR_MAJIK_NR;
	req->release_nr = CAPFS_RELEASE_NR;
	req->type = type;
	if (credentials) {
		req->uid = credentials->uid;
		req->gid = credentials->gid;
	}
	else {
		req->uid = 0;
		req->gid = 0;
	}
	return;
}

static inline void init_opstatus(opstatus *status, mack *ack)
{
	status->status = ack->status;
	status->eno = ack->eno;
	return;
}

bool_t
capfs_cbreg_1_svc(cb_args arg1, cb_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	struct sockaddr_in remote_address;
	int cbid = 0;

	/*remote_address = svc_getcaller(rqstp->rq_xprt);*/
	memset(&remote_address, 0, sizeof(struct sockaddr_in));
	remote_address.sin_addr.s_addr = arg1.svc_addr;
	cbid = cbreg_svc(&arg1, &remote_address);
	if (cbid < 0) {
		result->status.status = -1;
		/* hijacked some other errno! */
		result->status.eno = EXFULL;
	}
	else {
		result->status.status = 0;
		result->status.eno = 0;
	}
	result->cb_id = cbid;
	return retval;
}

bool_t
capfs_chmod_1_svc(chmod_args arg1, chmod_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_CHMOD, &arg1.credentials);
	/* Convert arg1 to relevant portions of req and pass it to compat layer */
	req.dsize = strlen(arg1.name);
	req.req.chmod.mode = arg1.mode;
	buf_p = arg1.name;

	err = process_compat_req(&req, &ack, buf_p, &ackdata);
	init_opstatus(&result->status, &ack);

	return retval;
}

bool_t
capfs_chown_1_svc(chown_args arg1, chown_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_CHOWN, &arg1.credentials);
	/* Convert arg1 to relevant portions of req and pass it to compat layer */
	req.dsize = strlen(arg1.name);
	req.req.chown.force_group_change = arg1.force_group_change;
	req.req.chown.owner = arg1.owner;
	req.req.chown.group = arg1.group;
	buf_p = arg1.name;

	err = process_compat_req(&req, &ack, buf_p, &ackdata);
	init_opstatus(&result->status, &ack);

	return retval;
}

bool_t
capfs_close_1_svc(close_args arg1, close_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_CLOSE, &arg1.credentials);
	req.dsize = 0;
	req.req.close.meta.fs_ino = arg1.meta.fs_ino;
	req.req.close.meta.u_stat.st_ino = arg1.meta.u_stat.st_ino;
	buf_p = NULL;
	/* Convert arg1 to relevant portions of req and pass it to compat layer */
	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	/* and remove this node from the callback list for this file */
	if (ack.status == 0 && arg1.use_hcache == 1)
	{
		del_callbacks(arg1.meta.fs_ino, arg1.meta.u_stat.st_ino, arg1.cb_id);
	}

	init_opstatus(&result->status, &ack);

	return retval;
}

bool_t
capfs_lstat_1_svc(stat_args arg1, stat_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_LSTAT, &arg1.credentials);
	req.dsize = strlen(arg1.name);
	buf_p = arg1.name;
	/* Convert arg1 to relevant portions of req and pass it to compat layer */
	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	init_opstatus(&result->status, &ack);
	copy_from_fmeta_to_fm(&ack.ack.stat.meta, &result->meta);
	return retval;
}

static int convert_to_iods_info(iods_info *info, struct ackdata *ackdata)
{
	int i;

	if (ackdata->type == MGR_OPEN) {
		info->iods_info_len = ackdata->u.open.niods;
	}
	else if (ackdata->type == MGR_IOD_INFO) {
		info->iods_info_len = ackdata->u.iodinfo.niods;
	}
	if ((info->iods_info_val = (i_info *) calloc(info->iods_info_len, sizeof(i_info))) == NULL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "malloc of iods_info failed!\n");
		info->iods_info_len = 0;
		if (ackdata->type == MGR_OPEN) {
			/* ackdata->u.open.iod must be freed here */
			free(ackdata->u.open.iod);
		}
		return -1;
	}
	for (i = 0; i < info->iods_info_len; i++) {
		if (ackdata->type == MGR_OPEN) {
			memcpy(&info->iods_info_val[i].addr, &((ackdata->u.open.iod[i])->addr), sizeof(s_addr));
			//sockio_dump_sockaddr((struct sockaddr_in *)&info->iods_info_val[i].addr, stderr);
		}
		else if (ackdata->type == MGR_IOD_INFO) {
			memcpy(&info->iods_info_val[i].addr, &(ackdata->u.iodinfo.iod[i].addr), sizeof(s_addr));
			//sockio_dump_sockaddr((struct sockaddr_in *)&info->iods_info_val[i].addr, stderr);
		}
	}
	if (ackdata->type == MGR_OPEN) {
		/* ackdata->u.open.iod must be freed here */
		free(ackdata->u.open.iod);
	}
	return 0;
}


static int copy_sha1_hashes(sha1_hashes *h, struct ackdata *ackdata)
{
	int i;

#undef  MIN
#define MIN(a, b) (a) < (b) ? (a) : (b)
	/*
	 * In case the file size was larger than (CHUNKSIZE * MAXCHUNKS)
	 * then we are in some trouble...
	 * The client expects only CAPFS_MAXHASHES, so we need to set 
	 * h->sha1_hashes_len to min(CAPFS_MAXHASHES, ackdata->u) so that
	 * we cap off the transfers to that much.
	 */
	if (ackdata->type == MGR_OPEN) {
		h->sha1_hashes_len = MIN(CAPFS_MAXHASHES, ackdata->u.open.nhashes);
		if (h->sha1_hashes_len != ackdata->u.open.nhashes)
		{
			LOG(stderr, WARNING_MSG, SUBSYS_META, "copy_sha1_hashes: truncated # of hashes returned to (%d) from (%lld)\n",
					h->sha1_hashes_len, ackdata->u.open.nhashes);
		}
	}
	else if (ackdata->type == MGR_GETHASHES) {
		h->sha1_hashes_len = MIN(CAPFS_MAXHASHES, ackdata->u.gethashes.nhashes);
		if (h->sha1_hashes_len != ackdata->u.gethashes.nhashes)
		{
			LOG(stderr, WARNING_MSG, SUBSYS_META, "copy_sha1_hashes: truncated # of hashes returned to (%d) from (%lld)\n",
					h->sha1_hashes_len, ackdata->u.gethashes.nhashes);
		}
	}
	else if (ackdata->type == MGR_WCOMMIT) {
		h->sha1_hashes_len = MIN(CAPFS_MAXHASHES, ackdata->u.wcommit.current_hash_len);
		if (h->sha1_hashes_len != ackdata->u.wcommit.current_hash_len)
		{
			LOG(stderr, WARNING_MSG, SUBSYS_META, "copy_sha1_hashes: truncated # of hashes returned to (%d) from (%Ld)\n",
					h->sha1_hashes_len, ackdata->u.wcommit.current_hash_len);
		}
	}
#undef MIN
	if (h->sha1_hashes_len > 0) {
		if ((h->sha1_hashes_val = 
					(sha1_hash *) calloc(h->sha1_hashes_len, sizeof(sha1_hash))) == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "copy_sha1_hashes: malloc of sha1_hashes_val failed!\n");
			h->sha1_hashes_len = 0;
			if (ackdata->type == MGR_OPEN) {
				free(ackdata->u.open.hashes);
				ackdata->u.open.hashes = NULL;
			}
			else if (ackdata->type == MGR_GETHASHES) {
				free(ackdata->u.gethashes.hashes);
				ackdata->u.gethashes.hashes = NULL;
			}
			else if (ackdata->type == MGR_WCOMMIT) {
				free(ackdata->u.wcommit.current_hashes);
				ackdata->u.wcommit.current_hashes = NULL;
			}
			return -1;
		}
	}
	else if (h->sha1_hashes_len < 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "-ve sha1 hash lengths not allowed\n");
		h->sha1_hashes_len = 0;
	}
	for (i = 0; i < h->sha1_hashes_len; i++) {
		if (ackdata->type == MGR_OPEN) {
			memcpy(h->sha1_hashes_val[i], ackdata->u.open.hashes + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH);
		}
		else if (ackdata->type == MGR_GETHASHES) {
			memcpy(h->sha1_hashes_val[i], ackdata->u.gethashes.hashes + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH);
		}
		else if (ackdata->type == MGR_WCOMMIT) {
			memcpy(h->sha1_hashes_val[i], ackdata->u.wcommit.current_hashes + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH);
		}
	}
	if (ackdata->type == MGR_OPEN) {
		free(ackdata->u.open.hashes);
		ackdata->u.open.hashes = NULL;
	}
	else if (ackdata->type == MGR_GETHASHES) {
		free(ackdata->u.gethashes.hashes);
		ackdata->u.gethashes.hashes = NULL;
	}
	else if (ackdata->type == MGR_WCOMMIT) {
		free(ackdata->u.wcommit.current_hashes);
		ackdata->u.wcommit.current_hashes = NULL;
	}
	return 0;
}

bool_t
capfs_open_1_svc(open_args arg1, open_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_OPEN, &arg1.credentials);
	req.dsize = strlen(arg1.name);
	buf_p = arg1.name;
	copy_from_fm_to_fmeta(&arg1.meta, &req.req.open.meta);
	req.req.open.flag = arg1.flag;
	req.req.open.mode = arg1.mode;
	req.req.open.ackdsize = 0;
	/* Convert arg1 to relevant portions of req and pass it to compat layer */
	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	init_opstatus(&result->status, &ack);
	copy_from_fmeta_to_fm(&ack.ack.open.meta, &result->meta);
	result->cap = ack.ack.open.cap;
	result->info.iods_info_len = 0;
	result->info.iods_info_val = NULL;
	if (ack.status == 0 && ackdata.u.open.iod != NULL) {
		if (convert_to_iods_info(&result->info, &ackdata) < 0) {
			result->status.status = -1;
			result->status.eno = ENOMEM;
		}
	}
	result->hash_status.status = 0;
	result->hash_status.eno = 0;
	result->h.sha1_hashes_len = 0;
	result->h.sha1_hashes_val = NULL;
	if (ackdata.u.open.nhashes < 0 || ackdata.u.open.hashes == NULL) {
		result->hash_status.status = -1;
		result->hash_status.eno = -(ackdata.u.open.nhashes);
		if (result->hash_status.eno == 0) {
			result->hash_status.eno = ENOMEM;
		}
	}
	else {
		/* if the user did not request hashes, do not send them back */
		if (arg1.request_hashes == 1)
		{
			if (copy_sha1_hashes(&result->h, &ackdata) < 0) {
				result->hash_status.eno = ENOMEM;
				result->hash_status.status = -1;
			}
		}
	}
	/* and add callbacks for this node and file */
	if (ack.status == 0 && arg1.request_hashes == 1) 
	{
		/*
		 * Note that this implies that we do need to keep
		 * track of callbacks of nodes requesting gethashes
		 * RPCs as well.
		 */
		add_callbacks(ack.ack.open.meta.fs_ino, ack.ack.open.meta.u_stat.st_ino, buf_p, arg1.cb_id);
	}
	return retval;
}

bool_t
capfs_unlink_1_svc(unlink_args arg1, unlink_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;
	unsigned long bitmap = 0;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_UNLINK, &arg1.credentials);
	/* Convert arg1 to relevant portions of req and pass it to compat layer */
	req.dsize = strlen(arg1.name);
	buf_p = arg1.name;
	err = process_compat_req(&req, &ack, buf_p, &ackdata);
	/* 
	 * okay, now we need to expunge this object 
	 * from the callback hashtables and also evict
	 * the hashes for this object from other hcaches
	 * if need be
	 */
	if (ack.status == 0 && arg1.desire_hcache_coherence == 1)
	{
		bitmap = clear_callbacks(ackdata.u.unlink.fs_ino, ackdata.u.unlink.f_ino);
		/* We will let errors slide by for now... */
		cb_clear_hashes(buf_p, bitmap, arg1.cb_id);
	}
	init_opstatus(&result->status, &ack);

	return retval;
}

bool_t
capfs_shutdown_1_svc(void *result, struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_SHUTDOWN, NULL);

	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	return retval;
}

bool_t
capfs_fstat_1_svc(fstat_args arg1, fstat_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_FSTAT, &arg1.credentials);
	req.dsize = 0;
	req.req.fstat.meta.fs_ino = arg1.meta.fs_ino;
	req.req.fstat.meta.u_stat.st_ino = arg1.meta.f_ino;
	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	init_opstatus(&result->status, &ack);
	copy_from_fmeta_to_fm(&ack.ack.fstat.meta, &result->meta);
	return retval;
}

bool_t
capfs_rename_1_svc(rename_args arg1, rename_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err, len1 = 0, len2 = 0;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_RENAME, &arg1.credentials);

	len1 = strlen(arg1.oldname);
	len2 = strlen(arg1.newname);
	req.dsize = len1 + len2 + 1;
	buf_p = (char *) calloc(req.dsize + 1, 1);
	if (buf_p == NULL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "malloc for rename failed\n");
		result->status.status = -1;
		result->status.eno = ENOMEM;
		return retval;
	}
	strcpy(buf_p, arg1.oldname);
	strcpy(&buf_p[len1+1], arg1.newname);
	err = process_compat_req(&req, &ack, buf_p, &ackdata);
	free(buf_p);
	init_opstatus(&result->status, &ack);
	return retval;
}

bool_t
capfs_iodinfo_1_svc(iodinfo_args arg1, iodinfo_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_IOD_INFO, &arg1.credentials);
	req.dsize = strlen(arg1.name);
	buf_p = arg1.name;
	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	init_opstatus(&result->status, &ack);
	result->info.iods_info_len = 0;
	result->info.iods_info_val = NULL;
	if (ack.status == 0 && ackdata.u.iodinfo.iod != NULL) {
		convert_to_iods_info(&result->info, &ackdata);
	}
	return retval;
}

bool_t
capfs_mkdir_1_svc(mkdir_args arg1, mkdir_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_MKDIR, &arg1.credentials);
	req.req.mkdir.mode = arg1.mode;
	req.dsize = strlen(arg1.name);
	buf_p = arg1.name;
	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	init_opstatus(&result->status, &ack);

	return retval;
}

bool_t
capfs_fchown_1_svc(fchown_args arg1, fchown_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_FCHOWN, &arg1.credentials);
	req.req.fchown.fs_ino = arg1.fhandle.fs_ino;
	req.req.fchown.file_ino = arg1.fhandle.f_ino;
	req.req.fchown.owner = arg1.owner;
	req.req.fchown.group = arg1.group;

	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	init_opstatus(&result->status, &ack);
	return retval;
}

bool_t
capfs_fchmod_1_svc(fchmod_args arg1, fchmod_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_FCHMOD, &arg1.credentials);
	req.req.fchmod.fs_ino = arg1.fhandle.fs_ino;
	req.req.fchmod.file_ino = arg1.fhandle.f_ino;
	req.req.fchmod.mode = arg1.mode;
	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	init_opstatus(&result->status, &ack);
	return retval;
}

bool_t
capfs_rmdir_1_svc(rmdir_args arg1, rmdir_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_RMDIR, &arg1.credentials);
	req.dsize = strlen(arg1.name);
	buf_p = arg1.name;
	err = process_compat_req(&req, &ack, buf_p, &ackdata);
	
	init_opstatus(&result->status, &ack);

	return retval;
}

bool_t
capfs_access_1_svc(access_args arg1, access_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_ACCESS, &arg1.credentials);
	req.dsize = strlen(arg1.name);
	buf_p = arg1.name;
	req.req.access.mode = arg1.mode;
	req.req.access.to_follow = arg1.to_follow;
	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	init_opstatus(&result->status, &ack);
	copy_from_fmeta_to_fm(&ack.ack.access.meta, &result->meta);

	return retval;
}

bool_t
capfs_truncate_1_svc(truncate_args arg1, truncate_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_TRUNCATE, &arg1.credentials);
	req.dsize = strlen(arg1.name);
	buf_p = arg1.name;
	req.req.truncate.length = arg1.length;
	err = process_compat_req(&req, &ack, buf_p, &ackdata);
	
	init_opstatus(&result->status, &ack);
	result->old_length = ackdata.u.truncate.old_length;

	/* in case we are using the hcache, we need to possibly invalidate them on all client nodes */
	if (ack.status == 0 && arg1.desire_hcache_coherence == 1)
	{
		unsigned long bitmap;
		char *fname = NULL;

		bitmap = get_callbacks(ackdata.u.truncate.fs_ino, ackdata.u.truncate.f_ino, &fname);
		/*
		 * Invalidate the truncated part of the file in client nodes hcache
		 * if fname exists.
		 */
		if (fname != NULL && ackdata.u.truncate.begin_chunk >= 0)
		{
			/* We will let errors in the invalidate hashes routine slide for now */
			cb_invalidate_hashes(fname, bitmap, arg1.cb_id, ackdata.u.truncate.begin_chunk, ackdata.u.truncate.nchunks);
		}
	}
	return retval;
}

bool_t
capfs_utime_1_svc(utime_args arg1, utime_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_UTIME, &arg1.credentials);
	req.dsize = strlen(arg1.name);
	buf_p = arg1.name;
	req.req.utime.actime = arg1.actime;
	req.req.utime.modtime = arg1.modtime;
	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	init_opstatus(&result->status, &ack);

	return retval;
}

bool_t
capfs_getdents_1_svc(getdents_args arg1, getdents_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct capfs_dirent *pdir = NULL;
	struct ackdata ackdata;
	int err, i;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_GETDENTS, &arg1.credentials);
	req.dsize = strlen(arg1.name);
	buf_p = arg1.name;
	req.req.getdents.offset = arg1.offset;
	req.req.getdents.length = arg1.length;
	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	init_opstatus(&result->status, &ack);
	result->offset = ack.ack.getdents.offset;
	result->entries.dentries_len = 0;
	result->entries.dentries_val = NULL;
	if (ack.status == 0 && ((pdir = ackdata.u.getdents.pdir) != NULL)) {
		result->entries.dentries_len = ackdata.u.getdents.nentries;
		result->entries.dentries_val = (dentry *) 
			calloc(result->entries.dentries_len, sizeof(dentry));
		if (result->entries.dentries_val == NULL) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_META,  "getdents malloc failed\n");
			ack.status = -1;
			ack.eno = ENOMEM;
			return retval;
		}
		for (i = 0; i < ackdata.u.getdents.nentries; i++) {
			result->entries.dentries_val[i].handle = pdir[i].handle;
			result->entries.dentries_val[i].off = pdir[i].off;
			result->entries.dentries_val[i].entry = strdup(pdir[i].name);
		}
	}
	free(pdir);
	return retval;
}

bool_t
capfs_statfs_1_svc(statfs_args arg1, statfs_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_STATFS, &arg1.credentials);
	req.dsize = strlen(arg1.name);
	buf_p = arg1.name;
	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	init_opstatus(&result->status, &ack);

	result->tot_bytes = ack.ack.statfs.tot_bytes;
	result->free_bytes = ack.ack.statfs.free_bytes;
	result->tot_files = ack.ack.statfs.tot_files;
	result->free_files = ack.ack.statfs.free_files;
	result->namelen = ack.ack.statfs.namelen;

	return retval;
}

bool_t
capfs_lookup_1_svc(lookup_args arg1, lookup_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_LOOKUP, &arg1.credentials);
	req.dsize = strlen(arg1.name);
	buf_p = arg1.name;
	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	init_opstatus(&result->status, &ack);
	copy_from_fmeta_to_fm(&ack.ack.lookup.meta, &result->meta);

	return retval;
}

bool_t
capfs_ctime_1_svc(ctime_args arg1, ctime_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_CTIME, &arg1.credentials);
	req.dsize = strlen(arg1.name);
	buf_p = arg1.name;
	req.req.ctime.createtime = arg1.createtime;
	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	init_opstatus(&result->status, &ack);

	return retval;
}

bool_t
capfs_link_1_svc(link_args arg1, link_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err, len1 = 0, len2 = 0;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_LINK, &arg1.credentials);
	len1 = strlen(arg1.link_name);
	len2 = strlen(arg1.target_name);
	req.dsize = len1 + len2 + 1;
	buf_p = (char *) calloc(req.dsize + 1, 1);
	if (buf_p == NULL) {
		result->status.status = 0;
		result->status.eno = ENOMEM;
		return retval;
	}
	strcpy(buf_p, arg1.link_name);
	strcpy(&buf_p[len1+1], arg1.target_name);
	req.req.link.soft = arg1.soft;
	copy_from_fm_to_fmeta(&arg1.meta, &req.req.link.meta);

	err = process_compat_req(&req, &ack, buf_p, &ackdata);
	free(buf_p);
	init_opstatus(&result->status, &ack);

	return retval;
}

bool_t
capfs_readlink_1_svc(readlink_args arg1, readlink_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL, *plink = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_READLINK, &arg1.credentials);
	req.dsize = strlen(arg1.link_name);
	buf_p = arg1.link_name;
	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	init_opstatus(&result->status, &ack);
	/* this should be freed by the rpc layer, I think */
	result->link_name = (filename) calloc(CAPFS_MAXNAMELEN, 1);
	if (ack.status == 0 && ((plink = ackdata.u.readlink.link_name) != NULL)) {
		strcpy(result->link_name, plink);
		free(plink);
	}
	return retval;
}

bool_t
capfs_stat_1_svc(stat_args arg1, stat_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_STAT, &arg1.credentials);
	req.dsize = strlen(arg1.name);
	buf_p = arg1.name;
	err = process_compat_req(&req, &ack, buf_p, &ackdata);

	init_opstatus(&result->status, &ack);
	copy_from_fmeta_to_fm(&ack.ack.stat.meta, &result->meta);

	return retval;
}

bool_t
capfs_gethashes_1_svc(gethashes_args arg1, gethashes_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	/* FIXME: Need to add credentials to the RPC structure? */
	init_defaults(&req, MGR_GETHASHES, NULL);
	buf_p = (char *)arg1.type.hbytype_u.name;
	req.dsize = strlen(buf_p);
	req.req.gethashes.begin_chunk = arg1.begin_chunk;
	req.req.gethashes.nchunks = arg1.nchunks;
	err = process_compat_req(&req, &ack, buf_p, &ackdata);
	/* We need to add callbacks for this file */
	if (ack.status == 0 && arg1.cb_id >= 0)
	{
		add_callbacks(ack.ack.gethashes.meta.fs_ino, ack.ack.gethashes.meta.u_stat.st_ino, buf_p, arg1.cb_id);
	}
	init_opstatus(&result->status, &ack);
	copy_from_fmeta_to_fm(&ack.ack.gethashes.meta, &result->meta);
	result->h.sha1_hashes_len = 0;
	result->h.sha1_hashes_val = NULL;
	if (ack.status == 0 && ackdata.u.gethashes.hashes != NULL) {
		if (copy_sha1_hashes(&result->h, &ackdata) < 0) {
			result->status.status = -1;
			result->status.eno = ENOMEM;
		}
	}
	return retval;
}


static int wcommit_ctor(wcommit_args *arg1, struct ackdata* ackdata)
{
	int i;

	ackdata->u.wcommit.old_hash_len = arg1->old_hashes.sha1_hashes_len;
	if (ackdata->u.wcommit.old_hash_len > 0) {
		ackdata->u.wcommit.old_hashes = (unsigned char **) 
			calloc(ackdata->u.wcommit.old_hash_len, sizeof(unsigned char *));
		if (ackdata->u.wcommit.old_hashes == NULL) {
			return -ENOMEM;
		}
		for (i = 0; i < ackdata->u.wcommit.old_hash_len; i++) {
			ackdata->u.wcommit.old_hashes[i] = arg1->old_hashes.sha1_hashes_val[i];
		}
	}
	ackdata->u.wcommit.new_hash_len = arg1->new_hashes.sha1_hashes_len;
	if (ackdata->u.wcommit.new_hash_len > 0) {
		ackdata->u.wcommit.new_hashes = (unsigned char **)
			calloc(ackdata->u.wcommit.new_hash_len, sizeof(unsigned char *));
		if (ackdata->u.wcommit.new_hashes == NULL) {
			if (ackdata->u.wcommit.old_hashes) {
				free(ackdata->u.wcommit.old_hashes);
				return -ENOMEM;
			}
		}
		for (i = 0; i < ackdata->u.wcommit.new_hash_len; i++) {
			ackdata->u.wcommit.new_hashes[i] = arg1->new_hashes.sha1_hashes_val[i];
		}
	}
	return 0;
}

static void wcommit_dtor(struct ackdata *ackdata)
{
	if (ackdata->u.wcommit.old_hashes) {
		free(ackdata->u.wcommit.old_hashes);
	}
	if (ackdata->u.wcommit.new_hashes) {
		free(ackdata->u.wcommit.new_hashes);
	}
}

/*
 * FIXME: Should we guarantee that the wcommit function executes atomically?
 * We definitely ensure that the recipe file gets updated atomically.
 * But we don't have any mechanisms for ensuring that all hcache 
 * /updates happen automatically. Consequently, we resort to using
 * hcache invalidates instead of hcache updates for now.
 */
bool_t
capfs_wcommit_1_svc(wcommit_args arg1, wcommit_resp *result,  struct svc_req *rqstp)
{
	bool_t retval = 1;
	mreq req;
	mack ack;
	char *buf_p = NULL;
	struct ackdata ackdata;
	int err;

	memset(&req, 0, sizeof(req));
	memset(&ack, 0, sizeof(ack));
	memset(&ackdata, 0, sizeof(ackdata));
	init_defaults(&req, MGR_WCOMMIT, NULL);
	buf_p = (char *)arg1.type.hbytype_u.name;
	req.dsize = strlen(buf_p);
	req.req.wcommit.begin_chunk = arg1.begin_chunk;
	req.req.wcommit.write_size = arg1.write_size;
	ackdata.type = MGR_WCOMMIT;
	/* tell the wcommit to invalidate/update hcache if needed */
	ackdata.u.wcommit.desire_hcache_coherence = arg1.desire_hcache_coherence;
	ackdata.u.wcommit.owner_cbid = arg1.cb_id;
	ackdata.u.wcommit.force_commit = arg1.force_wcommit;

	result->current_hashes.sha1_hashes_len = 0;
	result->current_hashes.sha1_hashes_val = NULL;
	if (wcommit_ctor(&arg1, &ackdata) < 0) {
		result->status.status = -1;
		result->status.eno = ENOMEM;
		return retval;
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "[CB %d] Server commit with %d OLD hashes from %Ld\n",
		arg1.cb_id, arg1.old_hashes.sha1_hashes_len, arg1.begin_chunk);
#ifdef VERBOSE_DEBUG
	{
		int i;
		for (i = 0; i < arg1.old_hashes.sha1_hashes_len; i++) {
			char str[256];

			hash2str(arg1.old_hashes.sha1_hashes_val[i], CAPFS_MAXHASHLENGTH, str);
			LOG(stderr, DEBUG_MSG, SUBSYS_META, "%d: %s\n", i, str);
		}
	}
#endif
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "[CB %d] Server commit with %d NEW hashes from %Ld\n",
			arg1.cb_id, arg1.new_hashes.sha1_hashes_len, arg1.begin_chunk);
#ifdef VERBOSE_DEBUG
	{
		int i;
		for (i = 0; i < arg1.new_hashes.sha1_hashes_len; i++) {
			char str[256];

			hash2str(arg1.new_hashes.sha1_hashes_val[i], CAPFS_MAXHASHLENGTH, str);
			LOG(stderr, DEBUG_MSG, SUBSYS_META, "%d: %s\n", i, str);
		}
	}
#endif
	err = process_compat_req(&req, &ack, buf_p, &ackdata);
	
	init_opstatus(&result->status, &ack);
	copy_from_fmeta_to_fm(&ack.ack.wcommit.meta, &result->meta);
	wcommit_dtor(&ackdata);

	if (ackdata.u.wcommit.current_hashes != NULL) {
		if (copy_sha1_hashes(&result->current_hashes, &ackdata) < 0) {
			result->status.status = -1;
			result->status.eno = ENOMEM;
		}
	}
	LOG(stderr, DEBUG_MSG, SUBSYS_META, "Committed writes for file %s for %Ld bytes. file size is now %Ld\n",
			buf_p, arg1.write_size, result->meta.u_stat.st_size);

	return retval;
}

int
capfs_mgr_1_freeresult (SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
	xdr_free (xdr_result, result);


	return 1;
}
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
