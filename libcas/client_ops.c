#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <unistd.h>
#include <string.h>
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
#include "log.h"
#include "cas.h"
#include "sha.h"
#include "tp_proto.h"
#include <semaphore.h>
#include <sockio.h>
#include <sys/statfs.h>
#include <sys/time.h>
#include <pthread.h>
#include <desc.h>
#include <assert.h>
#include "ll_capfs.h"
#include "iod_prot.h"
#include "iod_prot_client.h"

#define USED     1
#define UNUSED   0

/* are we resorting to socket based approach */
static int use_sockets = 0;
/* This value is overridden by the clnt_init() call */
static int iod_blk_size = 32*1024; 

pthread_spinlock_t seq_lock;

/* one time initialization variable */
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_key_t  key;

struct thread_input {
	int tcp;
	unsigned char* hashBlock;/*an array of hashes */
	struct cas_return *data;
	struct sockaddr *serverAddress;
	sem_t* countingSem;
	int *returnValue;
};

/* we use a thread pool, one thread for each iod */
static int numIods;
static tp_info poolInfo;
static tp_id   poolID;

struct keyInit_s {
	sem_t *value; /* given by calling thread */
	/* sem_t *guard;  guards the "count" integer */
	int *count;   /* how many of the instr slots are used, starting from 0 */
};

static pthread_mutex_t instrPoolMutex = PTHREAD_MUTEX_INITIALIZER;
static struct instrumentation_s *instrPool;
struct instrumentation_s put_criticalPathTime;
struct instrumentation_s get_criticalPathTime;

static int doInstrumentation = 1;
int64_t server_get_time[CAPFS_STATS_MAX], server_put_time[CAPFS_STATS_MAX];

static inline void initInstr(void)
{
	struct instrumentation_s *instr;
	if (doInstrumentation)
	{
		int i;
		for (i = 0; i < numIods; i++)
		{
			instr = &(instrPool[i]);
			instr->bytesDone = 0;
			instr->totalTime.tv_sec = 0;
			instr->totalTime.tv_usec = 0;
			instr->srvrTime.tv_sec = 0;
			instr->srvrTime.tv_usec = 0;
			instr->sha1Time.tv_sec = 0;
			instr->sha1Time.tv_usec = 0;
			instr->totalTime_ts.tv_sec = 0;
			instr->totalTime_ts.tv_usec = 0;
			instr->srvrTime_ts.tv_sec = 0;
			instr->srvrTime_ts.tv_usec = 0;
			instr->sha1Time_ts.tv_sec = 0;
			instr->sha1Time_ts.tv_usec = 0;
		}
	}
}

static inline void update_byteCount_instrumentation(struct instrumentation_s* instr, unsigned long long byteCount)
{
	if (doInstrumentation) {
		instr->bytesDone += byteCount;
	}
}

static inline void start_TotalTime_instrumentation(struct instrumentation_s* instr)
{
	if (doInstrumentation) {
		gettimeofday(&(instr->totalTime_ts), NULL);
	}
}

static inline void start_srvrTime_instrumentation(struct instrumentation_s* instr)
{
	if (doInstrumentation) {
		gettimeofday(&(instr->srvrTime_ts), NULL);
	}
}

static inline void start_sha1Time_instrumentation(struct instrumentation_s* instr)
{
	if (doInstrumentation) {
		gettimeofday(&(instr->sha1Time_ts), NULL);
	}
}

static inline void subtract_timevals(struct timeval *old)
{
	struct timeval tv;
	long int underflow;

	gettimeofday(&tv, NULL);
	old->tv_sec = tv.tv_sec - old->tv_sec;
	underflow =  tv.tv_usec - old->tv_usec;
	if (underflow < 0)
	{
		old->tv_sec = old->tv_sec - 1;
		underflow += 1000000;
	}
	old->tv_usec = underflow;
	return;
}

