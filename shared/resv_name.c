/*
 * (C) 1995-2001 Clemson University and Argonne National Laboratory.
 *
 * See LIBRARY_COPYING in top-level directory.
 */


#include <stdio.h>
#include <string.h>
#include <errno.h>

/* resv_name(name)
 *
 * Returns non-zero if a name corresponds to a CAPFS reserved name, 0
 * otherwise.
 *
 * At this time, the names ".iodtab" and ".capfsdir" 
 * and any file that has a ".hashes" appended to the end of the file
 * name are reserved file names.
 */
int resv_name(const char *name)
{
	int len, i;
	char *ptr;

	len = strlen(name); /* not including terminator */
	//if (len < 7) return 0; /* our names are longer than this one */

	if (name[0] == '/' || (name[0] == '.' && name[1] == '.' && name[2] == '/')
	|| (name[0] == '.' && name[1] == '/'))
	{
		/* gotta handle directories */
		for (i = len-1; i >= 0; i--) if (name[i] == '/') break;
		/* position i of name has '/' */
		//if (i > len - 7) return 0; /* again, our names are longer than this */

		i++; /* point to the first character of the name */
		if (!strcmp(&name[i], ".iodtab")
		|| !strcmp(&name[i], ".capfsdir"))
			return 1;
		ptr = (char *)&name[i];
		if ((ptr = strstr(&name[i], ".hashes")) != NULL) {
			/* also there should be a '\0' at the end of the .hashes */
			if (*(char *)(ptr + strlen(".hashes")) == '\0') {
				return 1;
			}
		}
	}
	else {
		/* no directories */
		if (!strcmp(name, ".iodtab") || !strcmp(name, ".capfsdir")) 
			return 1;
		if ((ptr = strstr(name, ".hashes")) != NULL) {
			/* also there should be a \0 at the end of the .hashes */
			if (*(char *)(ptr + strlen(".hashes")) == '\0') {
				return 1;
			}
		}
	}

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
