#ifndef _IOD_PROT_CLIENT_H
#define _IOD_PROT_CLIENT_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/statfs.h>
#include "cas.h" /* pull in the signatures for struct cas_return */

extern int cas_ping(int use_sockets, int tcp, struct sockaddr_in *addr);
extern int cas_statfs(int use_sockets, int tcp, struct sockaddr_in *addr, struct statfs *sfs);
extern int cas_put(int use_sockets, int tcp, struct sockaddr_in *addr, unsigned char *hashes, struct cas_return *ret);
extern int cas_get(int use_sockets, int tcp, struct sockaddr_in *addr, unsigned char *hashes, struct cas_return *ret);
extern int cas_removeall(int use_sockets, int tcp, struct sockaddr_in *addr, char *dirname);

#endif