static inline void add_timevals(struct timeval *old, struct timeval *number)
{
	struct timeval tv = *number;
	long int overflow;

	old->tv_sec = tv.tv_sec + old->tv_sec;
	overflow =  tv.tv_usec + old->tv_usec;
	if (overflow > 1000000)
	{
		old->tv_sec = old->tv_sec + (overflow/1000000);
		overflow = (overflow%1000000);
	}
	old->tv_usec = overflow;
	number->tv_sec = 0;
	number->tv_usec = 0;
	return;
}

static inline void end_TotalTime_instrumentation(struct instrumentation_s* instr)
{
	if (doInstrumentation)
	{
		subtract_timevals(&(instr->totalTime_ts));
		add_timevals(&(instr->totalTime), &(instr->totalTime_ts));
	}
}

static inline void end_srvrTime_instrumentation(struct instrumentation_s* instr)
{
	if (doInstrumentation)
	{
		subtract_timevals(&(instr->srvrTime_ts));
		add_timevals(&(instr->srvrTime), &(instr->srvrTime_ts));
	}
}

static inline void end_sha1Time_instrumentation(struct instrumentation_s* instr)
{
	if (doInstrumentation)
	{
		subtract_timevals(&(instr->sha1Time_ts));
		add_timevals(&(instr->sha1Time), &(instr->sha1Time_ts));
	}
}

static inline struct instrumentation_s* 
	getCriticalPath_instrumentation(struct instrumentation_s* criticalPath)
{
	struct instrumentation_s *instr;
	struct instrumentation_s* array;
	int count;
	int i, max = -1;
	struct timeval maxTime;

	if (!doInstrumentation) {
		return NULL;
	}

	array = instrPool;
	count = numIods;
	maxTime.tv_sec = 0;
	maxTime.tv_usec = 0;

	for(i = 0;i < count;i++)
	{
		if (array[i].used == UNUSED) continue;
		if (array[i].totalTime.tv_sec < maxTime.tv_sec)
			continue;
		if (array[i].totalTime.tv_sec > maxTime.tv_sec)
		{
			memcpy(&maxTime, &(array[i].totalTime), sizeof(struct timeval));
			max = i;
			continue;
		}
		
		/* (array[i].totalTime.tv_sec == maxTime.tv_sec) */
		if (array[i].totalTime.tv_usec > maxTime.tv_usec)
		{
			memcpy(&maxTime, &(array[i].totalTime), sizeof(struct timeval));
			max = i;
		}
	}
	{
		instr = &(array[max]);
		criticalPath->bytesDone    += instr->bytesDone;
		add_timevals(&(criticalPath->totalTime), &(instr->totalTime));
		add_timevals(&(criticalPath->srvrTime), &(instr->srvrTime));
		add_timevals(&(criticalPath->sha1Time), &(instr->sha1Time));
	}
	return (&(array[max]));
}

static void key_create(void)
{
	pthread_key_create(&key, NULL);
	if (doInstrumentation)
	{
		instrPool = (struct instrumentation_s *) calloc(numIods, sizeof(struct instrumentation_s));
		if (instrPool == NULL) {
			PERROR(SUBSYS_LIBCAS, "Could not allocate memory\n");
			exit(1);
		}
		initInstr();
	}
	return;
}

static int pick_index(void)
{
	int i;

	if (!doInstrumentation) {
		return -1;
	}
	pthread_mutex_lock(&instrPoolMutex);
	for (i = 0; i < numIods; i++) {
		if (instrPool[i].used == UNUSED) {
			instrPool[i].used = USED;
			break;
		}
	}
	pthread_mutex_unlock(&instrPoolMutex);
	return (i == numIods) ? -1 : i;
}

static void cas_return_dtor(struct cas_return *tmpdata)
{
	free(tmpdata->buf);
	tmpdata->buf = NULL;
	tmpdata->count = 0;
	return;
}

