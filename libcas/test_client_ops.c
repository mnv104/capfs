#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <openssl/evp.h>
#include <openssl/blowfish.h>
#include <capfs_config.h>
#include <netdb.h>
#include <errno.h>
#include <cas.h>
#include <semaphore.h>
#include <sys/statfs.h>

extern int errno;
extern int clnt_init(struct cas_options *options, int num_Iods, int blkSize);
extern void clnt_finalize(void);
extern void clnt_get(int tcp, struct cas_iod_worker_data *iod_jobs, int count);
extern void clnt_put(int tcp, struct cas_iod_worker_data *iod_jobs, int count);

static __inline void binToHex(unsigned char* bin, unsigned char hex[2])
{
	int i;
	unsigned char tmp;

	tmp = *bin;
	i = ((int) tmp) / 16;
	if (i < 10)
		hex[0] = '0' + i;
	else
		hex[0] = 'a' + (i - 10);
	i = ((int) tmp) % 16;
	if (i < 10)
		hex[1] = '0' + i;
	else
		hex[1] = 'a' + (i - 10);
}

static unsigned char *hexDigest(unsigned char *mesg, unsigned mesgLen)
{
	EVP_MD_CTX mdctx;
	const EVP_MD *md = EVP_sha1();
	unsigned char *md_value, *hexHash, *readPtr, *writePtr;
	
	int md_len, i;
	
	md_value = (unsigned char *) calloc(1, EVP_MAX_MD_SIZE);
	EVP_MD_CTX_init(&mdctx);
	EVP_DigestInit_ex(&mdctx, md, NULL);
	EVP_DigestUpdate(&mdctx, mesg, mesgLen);
	EVP_DigestFinal_ex(&mdctx, md_value, &md_len);
	EVP_MD_CTX_cleanup(&mdctx);
	
	hexHash = calloc(1, 42);
	readPtr = md_value;
	writePtr = hexHash;
	for(i = 0;i < CAPFS_MAXHASHLENGTH; i++, readPtr++, writePtr+=2)
		binToHex(readPtr, writePtr);
	*writePtr='\0';
	free(md_value);
	return hexHash;
}

static unsigned char *digest(unsigned char *mesg, unsigned mesgLen)
{
	EVP_MD_CTX mdctx;
	const EVP_MD *md = EVP_sha1();
	unsigned char *md_value;
	
	int md_len;
	
	md_value = (unsigned char *) calloc(1, EVP_MAX_MD_SIZE);
	EVP_MD_CTX_init(&mdctx);
	EVP_DigestInit_ex(&mdctx, md, NULL);
	EVP_DigestUpdate(&mdctx, mesg, mesgLen);
	EVP_DigestFinal_ex(&mdctx, md_value, &md_len);
	EVP_MD_CTX_cleanup(&mdctx);
	
	return md_value;
}

static int testHashes(void)
{
	int i, j, retVal = 0;
	unsigned char fname[32], hashName[32], *hash, hashFile[42], datafile[17000];

	for(i = 0; i< 1000; i++)
	{
		sprintf(fname,"/tmp/test/%d.test", i);
		sprintf(hashName,"/tmp/test/%d.sha", i);
		j = open(fname, O_RDONLY);
		read(j, datafile, 16384);
		close(j);
		hash = hexDigest(datafile, 16384);
		j = open(hashName, O_RDONLY);
		read(j, hashFile, 40);
		hashFile[40]=0;
		close(j);
		if (strcmp(hash, hashFile)!=0)
			return retVal;
		free(hash);
	}
	EVP_cleanup();
	return 1;
}

static struct sockaddr* openServer(char *serverName)
{
	struct sockaddr *serverAddress ;
	struct hostent *srvEntry;
	int srvPortNum = 7000;
	
	/* now create the server address */
	serverAddress = (struct sockaddr*) calloc(1, sizeof(struct sockaddr));
	if (!serverAddress) {
		perror("calloc:");
		exit(1);
	}
	bzero((char*)serverAddress, sizeof(struct sockaddr));
	if ( (srvEntry = gethostbyname(serverName)) == NULL)
	{
		perror("gethostbyname:");
		if (inet_aton(serverName,  &(((struct sockaddr_in*) serverAddress)->sin_addr))!=0)
		{
			perror("inet_aton");
			printf("giving up...\n");
			exit(1);
		};
	}
	else
	{
		bcopy(srvEntry->h_addr, (char *)&(((struct	sockaddr_in*) serverAddress)->sin_addr), srvEntry->h_length);
	}
	((struct sockaddr_in *)serverAddress)->sin_family = AF_INET;
	((struct sockaddr_in *)serverAddress)->sin_port = htons((u_short) srvPortNum);
	return serverAddress;
}

