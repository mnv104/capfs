#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <errno.h>
#include <pthread.h>
#include <linux/unistd.h>
#include "log.h"

_syscall0(pid_t,gettid)

static pthread_mutex_t *mutex = NULL;
static pthread_once_t once_initialize = PTHREAD_ONCE_INIT;
static pthread_once_t once_finalize = PTHREAD_ONCE_INIT;

static void do_lock(int mode, int n, const char *file, int line)
{
	if (mode & CRYPTO_LOCK) {
		pthread_mutex_lock(&mutex[n]);
	}
	else {
		pthread_mutex_unlock(&mutex[n]);
	}
	return;
}

static unsigned long get_tid(void)
{
	return gettid();
}

static void callback_init(void)
{
	int i, num_locks = 0;

	num_locks = CRYPTO_num_locks();
	mutex = (pthread_mutex_t *) calloc(num_locks, sizeof(pthread_mutex_t));
	for (i = 0; i < num_locks; i++) {
		pthread_mutex_init(&mutex[i], NULL);
	}
	CRYPTO_set_locking_callback(do_lock);
	CRYPTO_set_id_callback(get_tid);
	return;
}

void sha1_init(void)
{
	pthread_once(&once_initialize, callback_init);
	return;
}

static void callback_finalize(void)
{
	free(mutex);
	mutex = NULL;
}

void sha1_finalize(void)
{
	pthread_once(&once_finalize, callback_finalize);
	return;
}

unsigned char *digest(unsigned char *mesg, unsigned mesgLen)
{
    EVP_MD_CTX mdctx;
    const EVP_MD *md = EVP_sha1();
    unsigned char *md_value;

    int md_len;

    md_value = (unsigned char *) malloc(EVP_MAX_MD_SIZE);

    EVP_MD_CTX_init(&mdctx);
    EVP_DigestInit_ex(&mdctx, md, NULL);
    EVP_DigestUpdate(&mdctx, mesg, mesgLen);
    EVP_DigestFinal_ex(&mdctx, md_value, &md_len);
    EVP_MD_CTX_cleanup(&mdctx);

    return md_value;
}

int sha1(char *input_message, size_t input_length, unsigned char **output_hash, size_t *output_length)
{
	EVP_MD_CTX *digest = NULL;
	const EVP_MD *type = EVP_sha1();

	/* OpenSSL_add_all_digests(); */
	if (*output_hash == NULL) {
		*output_hash = (char *) calloc(sizeof(unsigned char), EVP_MAX_MD_SIZE);
	}
	if (!*output_hash) {
		return -ENOMEM;
	}
	digest = EVP_MD_CTX_create();
	if (digest == NULL) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_SHARED, "Could not create EVP_MD_CTX\n");
		return -EINVAL;
	}
	EVP_DigestInit(digest, type);
	EVP_DigestUpdate(digest, input_message, input_length);
	EVP_DigestFinal(digest, *output_hash, output_length); 
	EVP_MD_CTX_destroy(digest);
	return 0;
}

void hash2str(unsigned char *hash, int hash_length, unsigned char *str)
{
	int i, count = 0;

	if (!str || !hash || hash_length < 0) {
		LOG(stderr, WARNING_MSG, SUBSYS_SHARED, "Invalid parameter\n");
		return;
	}
	for (i = 0; i < hash_length; i++) {
		int cnt;
		cnt = sprintf(str + count, "%02x", hash[i]);
		count += cnt;
	}
	return;
}

void print_hash(unsigned char *hash, int hash_length)
{
	int i, count = 0;
	unsigned char str[256]; /* 41 bytes is sufficient.. but still */

	for (i = 0; i < hash_length; i++) {
		int cnt;
		cnt = snprintf(str + count, 256, "%02x", hash[i]);
		count += cnt;
	}
	LOG(stdout, DEBUG_MSG, SUBSYS_NONE, "%s\n", str);
	return;
}