static int cas_return_ctor(struct cas_return *tmpdata, struct cas_return *data,
		int startHashes, int countHashes)
{
	int i;

	tmpdata->count = countHashes;
	tmpdata->buf   = (struct dataArray *) calloc(countHashes, sizeof(struct dataArray));
	if (tmpdata->buf == NULL) {
		return -ENOMEM;
	}
	for (i = 0; i < countHashes; i++) {
		tmpdata->buf[i].start = data->buf[startHashes + i].start;
		tmpdata->buf[i].byteCount = data->buf[startHashes + i].byteCount;
		LOG(stderr, DEBUG_MSG, SUBSYS_LIBCAS, "%d -> (%p, %d)\n", i, tmpdata->buf[i].start, tmpdata->buf[i].byteCount);
	}
	return 0;
}

static void* clnt_get_thread(void* input)
{
	struct instrumentation_s *myInstr = NULL;
	sem_t* myCountingSem = NULL;
	struct thread_input *t_input = NULL;
	struct cas_return *job = NULL, curr_job;
	int bytes_read = 0, currentRequest = 0, startHashes = 0, remainingHashes = 0,
		*returnValue = NULL, to_free = 0;
	unsigned char *hashBlock = NULL;
	struct sockaddr *serverAddress = NULL;
	unsigned long long bytesDone = 0;

	/* Do this only if instrumentation is enabled */
	if (doInstrumentation) {
		if ((myInstr = pthread_getspecific(key)) == NULL) 
		{
			int ret, slot = pick_index();
			myInstr = &instrPool[slot];
			if ((ret = pthread_setspecific(key, myInstr)) != 0) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_LIBCAS,  "setspecific failed: %s\n", strerror(ret));
			}
		}
		start_TotalTime_instrumentation(myInstr);
	}

	t_input = (struct thread_input*) input;
	job = t_input->data;
	remainingHashes = job->count;
	myCountingSem = t_input->countingSem;
	hashBlock = t_input->hashBlock;
	serverAddress = t_input->serverAddress;
	returnValue = t_input->returnValue;
	
	if (remainingHashes == 0) {
		*returnValue = 0;
		sem_post(myCountingSem);
		return NULL;
	}
	startHashes = 0;
	while (remainingHashes > 0)
	{
		if (remainingHashes > CAPFS_MAXHASHES)
		{
			currentRequest = CAPFS_MAXHASHES;
			remainingHashes -= CAPFS_MAXHASHES;
		}
		else
		{
			currentRequest = remainingHashes;
			remainingHashes = 0;
		}
		/* Don't allocate if we can finish it off in one-shot */
		if (remainingHashes == 0 && startHashes == 0)
		{
			to_free = 0;
			curr_job = *job;
		}
		/* Allocate a temporary structure if we are splitting */
		else 
		{
			if (cas_return_ctor(&curr_job, job, startHashes, currentRequest) < 0) {
				*returnValue = -ENOMEM;
				sem_post(myCountingSem);
				return NULL;
			}
			to_free = 1;
		}
		if (doInstrumentation) {
			start_srvrTime_instrumentation(myInstr);
		}
		/* Make an RPC call to the CAS servers */
		if ((bytes_read = cas_get(use_sockets, t_input->tcp, (struct sockaddr_in *) serverAddress, 
						hashBlock, &curr_job)) < 0) {
			*returnValue = -errno;
			if (to_free == 1) {
				cas_return_dtor(&curr_job);
			}
			sem_post(myCountingSem);
			return NULL;
		}
		/* Now we know the time it took on the server. add it up */
		job->server_time += curr_job.server_time;
		LOG(stderr, DEBUG_MSG, SUBSYS_LIBCAS, "[Get] current job service time %Ld"
				" overall service time %Ld\n", curr_job.server_time, job->server_time);
		if (doInstrumentation) {
			end_srvrTime_instrumentation(myInstr);
		}

		if (to_free == 1) {
			cas_return_dtor(&curr_job);
		}
		bytesDone += bytes_read;
		startHashes += currentRequest;
		hashBlock += (CAPFS_MAXHASHLENGTH * currentRequest);
	}
	if (doInstrumentation) {
		update_byteCount_instrumentation(myInstr, bytesDone);
		end_TotalTime_instrumentation(myInstr);
	}
	*returnValue = job->count;
	sem_post(myCountingSem);
	return NULL;
}