static int populateServer(struct sockaddr* serverAddress)
{
	int i, fd, bytesRead, retVal;
	unsigned char *ptr, *writePtr, hash[100], *data, fname[64];
	struct cas_iod_worker_data job;
	struct dataArray fData[5];
	struct cas_return cr;

	writePtr = hash;

	for(i = 0;i < 5;i++)
	{
		char str[256];

		sprintf(fname, "/tmp/test/%d.test", i);
		fd = open(fname, O_RDONLY);
		data = (unsigned char *)calloc(1, 16384);
		bytesRead = read(fd, data, 16384);
		close(fd);
		ptr = digest(data, 16384);
		fData[i].start = data;
		fData[i].byteCount = bytesRead;
		memcpy(writePtr, ptr, CAPFS_MAXHASHLENGTH);
		writePtr += CAPFS_MAXHASHLENGTH;
#if 0
		printf("PUT file name %s: ", fname);
		hash2str(ptr, CAPFS_MAXHASHLENGTH, str);
		printf("%s\n", str);
#endif
		free(ptr);
	}
	cr.buf = fData;
	cr.count = 5;
	job.hashes = hash;
	job.iodAddress = serverAddress;
	job.data = &cr;
	job.returnValue = &retVal;
	clnt_put(1, &job, 1);
	//cas_put(1, serverAddress, hash, &cr);
	printf("number of puts done:%d\n", retVal);
	return 0;
}

static int queryServer(struct sockaddr* serverAddress )
{
	int i, j, fd, bytesRead, retVal;
	unsigned char *tmp, *ptr, *writePtr, *in_fPtr[5],  hash[100], *data, fname[64];
	struct cas_iod_worker_data job;
	struct dataArray fData[5];
	struct cas_return cr;
	
	writePtr = hash;

	for(i = 0;i < 5;i++)
	{
		char str[256];

		sprintf(fname, "/tmp/test/%d.test", i);
		fd = open(fname, O_RDONLY);
		data = (unsigned char *)calloc(1, 16384);
		bytesRead = read(fd, data, 16384);
		close(fd);
		ptr = digest(data, 16384);
		memcpy(writePtr, ptr, CAPFS_MAXHASHLENGTH);
		writePtr += CAPFS_MAXHASHLENGTH;
#if 0
		printf("GET file name %s: ", fname);
		hash2str(ptr, CAPFS_MAXHASHLENGTH, str);
		printf("%s\n", str);
#endif
		free(ptr);
		in_fPtr[i]=data;
		fData[i].start = (unsigned char *) calloc(1, 16384);
	}
	cr.buf = fData;
	cr.count = 5;
	job.hashes = hash;
	job.iodAddress = serverAddress;
	job.data = &cr;
	job.returnValue = &retVal;
	clnt_get(1, &job, 1);
	//cas_get(1, serverAddress, hash, &cr);
	printf("number of gets done:%d\n", retVal);
	for(i = 0;i < 5;i++)
	{
		ptr = fData[i].start;
		tmp = in_fPtr[i];
		j = memcmp(ptr, tmp, fData[i].byteCount);
		if (j == 0)
			printf("File: %d Success\n", i);
		else
			printf("File: %d failed\n", i);
		free(ptr);
		free(tmp);
	}
		
	return 0;
}

int pingServer(struct sockaddr* serverAddress)
{
	if (clnt_ping(1, serverAddress) == 0) {
		printf("Server Unreachable\n");
		return -1;
	}
	else {
		printf("Server alive and kicking A$$\n");
		return 0;
	}
}

void statfsQuery(struct sockaddr* serverAddress)
{
	struct statfs sfs;
	int retVal;

	retVal = clnt_statfs_req(1, serverAddress, &sfs);
	if (retVal == 0)
	{
		printf("Free bytes: %Ld\n", ((int64_t)sfs.f_bavail)*((int64_t)sfs.f_bsize));
	}
	else
	{
		errno = retVal;
		perror("[statfs]");
	}
}

static int testServer(void)
{
	struct sockaddr* serverAddress = NULL;
	struct sockaddr* serverAddress2 = NULL;

	serverAddress = openServer("localhost");
	/*serverAddress2 = openServer("130.203.36.129");
	pingServer(serverAddress2);*/
	if (pingServer(serverAddress) < 0) {
		return 1;
	}
	populateServer(serverAddress);
	queryServer(serverAddress);
	statfsQuery(serverAddress);
	return 0;
}

int main(int argc, char *argv[])
{
	int c;
	struct cas_options cas_options = {
doInstrumentation:0,
use_sockets:0,
	};
	while ((c = getopt(argc, argv, "s")) != EOF)
	{
		switch (c) 
		{
			case 's':
				cas_options.use_sockets = 1;
				break;
		}
	}
	clnt_init(&cas_options, 1, 32*1024);
	return testServer();
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
