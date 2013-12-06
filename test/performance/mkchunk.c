#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <openssl/evp.h>

#define HASHLEN 20

static void usage(void)
{
	printf("mkchunk <source-dir> <chunksize(KB)>\n");
	printf("Traverses the source directory specified and for every	file encountered, creates a recipie of hashes.\n");
	printf("The recipie file has the same filename with an appended .recipie extension.\n");
	printf("Every %d bytes in the recipie file corresponds to a chunk file filename.n\n", HASHLEN);
	exit(0);
}

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

char* mkchunk(int argc, char *argv[])
{
	char sourceBase[1024], *tmpFile, recipieFile[1024], tmpString[1024];
	char *line, dataFileName[1024], *hash;
	int len, chunksize, retVal, err, fSize, recipieFd, bytesRead;
	struct stat statbuf;
	FILE* cFile;
	
	if (argc<3)
		usage();
	sourceBase[0]=0;
	strcpy(sourceBase,argv[1]);
	retVal=stat(sourceBase, &statbuf);
	tmpFile=malloc(1024);
	err=errno;
	if (retVal!=0)
	{
		printf("Bad source path. Retval: %d Errno: %d\n",retVal, err);
		usage();
	}
	chunksize=atoi(argv[2])*1024;
	sprintf(tmpFile,"%s","/tmp/tmpfile.XXXXXX"); 
	mkstemp(tmpFile); 
	sprintf(tmpString,"find %s -type f -print>%s", sourceBase, tmpFile);
	system(tmpString);
	OpenSSL_add_all_digests();
	cFile=fopen(tmpFile,"r");
	if(!cFile)
	{
		printf("error: couldnt open %s\n", tmpFile);
		usage();
	};

	while ((bytesRead = getline(&line, &len, cFile)) != -1) {
		int numChunks, i, fd, bytesDone;
		char chunkName[1024];
		unsigned char* file_addr, *readPtr;

		chunkName[0]=0;
		dataFileName[0]=0;
		sscanf(line,"%s",dataFileName);
		if (strlen(dataFileName)<1)
		{
			printf("Read 0 chars as filename in source tree\n");
			fclose(cFile);
			remove(tmpFile);
			exit(0);
		}
		if ((fd=open(dataFileName,O_RDONLY))<3)
		{
			printf("Couldnt open source file %s\n",dataFileName);
			fclose(cFile);
			remove(tmpFile);
			exit(0);
		}
		if (fstat(fd, &statbuf) < 0) {
			printf("stat failed on %s\n",dataFileName);
			fclose(cFile);
			remove(tmpFile);
			exit(0);
		}
		sprintf(recipieFile,"%s.recipie",dataFileName); 
		if ( (recipieFd=open(recipieFile, O_WRONLY|O_APPEND|O_TRUNC|O_CREAT,
						0600))<3)
		{
			printf("Couldnt open recipie file %s\n",recipieFile);
			fclose(cFile);
			remove(tmpFile);
			exit(0);
		}

		fSize = statbuf.st_size;
		numChunks = statbuf.st_size / chunksize + 1;
		file_addr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
		if (file_addr == MAP_FAILED) {
			close(fd);
			close(recipieFd);
			fclose(cFile);
			remove(tmpFile);
			exit(0);
		}
		readPtr=file_addr;
		bytesDone=0;
		for(i=0;i<numChunks;i++)
		{
			int chunkfd, currentRead;

			if (bytesDone+chunksize>fSize)
				currentRead=fSize-bytesDone;
			else
				currentRead=chunksize;
			sprintf(chunkName,"%s.chunk%06d",dataFileName,i);
			if ((chunkfd=open(chunkName,O_WRONLY|O_TRUNC|O_CREAT,0600))<3)
			{
				printf("Couldnt open chunk file %s\n",chunkName);
				munmap(file_addr, statbuf.st_size);
				close(fd);
				close(recipieFd);
				fclose(cFile);
				remove(tmpFile);
				exit(0);
			}
			write(chunkfd,readPtr,currentRead);
			hash=digest(readPtr,currentRead);
			write(recipieFd,hash,HASHLEN);
			free(hash);
			readPtr+=currentRead;
			bytesDone+=currentRead;
			close(chunkfd);
		}
		munmap(file_addr, statbuf.st_size);
		close(recipieFd);
		close(fd);
	}
	if (line)
		free(line);
	EVP_cleanup();
	fclose(cFile);
	printf ("Original file list in %s\n",tmpFile);
	return tmpFile;
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