static void* clnt_put_thread(void* input)
{
	unsigned long long bytesDone = 0;
	unsigned char *hashBlock = NULL;
	struct cas_return *job = NULL, curr_job;
	struct sockaddr *serverAddress = NULL;
	int bytes_written = 0, *returnValue = NULL, startHashes = 0, 
		remainingHashes = 0, currentRequest = 0, to_free = 0;
	struct thread_input *t_input = NULL;
	sem_t* myCountingSem = NULL;
	struct instrumentation_s *myInstr = NULL;

	/* Start instrumentation if requested */
	if (doInstrumentation) {
		if ((myInstr = pthread_getspecific(key)) == NULL) 
		{
			int ret, slot = pick_index();
			myInstr = &instrPool[slot];
			if ((ret = pthread_setspecific(key, myInstr)) != 0) {
				LOG(stderr, CRITICAL_MSG, SUBSYS_LIBCAS,  "setspecific failed: %s\n", strerror(ret));
			}
		}
		start_TotalTime_instrumentation(myInstr);
	}

	t_input = (struct thread_input *) input;
	myCountingSem = t_input->countingSem;
	serverAddress = t_input->serverAddress;
	returnValue = t_input->returnValue;
	hashBlock = t_input->hashBlock;
	job = t_input->data;
	remainingHashes = job->count;

	if (remainingHashes == 0) {
		*returnValue = 0;
		sem_post(myCountingSem);
		return NULL;
	}

	startHashes = 0;
	while (remainingHashes > 0)
	{
		if (remainingHashes > CAPFS_MAXHASHES)
		{
			currentRequest = CAPFS_MAXHASHES;
			remainingHashes -= CAPFS_MAXHASHES;
		}
		else
		{
			currentRequest = remainingHashes;
			remainingHashes = 0;
		}
		/* Don't allocate a new job structure if you can do it off in one-shot */
		if (startHashes == 0 && remainingHashes == 0) {
			to_free = 0;
			curr_job = *job;
		}
		/* allocate a temporary structure */
		else {
			if (cas_return_ctor(&curr_job, job, startHashes, currentRequest) < 0) {
				*returnValue = -ENOMEM;
				sem_post(myCountingSem);
				return NULL;
			}
			to_free = 1;
		}
		if (doInstrumentation) {
			start_srvrTime_instrumentation(myInstr);
		}
		/* Make an RPC call to the CAS servers */
		if ((bytes_written = cas_put(use_sockets, t_input->tcp, (struct sockaddr_in *) serverAddress, 
						hashBlock, &curr_job)) < 0) 
		{
			LOG(stderr, DEBUG_MSG, SUBSYS_LIBCAS, "cas_put crapped out %d\n", -errno);
			*returnValue = -errno;
			if (to_free == 1) {
				cas_return_dtor(&curr_job);
			}
			sem_post(myCountingSem);
			return NULL;
		}
		/* Now we know the time it took on the server. add it up */
		job->server_time += curr_job.server_time;
		LOG(stderr, DEBUG_MSG, SUBSYS_LIBCAS, "[Put] current job service time %Ld "
				"overall service time %Ld\n", curr_job.server_time, job->server_time);
		if (doInstrumentation) {
			end_srvrTime_instrumentation(myInstr);
		}
		if (to_free == 1) {
			cas_return_dtor(&curr_job);
		}
		bytesDone += bytes_written;
		startHashes += currentRequest;
		hashBlock += (CAPFS_MAXHASHLENGTH * currentRequest);
	}
	if (doInstrumentation) {
		update_byteCount_instrumentation(myInstr, bytesDone);
		end_TotalTime_instrumentation(myInstr);
	}
	*returnValue = job->count;
	sem_post(myCountingSem);
	return NULL;
}

