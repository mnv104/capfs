#ifndef _MGR_PROT_COMMON_H
#define _MGR_PROT_COMMON_H

#include "mgr_prot.h"
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <req.h>
#include <capfs_config.h>
#include <meta.h>
#include <desc.h>

extern void copy_from_capfs_stat_to_pstat(struct capfs_stat *capfs_stat, p_stat *u_stat);
extern void copy_from_pstat_to_capfs_stat(p_stat *u_stat, struct capfs_stat *capfs_stat);
extern void copy_from_capfs_filestat_to_pfilestat(struct capfs_filestat *capfs_filestat, p_filestat *p_stat);
extern void copy_from_pfilestat_to_capfs_filestat(p_filestat *p_stat, struct capfs_filestat *capfs_filestat);
extern void copy_from_fmeta_to_fm(fmeta *fmeta_p, fm *fmp);
extern void copy_from_fm_to_fmeta(fm *fmp, fmeta *fmeta_p);

#endif
