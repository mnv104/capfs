/*
 * copyright (c) 1999 Rob Ross and Phil Carns, all rights reserved.
 *
 * Written by Rob Ross and Phil Carns, funded by Scyld Computing.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Contact:  Rob Ross    rbross@parl.clemson.edu
 *           Phil Carns  pcarns@parl.clemson.edu
 */

#include "capfs_kernel_config.h"
#include "capfs_linux.h"

/* Ugly hack needed for gcc 3.4.x and broken kernel headers */
#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 3)
static const char __module_kernel_version1[] __attribute_used__
       __attribute__((section(".modinfo"))) = "kernel_version=" UTS_RELEASE;
#ifdef CONFIG_MODVERSIONS
static const char __module_using_checksums1[] __attribute_used__ 
		 __attribute__((section(".modinfo"))) = "using_checksums=1";
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,9)
static const char __module_license1[] __attribute_used__ 
		 __attribute__((section(".modinfo"))) = "license=GPL";
#endif
#endif


MODULE_AUTHOR("Rob Ross <rbross@parl.clemson.edu> and Phil Carns <pcarns@parl.clemson.edu>");
MODULE_DESCRIPTION("capfs: CAPFS VFS support module");
MODULE_PARM(debug, "1i"); /* one integer debugging parameter */
MODULE_PARM_DESC(debug, "debugging level, 0 is no debugging output");
MODULE_PARM(maxsz, "1i"); /* one integer max size parameter */
MODULE_PARM_DESC(maxsz, "maximum request size, used to limit memory utilization");
MODULE_PARM(buffer, "s"); /* what kind of buffer to use to move data around */
MODULE_PARM_DESC(buffer, "buffering technique, static, dynamic, or mapped");
MODULE_PARM(major, "1i");	/* integer major number parameter */
MODULE_PARM_DESC(major, "major number module should attempt to use");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,9)
/* New License scheme */
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
#endif


/* we don't need any symbols exported, so let's keep the ksyms table
 * clean.
 */
EXPORT_NO_SYMBOLS;

static int init_capfs_fs(void);
int capfsdev_init(void);
void capfsdev_cleanup(void);

/* GLOBALS */
static int debug = -1;
static int maxsz = -1;
static char *buffer = NULL;
int capfs_debug = CAPFS_DEFAULT_DEBUG_MASK;
int capfs_buffer = CAPFS_DEFAULT_BUFFER;
int capfs_maxsz = CAPFS_DEFAULT_IO_SIZE;

static int major = 0;
int capfs_major = 0;

static int __init init_capfs(void)
{
	int i;
	char b_static[] = "static";
	char b_dynamic[] = "dynamic";
	char b_mapped[] = "mapped";
	char *buftype = NULL;
	PENTRY;

	/* parse module options, print a configuration line */
	if (debug > -1) capfs_debug = debug;
	if (maxsz >= CAPFS_MIN_IO_SIZE) capfs_maxsz = maxsz;
	if (major > 0) capfs_major = major;
	if (buffer != NULL) {
		if (strcmp(buffer, b_static) == 0)
			capfs_buffer = CAPFS_BUFFER_STATIC;
		else if (strcmp(buffer, b_mapped) == 0)
			capfs_buffer = CAPFS_BUFFER_MAPPED;
		else if (strcmp(buffer, b_dynamic) == 0)
			capfs_buffer = CAPFS_BUFFER_DYNAMIC;
		else {
			printk("buffer value %s is invalid; defaulting to %s\n",
			buffer, b_dynamic);
			capfs_buffer = CAPFS_BUFFER_DYNAMIC;
		}
	}
	switch (capfs_buffer) {
		case CAPFS_BUFFER_DYNAMIC: buftype = b_dynamic; break;
		case CAPFS_BUFFER_MAPPED:  buftype = b_mapped;  break;
		case CAPFS_BUFFER_STATIC:  buftype = b_static;  break;
		default: printk("capfs: init_module: should not hit here\n");
	}
	
	printk("capfs: debug = 0x%x, maxsz = %d bytes, buffer = %s, major = %d\n", 
		capfs_debug, capfs_maxsz, buftype, capfs_major);

	/* initialize all the good stuff */
	i = capfsdev_init();
	if (i){
		return i;
		PEXIT;
	}
	i = init_capfs_fs();
	if(i) {
		capfsdev_cleanup();
	}
	PEXIT;
	return i;
}

static void __exit cleanup_capfs(void)
{
	PENTRY;
	unregister_filesystem(&capfs_fs_type);
	capfsdev_cleanup();
	PEXIT;
}

static int init_capfs_fs(void)
{
	int ret = -1;
	PENTRY;
	ret = register_filesystem(&capfs_fs_type);
	PEXIT;
	return(ret); 
}

module_init(init_capfs);
module_exit(cleanup_capfs);
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 *
 * vim: ts=3
 * End:
 */ 