void freeJobs(struct cas_iod_worker_data *jobs, int nIods)
{
	struct cas_return* data;
	int i;
	
	for (i = 0;i < nIods;i++)
	{
		free(jobs[i].returnValue);
		free(jobs[i].hashes);
		data = jobs[i].data;
		free(data->buf);
		free(data);
	}
	free(jobs);
}

struct cas_iod_worker_data* convert_to_jobs(struct dataArray* da, int nChunks, struct iod_map* map,
		fdesc* desc, unsigned char* hash, int *iodCount )
{
	struct cas_iod_worker_data* jobs;
	int i, buffersAdded, j, counts[CAPFS_MAXIODS], iodsUsed = 0, unique[CAPFS_MAXIODS], *myMapping, *reverseMap, global_iods[CAPFS_MAXIODS];
	unsigned char *ptr; 
	struct cas_return* data;

	memset(counts, 0, CAPFS_MAXIODS*sizeof(int));
	for(i = 0;i < nChunks;i++)
	{
		if (counts[map[i].normalized_iod]==0)
		{
			unique[iodsUsed] = map[i].normalized_iod;
			global_iods[iodsUsed] = map[i].global_iod;
			iodsUsed++;
		}
		counts[map[i].normalized_iod] = counts[map[i].normalized_iod] + 1;
	}
	reverseMap = unique;
	
	jobs = (struct cas_iod_worker_data*)calloc(1, iodsUsed*sizeof(struct cas_iod_worker_data));
	for(i = 0;i < iodsUsed;i++)
	{
			jobs[i].iodNumber = global_iods[i];
			jobs[i].iodAddress = (struct sockaddr*)&(desc->fd.iod[reverseMap[i]].addr);
			//sockio_dump_sockaddr((struct sockaddr_in *)jobs[i].iodAddress, stderr);
			jobs[i].returnValue = (int*)calloc(1, sizeof(int));
			jobs[i].data = (struct cas_return*)calloc(1, sizeof(struct cas_return));
			(jobs[i].data)->server_time = 0;
			(jobs[i].data)->count = counts[reverseMap[i]];
			(jobs[i].data)->buf = (struct dataArray*) calloc(1, sizeof(struct dataArray)*counts[reverseMap[i]]);
			jobs[i].hashes = (unsigned char*) calloc(1, counts[reverseMap[i]]*CAPFS_MAXHASHLENGTH);
			*(jobs[i].returnValue) = 0;
	}
	memset(counts, 0, CAPFS_MAXIODS * sizeof(int));
	for(i = 0;i < iodsUsed;i++)
		counts[reverseMap[i]] = i;
	myMapping = counts;

	for(i = 0;i < nChunks;i++)
	{
		j = myMapping[map[i].normalized_iod];
		buffersAdded = *(jobs[j].returnValue);
		ptr = jobs[j].hashes + CAPFS_MAXHASHLENGTH * buffersAdded;
		memcpy(ptr, hash + CAPFS_MAXHASHLENGTH * i, CAPFS_MAXHASHLENGTH);
		data = jobs[j].data;
		data->buf[buffersAdded].start = da[i].start;
		data->buf[buffersAdded].byteCount = da[i].byteCount;
		*(jobs[j].returnValue) = buffersAdded + 1;
	}
	for(i = 0;i < iodsUsed;i++) {
		/*
		 * FIXME: Partho can you tell me if this array index is j or i?
		 * assert(*(jobs[j].returnValue) == (jobs[j].data)->count);
		 */
		if (*(jobs[i].returnValue) != (jobs[i].data)->count) {
			LOG(stderr, CRITICAL_MSG, SUBSYS_LIBCAS,  "FATAL error? return value for jobs[%d] = %d, should have been %d\n",
					i, *(jobs[i].returnValue), (jobs[i].data)->count);
		}
	}
	*iodCount = iodsUsed;
	return jobs;
}

