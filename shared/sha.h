#ifndef _SHA_H
#define _SHA_H

#include <sys/types.h>
#include <openssl/evp.h>

extern void sha1_init(void);
extern void sha1_finalize(void);
/* remember to free return value of digest */
extern unsigned char *digest(unsigned char *mesg, unsigned mesgLen);
/* remember to free *output_hash after sha1 is called */
extern int sha1(char *input_message, size_t input_length, unsigned char **output_hash, size_t *output_length);
extern void print_hash(unsigned char *, int);
extern void hash2str(unsigned char *hash, int hash_length, unsigned char *str);

#endif
