#ifndef _PEER_H
#define _PEER_H

#include <rpc/xdr.h>

struct t_args {
	char *ta_fname;
	int	ta_id;
	int   ta_status;
	int   ta_prog_number;
	int   ta_version;
};

#define LOCKCLNT 0x20000002
#define REVOKE   1
#define UPDATE   2

#endif