void clnt_get(int tcp, struct cas_iod_worker_data *iod_jobs, int count)
{
	sem_t mySem;
	int i, err;
	struct thread_input *tInput;

	sem_init(&mySem, 0, 0);
	tInput = (struct thread_input*) calloc(count, sizeof(struct thread_input));
	if (tInput == NULL) {
		for (i = 0; i < count; i++) {
			*(iod_jobs[i].returnValue) = -ENOMEM;
		}
		return;
	}
	for(i = 0;i < count;i++)
	{
		tInput[i].tcp = tcp;
		tInput[i].hashBlock = iod_jobs[i].hashes;
		tInput[i].serverAddress = iod_jobs[i].iodAddress;
		tInput[i].data = iod_jobs[i].data;
		tInput[i].returnValue = iod_jobs[i].returnValue;
		tInput[i].countingSem = &mySem;
		tp_assign_work_by_id(poolID, clnt_get_thread, (void *)(&(tInput[i])));
	}

	for(i = 0;i < count;i++)
	{
		do {err = sem_wait(&mySem);} while (err == EINTR);
		if (iod_jobs[i].iodNumber >= 0 && iod_jobs[i].iodNumber < CAPFS_STATS_MAX)
		{
			server_get_time[iod_jobs[i].iodNumber] += (iod_jobs[i].data)->server_time;
			LOG(stderr, DEBUG_MSG, SUBSYS_LIBCAS, "server_get_time[%d] = %Ld\n", iod_jobs[i].iodNumber,
					server_get_time[iod_jobs[i].iodNumber]);
		}
	}
	free(tInput);
	sem_destroy(&mySem);

	if (doInstrumentation)
		getCriticalPath_instrumentation(&get_criticalPathTime);
}

void clnt_put(int tcp, struct cas_iod_worker_data *iod_jobs, int count)
{
	sem_t mySem;
	int i, err;
	struct thread_input *tInput;

	sem_init(&mySem, 0, 0);
	tInput = (struct thread_input*) calloc(count, sizeof(struct thread_input));
	if (tInput == NULL) {
		for (i = 0; i < count; i++) {
			*(iod_jobs[i].returnValue) = -ENOMEM;
		}
		return;
	}
	for(i = 0;i < count;i++)
	{
		tInput[i].tcp = tcp;
		tInput[i].hashBlock = iod_jobs[i].hashes;
		tInput[i].serverAddress = iod_jobs[i].iodAddress;
		tInput[i].data = iod_jobs[i].data;
		tInput[i].returnValue = iod_jobs[i].returnValue;
		tInput[i].countingSem = &mySem;

		tp_assign_work_by_id(poolID, clnt_put_thread, (void *)(&(tInput[i])));
	}

	for(i = 0;i < count;i++)
	{
		do {err = sem_wait(&mySem);} while (err == EINTR);
		if (iod_jobs[i].iodNumber >= 0 && iod_jobs[i].iodNumber < CAPFS_STATS_MAX)
		{
			server_put_time[iod_jobs[i].iodNumber] += (iod_jobs[i].data)->server_time;
			LOG(stderr, DEBUG_MSG, SUBSYS_LIBCAS, "server_put_time[%d] = %Ld\n", iod_jobs[i].iodNumber,
					server_put_time[iod_jobs[i].iodNumber]);
		}
	}
	free(tInput);
	sem_destroy(&mySem);

	if (doInstrumentation)
		getCriticalPath_instrumentation(&put_criticalPathTime);
}

/* returns 0 if failure, 1 if server alive and listening */
int clnt_ping(int tcp, struct sockaddr* serverAddress)
{
	if (cas_ping(use_sockets, tcp, (struct sockaddr_in *) serverAddress) < 0) {
		return 0;
	}
	return 1;
}

/* 
 * returns 0 on success or appropriate errno otherwise as return value
 * fills in the sfs structure given to it
 */
