/*
 * plugin.c
 *
 * Copyright (C) 2005 for capfs by Murali Vilayannur (vilayann@cse.psu.edu)
 */
#include "capfs-header.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h> /* Header file for dynamic loading of shared libs */
#include <malloc.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <sys/types.h>

#include "log.h"
#include "plugin.h"
#include "list.h"

#ifndef RTLD_NOW
#define RTLD_NOW 0
#endif

#ifndef SHARED_LIB_EXT 
#define SHARED_LIB_EXT ".so"
#endif

#ifdef PLUGIN_DIR
#define PLUGIN_PATH PLUGIN_DIR
#else
#define PLUGIN_PATH "./client/"
#endif

#define _STRING(x) #x
#define STRING(x)  _STRING(x)

static pthread_mutex_t plugin_lock = PTHREAD_MUTEX_INITIALIZER;

static char *plugin_dir_list[] = 
{
	/* relative paths are only listed for now. */
	"plugins", NULL
};

static LIST_HEAD(plugin_list);

static void *open_dll(char *filename)
{
	return dlopen(filename, RTLD_NOW);
}

static void close_dll(void *handle)
{
	dlclose(handle);
	return;
}

static void* find_symbol(void *handle, void *symbol)
{
	return dlsym(handle, symbol);
}

static void dll_error(void)
{
	LOG(stderr, CRITICAL_MSG, SUBSYS_CLIENT, "%s\n", dlerror());
	return;
}

static void list_add_unique(struct plugin_info *pinfo, char *filename, void *handle)
{
	/* iterate thru the rest of the list and make sure that there are no duplicate policy_names */
	struct list_head *tmp;

	for (tmp = plugin_list.next; tmp != &plugin_list; tmp = tmp->next) {
		struct plugin_info *p = list_entry(tmp, struct plugin_info, next);
		/* Duplicate policy names! */
		if (strcmp(p->policy_name, pinfo->policy_name) == 0) {
			LOG(stderr, INFO_MSG, SUBSYS_CLIENT, "Disallowing loading of %s due to collission in policy names\n",
					filename);
			return;
		}
		/* Duplicate policy identifiers */
		if (p->policy_id == pinfo->policy_id) {
			LOG(stderr, INFO_MSG, SUBSYS_CLIENT, "Disallowing loading of %s due to collission in policy ids\n",
					filename);
			return;
		}
	}
	pinfo->handle = handle;
	pinfo->filename = strdup(filename);
	list_add_tail(&pinfo->next, &plugin_list);
	return;
}

static void add_plugin(char *filename)
{
	void *handle;
	void *(*pfn) (void);

	if ((handle = open_dll(filename)) == NULL) {
		dll_error();
		return;
	}
	if ((pfn = find_symbol(handle, "get_plugin_info")) != NULL) {
		struct plugin_info *pinfo = pfn();
		list_add_unique(pinfo, filename, handle);
	}
	else {
		close_dll(handle);
	}
}

static void scan_plugins(char *dirname)
{
	DIR *dir;
	char *filename, *ext;
	struct dirent *ent;
	struct stat statbuf;

	dir = opendir(dirname);
	if (!dir) {
		return;
	}
	while ((ent = readdir(dir)) != NULL) {
		filename = (char *) calloc(PATH_MAX, sizeof(char));
		if (filename == NULL) {
			closedir(dir);
			return;
		}
		sprintf(filename, "%s/%s", dirname, ent->d_name);
		if (!stat(filename, &statbuf) && S_ISREG(statbuf.st_mode) && (ext = strrchr(ent->d_name, '.')) != NULL) {
			if (!strcmp(ext, SHARED_LIB_EXT)) {
				add_plugin(filename);
			}
		}
		free(filename);
	}
	closedir(dir);
	return;
}

int capfsd_plugin_init(void)
{
	int dirsel = 0;
	struct list_head *tmp = NULL;

	pthread_mutex_lock(&plugin_lock);
	while (plugin_dir_list[dirsel]) {
		char dir[PATH_MAX];

		sprintf(dir, "%s/%s", STRING(PLUGIN_PATH), plugin_dir_list[dirsel++]);
		scan_plugins(dir);
	}
	for (tmp = plugin_list.next; tmp != &plugin_list; tmp = tmp->next) {
		/* initialize all the plugins if need be */
		struct plugin_info *pinfo = list_entry(tmp, struct plugin_info, next);
		if (pinfo && pinfo->policy_ops && pinfo->policy_ops->init) {
			pinfo->policy_ops->init();
		}
	}
	pthread_mutex_unlock(&plugin_lock);
	return 0;
}

int capfsd_plugin_cleanup(void)
{
	pthread_mutex_lock(&plugin_lock);
	while (plugin_list.next != &plugin_list) {
		struct plugin_info *pinfo = list_entry(plugin_list.next, struct plugin_info, next);
		list_del(plugin_list.next);
		if (pinfo && pinfo->policy_ops && pinfo->policy_ops->cleanup) {
			pinfo->policy_ops->cleanup();
		}
		free(pinfo->filename);
		close_dll(pinfo->handle);
	}
	pthread_mutex_unlock(&plugin_lock);
	return 0;
}

struct plugin_info* capfsd_match_policy_name(char *policy)
{
	struct list_head *tmp = NULL;

	pthread_mutex_lock(&plugin_lock);
	/* iterate thru the list of plugins to see if we have a match */
	for (tmp = plugin_list.next; tmp != &plugin_list; tmp = tmp->next) {
		struct plugin_info *pinfo;

		pinfo = list_entry(tmp, struct plugin_info, next);
		if (pinfo && !strcmp(pinfo->policy_name, policy)) {
			pthread_mutex_unlock(&plugin_lock);
			return pinfo;
		}
	}
	pthread_mutex_unlock(&plugin_lock);
	return NULL;
}

struct plugin_info* capfsd_match_policy_id(int id)
{
	struct list_head *tmp = NULL;

	pthread_mutex_lock(&plugin_lock);
	/* iterate thru the list of plugins to see if we have a match */
	for (tmp = plugin_list.next; tmp != &plugin_list; tmp = tmp->next) {
		struct plugin_info *pinfo;

		pinfo = list_entry(tmp, struct plugin_info, next);
		if (pinfo && pinfo->policy_id == id) {
			pthread_mutex_unlock(&plugin_lock);
			return pinfo;
		}
	}
	pthread_mutex_unlock(&plugin_lock);
	return NULL;
}
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 *
 * vim: ts=3
 */

