/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See COPYING in top-level directory.
 */


#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <linux/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/param.h>
#include <meta.h>
#include <log.h>

/* put_dmeta()
 *
 * This function now assumes that it is passed a directory name.  It
 * appends "/.capfsdir" to this name and uses that as the capfsdir
 * filename.
 *
 * When writing out the metadata, it relies on the fname passed in
 * having the same root as the one in the dir structure.
 */
int put_dmeta(char * fname, dmeta_p dir)
{
	int length;
	char temp[MAXPATHLEN];
	FILE *fp;
	mode_t old_umask;

   /* build name of dotfile */
   strncpy(temp, fname, MAXPATHLEN-1);
   length = strlen(temp);

   /* open dotfile */
	if (length + 9 >= MAXPATHLEN-1) {
		return(-1);
	}
   strcat(temp, "/.capfsdir");
   old_umask = umask(0033);

	/* should use the dmeta_xxx calls */
   if ((fp = fopen(temp, "w+")) == 0) {
      PERROR(SUBSYS_META,"Error opening dot file");
      
		umask(old_umask);
      return(-1);
   }

   /* write dotfile */
   fprintf(fp,"%Ld\n%d\n%d\n%07o\n%d\n%s\n%s\n",
			  dir->fs_ino,
			  dir->dr_uid,
			  dir->dr_gid,
			  dir->dr_mode,
			  dir->port,
			  dir->host,
			  dir->rd_path);
   fclose(fp);
	umask(old_umask);

   /* return data */
   return 0;
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