int clnt_statfs_req(int tcp, struct sockaddr* serverAddress, struct statfs *sfs)
{
	if (cas_statfs(use_sockets, tcp, (struct sockaddr_in *) serverAddress, sfs) < 0) {
		return -1;
	}
	return 0;
}

/*
 * Use this routine sparingly, and only if you know what you are doing.
 * Cleans up the entire data directories on IODs
 */
int clnt_removeall(int tcp, struct sockaddr *serverAddress, char *dirname)
{
	if (cas_removeall(use_sockets, tcp, (struct sockaddr_in *) serverAddress, dirname) < 0) {
		return -1;
	}
	return 0;
}

/*
 * Sets the ball rolling
 */
int clnt_init(struct cas_options *options, int num_Iods, int blkSize)
{
	pthread_spin_init(&seq_lock, 0);
	doInstrumentation = 0;
	if (options)
	{
		if (options->doInstrumentation == 0)
			doInstrumentation = 0;
		use_sockets = options->use_sockets;
	}
	iod_blk_size = blkSize;
	numIods = num_Iods;
	poolInfo.tpi_name = NULL;
	poolInfo.tpi_stack_size = -1;
	poolInfo.tpi_count = numIods;
	/* Create a key and also initialize key data structures */
	pthread_once(&once, key_create);
	/* 
	 * Fire up the thread pool with the specified number
	 * of threads.
	 */
	poolID = tp_init(&poolInfo);
	if (poolID < 0) {
		LOG(stderr, CRITICAL_MSG, SUBSYS_LIBCAS, "Could not fire up thread pool\n");
		return -1;
	}
	sha1_init();
	return 0;
}


/*
 * Cleanly shutsdown the cas components
 */
void clnt_finalize(void)
{
	sha1_finalize();
	/*
	 * Kill the thread pool 
	 */
	tp_cleanup_by_id(poolID);
	poolID = -1;

	if (0) {
		LOG(stdout, DEBUG_MSG, SUBSYS_LIBCAS, "Bytes Put:%llu\n", put_criticalPathTime.bytesDone);
		LOG(stdout, DEBUG_MSG, SUBSYS_LIBCAS, "Put Total time:%ld(sec) %ld(usec)\n",
				put_criticalPathTime.totalTime.tv_sec,
				put_criticalPathTime.totalTime.tv_usec);
		LOG(stdout, DEBUG_MSG, SUBSYS_LIBCAS, "Put Total Server Time:%ld(sec) %ld(usec)\n",
				put_criticalPathTime.srvrTime.tv_sec,
				put_criticalPathTime.srvrTime.tv_usec);
		LOG(stdout, DEBUG_MSG, SUBSYS_LIBCAS, "Put Total SHA1 time:%ld(sec) %ld(usec)\n",
				put_criticalPathTime.sha1Time.tv_sec,
				put_criticalPathTime.sha1Time.tv_usec);
		LOG(stdout, DEBUG_MSG, SUBSYS_LIBCAS, "Bytes Get:%llu\n", get_criticalPathTime.bytesDone);
		LOG(stdout, DEBUG_MSG, SUBSYS_LIBCAS, "Get Total time:%ld(sec) %ld(usec)\n",
				get_criticalPathTime.totalTime.tv_sec,
				get_criticalPathTime.totalTime.tv_usec);
		LOG(stdout, DEBUG_MSG, SUBSYS_LIBCAS, "Get Total Server Time:%ld(sec) %ld(usec)\n",
				get_criticalPathTime.srvrTime.tv_sec,
				get_criticalPathTime.srvrTime.tv_usec);
		LOG(stdout, DEBUG_MSG, SUBSYS_LIBCAS, "Get Total SHA1 time:%ld(sec) %ld(usec)\n",
				get_criticalPathTime.sha1Time.tv_sec,
				get_criticalPathTime.sha1Time.tv_usec);
		free(instrPool);
	}
	return;
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

