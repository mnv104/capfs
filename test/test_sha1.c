#include "sha.h"

void print(unsigned char *a, int len)
{
	int i;
	for (i=0;i < len; i++) {
		printf("%02x", a[i]);
	}
	printf("\n");
}

int main(void)
{
	char *chunk = (char *) calloc(1, 256);
	unsigned char *hash = (char *) calloc(1, EVP_MAX_MD_SIZE);
	//unsigned char *hash = NULL;
	int i;
	
	sha1(chunk, 256, &hash, &i);
	print(hash, i);
	free(hash);
	free(chunk);
	return 0;
}
