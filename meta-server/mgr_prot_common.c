#include "mgr_prot_common.h"

void copy_from_capfs_stat_to_pstat(struct capfs_stat *capfs_stat, p_stat *u_stat)
{
	u_stat->st_size = capfs_stat->st_size;
	u_stat->st_ino = capfs_stat->st_ino;
	u_stat->atime = capfs_stat->atime;
	u_stat->mtime = capfs_stat->mtime;
	u_stat->ctime = capfs_stat->ctime;
	u_stat->st_mode = capfs_stat->st_mode;
	u_stat->st_uid = capfs_stat->st_uid;
	u_stat->st_gid = capfs_stat->st_gid;
	return;
}

void copy_from_pstat_to_capfs_stat(p_stat *u_stat, struct capfs_stat *capfs_stat)
{
	capfs_stat->st_size = u_stat->st_size;
	capfs_stat->st_ino = u_stat->st_ino;
	capfs_stat->atime = u_stat->atime;
	capfs_stat->mtime = u_stat->mtime;
	capfs_stat->ctime = u_stat->ctime;
	capfs_stat->st_mode = u_stat->st_mode;
	capfs_stat->st_uid = u_stat->st_uid;
	capfs_stat->st_gid = u_stat->st_gid;
	return;
}

void copy_from_capfs_filestat_to_pfilestat(struct capfs_filestat *capfs_filestat, p_filestat *p_stat)
{
	p_stat->base = capfs_filestat->base;
	p_stat->pcount = capfs_filestat->pcount;
	p_stat->ssize = capfs_filestat->ssize;
	return;
}

void copy_from_pfilestat_to_capfs_filestat(p_filestat *p_stat, struct capfs_filestat *capfs_filestat)
{
	capfs_filestat->base = p_stat->base;
	capfs_filestat->pcount = p_stat->pcount;
	capfs_filestat->ssize = p_stat->ssize;
	return;
}

void copy_from_fmeta_to_fm(fmeta *fmeta_p, fm *fmp)
{
	fmp->fs_ino = fmeta_p->fs_ino;

	copy_from_capfs_stat_to_pstat(&fmeta_p->u_stat, &fmp->u_stat);
	copy_from_capfs_filestat_to_pfilestat(&fmeta_p->p_stat, &fmp->p_stat);
	return;
}

void copy_from_fm_to_fmeta(fm *fmp, fmeta *fmeta_p)
{
	fmeta_p->fs_ino = fmp->fs_ino;

	copy_from_pstat_to_capfs_stat(&fmp->u_stat, &fmeta_p->u_stat);
	copy_from_pfilestat_to_capfs_filestat(&fmp->p_stat, &fmeta_p->p_stat);
	return;
}


