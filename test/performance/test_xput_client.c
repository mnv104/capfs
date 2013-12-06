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

static unsigned char *digest(unsigned char *mesg, unsigned mesgLen)
{
	EVP_MD_CTX mdctx;
	const EVP_MD *md;
	unsigned char *md_value;
	
	int md_len;
	
	md_value = (unsigned char *) malloc(EVP_MAX_MD_SIZE);
	md = EVP_get_digestbyname("sha1");
	EVP_MD_CTX_init(&mdctx);
	EVP_DigestInit_ex(&mdctx, md, NULL);
	EVP_DigestUpdate(&mdctx, mesg, mesgLen);
	EVP_DigestFinal_ex(&mdctx, md_value, &md_len);
	EVP_MD_CTX_cleanup(&mdctx);
	
	return md_value;
}

static struct sockaddr* openServer(char *serverName)
{
	struct sockaddr *serverAddress ;
	struct hostent *srvEntry;
	int srvPortNum=IOD_BASE_PORT;
	
	/* now create the server address */
	serverAddress = (struct sockaddr*)malloc(sizeof(struct sockaddr));
	if (!serverAddress) {
		perror("malloc:");
		exit(1);
	}
	bzero((char*)serverAddress,sizeof(struct sockaddr));
	if ( (srvEntry=gethostbyname(serverName))==NULL)
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
		bcopy(srvEntry->h_addr,(char *)&(((struct	sockaddr_in*) serverAddress)->sin_addr), srvEntry->h_length);
	}
	((struct sockaddr_in *)serverAddress)->sin_family = AF_INET;
	((struct sockaddr_in *)serverAddress)->sin_port = htons((u_short) srvPortNum);
	return serverAddress;
}

static int usage(void)
{
	printf("./client <file containing fileList> <chunksizeKB> <list of iod servers>\n");
	exit(0);
}

int main(int argc, char *argv[])
{
	FILE* cFile;
	char *line=NULL, currentFile[1024], recipieFile[1024], *hash;
	char chunkName[1024];
	int thisChunkSize, chunkfd;
	struct stat statbuf;
	int numIODS = 0, len, i, numChunks, chunkNumber, chunksize = 0, retVal, err, fSize, j;
	int recipieFd, bytesRead, myHashes, hashesPerIOD, iodsWithExtraHash;
	int iodsUsed = 0;
	struct cas_iod_worker_data *jobs, *jobPtr;
	struct dataArray *da_workerData, *daPtr;
	struct cas_return *cr_workerData;
	int c, errorNum;
	struct cas_options cas_options = {
doInstrumentation:0,
use_sockets:0,
	};

	while ((c = getopt(argc, argv, "sn:c:")) != EOF)
	{
		switch (c) 
		{
			case 'c':
				chunksize = atoi(optarg) * 1024;
				break;
			case 's':
				cas_options.use_sockets = 1;
				break;
			case 'n':
				numIODS = atoi(optarg);
				break;
		}
	}
	clnt_init(&cas_options, numIODS, chunksize);
	jobs=(struct cas_iod_worker_data*)malloc(numIODS*sizeof(struct cas_iod_worker_data));
	for(i=0;i<numIODS;i++)
	{
		jobs[i].returnValue = (int*)malloc(sizeof(int));
		jobs[i].iodAddress=openServer(argv[3+i]);
	}
	cFile = fopen(argv[1],"r");
	if(!cFile)
	{
		printf("error: couldnt open %s\n", argv[1]);
		usage();
	};
	//OpenSSL_add_all_digests();
	while ((bytesRead = getline(&line, &len, cFile)) != -1) {
		sscanf(line, "%s", currentFile);
		sprintf(recipieFile,"%s.recipie",currentFile);
		if ( (recipieFd=open(recipieFile, O_RDONLY))<3)
		{
			printf("Couldnt open recipie file %s\n",recipieFile);
			fclose(cFile);
			exit(0);
		}
		if (fstat(recipieFd, &statbuf) < 0) {
			printf("stat failed on %s\n",recipieFile);
			fclose(cFile);
			close(recipieFd);
			exit(0);
		}
		fSize = statbuf.st_size;
		if (fSize % CAPFS_MAXHASHLENGTH)
		{
			printf("Recipie file for %s gone bad", currentFile);
			fclose(cFile);
			close(recipieFd);
			exit(0);
		}

		close(recipieFd);
		
		numChunks = fSize/CAPFS_MAXHASHLENGTH;
		hashesPerIOD = numChunks/numIODS+1;
		iodsWithExtraHash = numChunks%numIODS;
		chunkNumber=0;
		iodsUsed=0;
		for(i=0;i<numIODS && chunkNumber<numChunks;i++,iodsUsed++)
		{
			/*create the jobs*/
			jobs[i].hashes=NULL;
			cr_workerData=(struct cas_return*)malloc(sizeof(struct cas_return));
			jobs[i].data=cr_workerData;
			if (i<iodsWithExtraHash)
				cr_workerData->count=hashesPerIOD;
			else
				cr_workerData->count=hashesPerIOD-1;
			da_workerData=(struct dataArray*)malloc(cr_workerData->count*sizeof(struct dataArray));
			cr_workerData->buf=da_workerData;
			for(j=0;j<cr_workerData->count && chunkNumber<numChunks;j++)
			{
				sprintf(chunkName,"%s.chunk%06d", currentFile, chunkNumber);
				if ((chunkfd=open(chunkName,O_RDONLY))<3)
				{
					errorNum=errno;
					printf("Couldnt open chunk file %s\n",chunkName);
					errno=errorNum;
					perror("[open]");
					exit(0);
				}
				if (fstat(chunkfd,&statbuf)<0)
				{
					printf("Couldnt fstat chunk file %s\n",chunkName);
					exit(0);
				}
				da_workerData[j].byteCount=statbuf.st_size;
				da_workerData[j].start=malloc(statbuf.st_size);
				if (read(chunkfd,da_workerData[j].start,statbuf.st_size)
						!=statbuf.st_size)
				{
					printf("Could not read %ld bytes from %s\n",statbuf.st_size,
							chunkName);
					exit(0);
				}
				close(chunkfd);	
				chunkNumber++;
			}
		}
		clnt_put(1, jobs, iodsUsed);
		/*  this test is bad - it fails if numIODS is large and
		*   we have a small number of chunks - so a few job slots
		*   remain unused
		for(i=0;i<iodsUsed;i++)
			if (*(jobs[i].returnValue)<1)
				printf("Recieved error %d for file %s\n",*(jobs[i].returnValue), currentFile);
		*/
		for(i=0;i<iodsUsed ;i++)
		{
			jobPtr=&(jobs[i]);
			free(jobPtr->hashes);
			daPtr=jobPtr->data->buf;
			for(j=0;j<jobPtr->data->count;j++)
				free(daPtr[j].start);
			free(daPtr);
			free(jobPtr->data);
			jobPtr->data=NULL;
		}
	}
	fclose(cFile);
	if (line)
		free(line);
	//clnt_put(1, jobs, iodsUsed);
	clnt_finalize();

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
