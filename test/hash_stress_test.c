/*
 * hcache stress tester for correctness
 * Murali Vilayannur (C) 2005
 * vilayann@cse.psu.edu
 */
#include <sha.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include "capfs_config.h"
#include "hcache.h"
#include <string.h>

#define CHUNK_SIZE 	4096
#define HCACHE_COUNT 4096

typedef struct {
	char *fname;
	char *hname;
	int real_fd;
	int hash_fd;
} file;

struct recipe {
	int count;
	unsigned char **hashes;
	size_t  *hash_lengths;
};

struct recipe* get_recipe_list(char *filename, int chunk_size, int *error)
{
	struct stat statbuf;
	struct recipe *recipe = NULL;
	int i, fd;
	void *file_addr;
	size_t size = 0;

	if (stat(filename, &statbuf) < 0) {
		*error = errno;
		return NULL;
	}
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		*error = errno;
		return NULL;
	}
	if ((file_addr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		close(fd);
		*error = ENOMEM;
		return NULL;
	}
	recipe = (struct recipe *) calloc(1, sizeof(struct recipe));
	if (recipe == NULL) {
		munmap(file_addr, statbuf.st_size);
		close(fd);
		*error = ENOMEM;
		return NULL;
	}
	recipe->count = statbuf.st_size / chunk_size;
	if (statbuf.st_size % chunk_size != 0) {
		recipe->count++;
	}

	recipe->count = statbuf.st_size / chunk_size + 1;
	recipe->hashes = (unsigned char **) calloc(recipe->count, sizeof(unsigned char *));
	if (recipe->hashes == NULL) {
		free(recipe);
		munmap(file_addr, statbuf.st_size);
		close(fd);
		*error = ENOMEM;
		return NULL;
	}
	recipe->hash_lengths = (size_t *) calloc (recipe->count, sizeof(size_t));
	if (recipe->hash_lengths == NULL) {
		free(recipe->hashes);
		free(recipe);
		munmap(file_addr, statbuf.st_size);
		close(fd);
		*error = ENOMEM;
		return NULL;
	}
	*error = 0;
	for (i = 0; i < recipe->count; i++) {
		int ret;
		size_t input_length = 0;

		if (size + chunk_size <= statbuf.st_size) {
			input_length = chunk_size;
		}
		else {
			input_length = statbuf.st_size - size;
		}
		if ((ret = sha1((char *)file_addr + i * chunk_size, input_length,
						&recipe->hashes[i], &recipe->hash_lengths[i])) < 0) {
			*error = -ret;
			break;
		}
		size += input_length;
	}
	if (*error < 0) {
		free(recipe->hash_lengths);
		free(recipe->hashes);
		free(recipe);
		munmap(file_addr, statbuf.st_size);
		close(fd);
		return NULL;
	}
	munmap(file_addr, statbuf.st_size);
	close(fd);
	return recipe;
}

static void doprinthash(FILE *fp, unsigned char *hash, int hash_length)
{
	char str[256];
	FILE *newfp = stdout;

	hash2str(hash, hash_length, str);
	if (fp) {
		newfp = fp;
	}
	fprintf(newfp, "%s\n", str);
	return;
}

void my_open(char *name, file *filp)
{
	filp->real_fd = open(name, O_CREAT | O_RDWR | O_TRUNC, 0755);
	if (filp->real_fd > 0) {
		char *hash_name = (char *) malloc(strlen(name) + 10);
		sprintf(hash_name, "%s.hashes", name);
		filp->hash_fd = open(hash_name, O_RDWR | O_CREAT | O_TRUNC, 0755);
		filp->fname = (char *) strdup(name);
		filp->hname = hash_name;
	}
	return;
}

#ifndef min
#define min(a, b) (a) < (b) ? (a) : (b)
#endif
#ifndef max
#define max(a, b) (a) > (b) ? (a) : (b)
#endif

int my_close(file *filp)
{
	int i, error, nhashes, maxhashes;
	struct recipe *recipe = get_recipe_list(filp->fname, CHUNK_SIZE, &error);
	void *ptr;
	char *hashes= NULL;
	struct stat statbuf;
	FILE *fp = NULL;

	close(filp->real_fd);

	fp = (FILE *) fopen("/tmp/orig", "w+");
	for (i = 0; i < recipe->count; i++) {
		doprinthash(fp, recipe->hashes[i], recipe->hash_lengths[i]);
	}
	fclose(fp);
	fstat(filp->hash_fd, &statbuf);
	if ((ptr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, filp->hash_fd, 0)) == MAP_FAILED) {
		printf("error in hash file mmap\n");
	}
	else {
		fp = (FILE *) fopen("/tmp/orig.hash", "w+");
		for (i = 0; i < statbuf.st_size / CAPFS_MAXHASHLENGTH; i++) {
			doprinthash(fp, ptr + i * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH);
		}
		fclose(fp);
	}
	fp = (FILE *) fopen("/tmp/orig.hcache", "w+");
	maxhashes = statbuf.st_size / CAPFS_MAXHASHLENGTH;
	hashes = (char *) calloc(CAPFS_MAXHASHLENGTH, maxhashes);
	nhashes = 0;
	while (nhashes < maxhashes)
	{
		int j;
		int64_t ret, rem = min(max(1, HCACHE_COUNT/2), maxhashes - nhashes);
		ret = hcache_get(filp->hname, nhashes, rem, -1, hashes);
		if ((ret / CAPFS_MAXHASHLENGTH) != rem)
		{
			printf("hcache_get asked for %Ld, obtained %Ld\n", rem, ret / CAPFS_MAXHASHLENGTH);
			assert((ret/CAPFS_MAXHASHLENGTH) == rem);
		}
		nhashes += (ret / CAPFS_MAXHASHLENGTH);
		for (j = 0; j < (ret / CAPFS_MAXHASHLENGTH); j++)
		{
			doprinthash(fp, hashes + j * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH);
		}
	}
	fclose(fp);

	hcache_clear(filp->hname);
	fp = (FILE *) fopen("/tmp/orig.hcache2", "w+");
	nhashes = 0;
	while (nhashes < maxhashes)
	{
		int j;
		int64_t ret, rem = min(max(1, HCACHE_COUNT/2), maxhashes - nhashes);
		ret = hcache_get(filp->hname, nhashes, rem, -1, hashes);
		if ((ret / CAPFS_MAXHASHLENGTH) != rem)
		{
			printf("hcache_get asked for %Ld, obtained %Ld\n", rem, ret / CAPFS_MAXHASHLENGTH);
			assert((ret/CAPFS_MAXHASHLENGTH) == rem);
		}
		nhashes += (ret / CAPFS_MAXHASHLENGTH);
		for (j = 0; j < (ret / CAPFS_MAXHASHLENGTH); j++)
		{
			doprinthash(fp, hashes + j * CAPFS_MAXHASHLENGTH, CAPFS_MAXHASHLENGTH);
		}
	}
	fclose(fp);
	return close(filp->hash_fd);
}

ssize_t my_read(file* filp, void *buf, size_t size)
{
	int64_t begin_chunk, end_chunk, nchunks;
	unsigned char *phashes = NULL;
	off_t off = lseek(filp->real_fd, 0, SEEK_CUR);
	int nhashes = 0;

	begin_chunk = off / CHUNK_SIZE;
	end_chunk = (off + size - 1) / CHUNK_SIZE;
	nchunks = (end_chunk - begin_chunk + 1);
	phashes = (unsigned char *) calloc(CAPFS_MAXHASHLENGTH, nchunks);
	nhashes = hcache_get(filp->hname, begin_chunk, nchunks, -1, phashes);

	printf("read off : %lu, size : %u, nhashes : %d\n", off, size, nhashes);
	return read(filp->real_fd, buf, size);
}

ssize_t my_write(file *filp, const void *buf, size_t size)
{
	off_t off; 
	int64_t begin_chunk = 0, end_chunk = 0, nchunks = 0, i = 0;
	unsigned char **phashes = NULL;
	int64_t issue_read[2] = {0, 0};
	int     nissues = 0, err = 0, part1 = 0, part2 = 0;
	int64_t save_offset, j;
	char *ptr = (char *) buf;

	save_offset = off = lseek(filp->real_fd, 0, SEEK_CUR);
	begin_chunk = off / CHUNK_SIZE;
	end_chunk = (off + size - 1) / CHUNK_SIZE;
	nchunks = (end_chunk - begin_chunk + 1);
	phashes = (unsigned char **) calloc(nchunks, sizeof(unsigned char *));
	if ((part1 = (off % CHUNK_SIZE)) != 0) {
		issue_read[nissues++] = begin_chunk;
	}
	if ((part2 = ((off + size) % CHUNK_SIZE)) != 0) {
		if (nissues == 0 || begin_chunk != end_chunk) {
			issue_read[nissues++] = end_chunk;
		}
	}
	/* let us be optimistic here */
	if (nissues == 0) {
		for (i = 0; i < nchunks; i++) {
			size_t len;

			if ((err = sha1(ptr + i * CHUNK_SIZE, 
							CHUNK_SIZE, &phashes[i], &len)) < 0) {
				break;
			}
			/* DEBUG */
			hcache_put(filp->hname, begin_chunk + i, 1, phashes[i]);
		}
	}
	else {
		char *pfile[2] = {NULL, NULL}, *overall = NULL;
		int comp[2] = {0, 0};
		long total_length = 0;
		size_t size_thus_far = 0;

		overall = (char *) calloc(nchunks, CHUNK_SIZE);
		/* We need to issue reads here for atmost 2 of the chunks here */
		for (j = 0; j < nissues; j++) {
			off_t newoff;

			newoff = (CHUNK_SIZE * issue_read[j]);
			pfile[j] = (char *) calloc(CHUNK_SIZE, sizeof(char));
			comp[j] = pread(filp->real_fd, pfile[j], CHUNK_SIZE, newoff);
			if (comp[j] > 0) {
				total_length = (comp[j] + newoff);
				/* Copy the read in data */
				memcpy(overall + (issue_read[j] - begin_chunk) * CHUNK_SIZE, pfile[j], comp[j]);
			}
		}
		if (pfile[0]) {
			free(pfile[0]);
		}
		if (pfile[1]) {
			free(pfile[1]);
		}
		if (total_length < (off + size)) {
			total_length = (off + size);
		}
		/* copy the new data to be written */
		memcpy(overall + part1, ptr, size);
		size_thus_far = (begin_chunk * CHUNK_SIZE);
		for (i = 0; i < nchunks; i++) {
			size_t len;

			if (size_thus_far + CHUNK_SIZE < total_length) {
				if ((err = sha1(overall + i * CHUNK_SIZE, 
								CHUNK_SIZE, &phashes[i], &len)) < 0) {
					break;
				}
			}
			else {
				assert(total_length - size_thus_far > 0);
				if ((err = sha1(overall + i * CHUNK_SIZE, 
								(total_length - size_thus_far), &phashes[i], &len)) < 0) {
					break;
				}
			}

			/* DEBUG */
			hcache_put(filp->hname, begin_chunk + i, 1, phashes[i]);
			size_thus_far += CHUNK_SIZE;
		}
		free(overall);
	}
	/* write the hashes out */
	for (i = 0; i < nchunks; i++) {
		pwrite(filp->hash_fd, phashes[i], CAPFS_MAXHASHLENGTH, (begin_chunk + i) * CAPFS_MAXHASHLENGTH);
	}
	/* write the real data out */
	return write(filp->real_fd, buf, size);
}

static void do_copy_some_stuff(file *filp, char *srcfile)
{
	int fd;
	struct stat statbuf;
	char *buf = NULL;
	long off;
	
	fd = open(srcfile, O_RDONLY);
	if (fd < 0)
	{
		perror("open:");
		exit(1);
	}
	if (fstat(fd, &statbuf) < 0 && statbuf.st_size > 0)
	{
		perror("fstat:");
		exit(1);
	}
	buf = (char *) calloc(statbuf.st_size, 1);
	read(fd, buf, statbuf.st_size);
	my_write(filp, buf, statbuf.st_size);
	
	
	
	srand(time(NULL));
	strcpy(buf, "ljfsjklfjslkjf lllfjllfjlljfln kjf");
	off = rand() % statbuf.st_size;
	lseek(filp->real_fd, off, SEEK_SET);
	my_write(filp, buf, statbuf.st_size);
	
	
	close(fd);
	return;
}

struct user_ptr 
{
	cm_handle_t    p;
	int 				mode;
	int				nframes;
	char 			  **buffers;
	size_t		  *sizes;
	int64_t		  *offsets;
	int			  *completed;
};

/*
* NOTE that we dont free the ->completed integer pointer
* since that is returned to the cache manager
*/
static void dealloc_user_ptr(struct user_ptr *ptr)
{
	if (ptr)
	{
		free(ptr);
	}
}

static struct user_ptr* alloc_user_ptr(cm_handle_t p, int mode, int nframes,
		char **buffers, size_t *sizes, int64_t *offsets)
{
		struct user_ptr *ptr = NULL;

		ptr = (struct user_ptr *) calloc(1, sizeof(struct user_ptr));
		if (ptr)
		{
			ptr->p = p;
			ptr->mode = mode;
			ptr->nframes = nframes;
			ptr->buffers = buffers;
			ptr->sizes = sizes;
			ptr->offsets = offsets;
			/* We try to allocate this at the last, so 
			 * that if we had to call dealloc_user_ptr()
			 * we still would not have to free this!
			 */
			ptr->completed = (int *) 
				calloc(nframes, sizeof(int));
			if (!ptr->completed)
			{
				goto error_exit;
			}
		}
		return ptr;
error_exit:
		dealloc_user_ptr(ptr);
		return NULL;
}

static void
fetch_hashes(struct user_ptr *uptr)
{
	struct stat statbuf;
	int fd, i, start_chunk;
	char *filename = ((struct handle *)uptr->p)->name; /* This is a hashes file. so no computation */
	void *file_addr;
	size_t size = 0;

	if (stat(filename, &statbuf) < 0) {
		fprintf(stderr, "No such file: %s!\n", filename);
		for (i = 0; i < uptr->nframes; i++) {
			uptr->completed[i] = -errno;
		}
		return;
	}
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "%s could not be opened! %s\n", filename, strerror(errno));
		for (i = 0; i < uptr->nframes; i++) {
			uptr->completed[i] = -errno;
		}
		return;
	}
	if ((file_addr = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED) {
		for (i = 0; i < uptr->nframes; i++) {
			uptr->completed[i] = -errno;
		}
		close(fd);
		return;
	}
	start_chunk = (uptr->offsets[0] / uptr->sizes[0]);
	for (i = start_chunk; i < (start_chunk + uptr->nframes); i++) 
	{
		size_t input_length = 0;

		if (size + 20 <= statbuf.st_size) {
			input_length = 20;
		}
		else {
			if (size < statbuf.st_size) {
				input_length = statbuf.st_size - size;
			}
			else {
				uptr->completed[i - start_chunk] = 0;
				size += 20;
				continue;
			}
		}
		memcpy(uptr->buffers[i - start_chunk], file_addr + i * 20, input_length);
		uptr->completed[i - start_chunk] = input_length;
		size += 20;
	}
	munmap(file_addr, statbuf.st_size);
	close(fd);
	return;
}


static struct user_ptr* 
post_io(cm_handle_t p, int nframes, char **buffers,
		size_t *sizes, int64_t *offsets, int mode, int *error)
{
		struct user_ptr *uptr = NULL;

		/* try to allocate the user ptr to keep track of the state */
		uptr = alloc_user_ptr(p, mode, nframes, buffers, sizes, offsets);
		if (!uptr)
		{
			*error = -ENOMEM;
			return NULL;
		}
		return uptr;
}

/*
 * readpage_begin() routine must return an opaque handle
 * on success and negative error code on failure.
 * This routine is invoked only on a cache miss.
 * Use this to just setup information for computing hashes if need be
 */ 
static long hash_buffered_read_begin(cm_handle_t p, 
		int number, cm_buffer_t *buffers, size_t *sizes, int64_t *offsets)
{
		int ret;
		struct user_ptr *uptr = NULL;

		/* Actually the bulk of the work is done only at the time of the complete routine */
		if ((uptr = post_io(p, number, (char **) buffers,
						sizes, offsets, 0, &ret)) == NULL)
		{
			panic("hash_buffered_read: could not post read %d\n", ret);
			return ret;
		}
		return (long) uptr;
}

/*
 * Complete the I/O operation that was posted asynchronously
 * earlier. We return a pointer to an array of integers
 * that indicate the error codes in case of failed I/O
 * or amount of I/O completed.
 * Callers responsibility to free it.
 */
static int* hash_buffered_read_complete(long _uptr)
{
		struct user_ptr *uptr = NULL;
		int *completed;

		uptr = (struct user_ptr *) _uptr;
		completed = uptr->completed;
		/*
		 * Fetch the hashes for the file
		 * here. Note, at some point
		 * this would become an RPC call, 
		 * right now, I just fetch it locally
		 */
		fetch_hashes(uptr);
		/* Deallocate the user pointer */
		dealloc_user_ptr(uptr);
		return completed;
}

int main(int argc, char *argv[])
{
	file filp;
	int exit_status1, exit_status2, exit_status3;
	struct hcache_options opt;
	cmgr_stats_t  stats;
	char options[256];

	memset(&opt, 0, sizeof(opt));
	opt.organization = CAPFS_HCACHE_SIMPLE;
	opt.hr_begin = hash_buffered_read_begin;
	opt.hr_complete = hash_buffered_read_complete;

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <filename for testing>\n", argv[0]);
		exit(1);
	}
	//setenv("CMGR_DEBUG", "true", 1);
	snprintf(options, 256, "%d", HCACHE_COUNT); /* MODIFY VALUE HERE */
	setenv("CMGR_BCOUNT", options, 1);
	snprintf(options, 256, "%d", CHUNK_SIZE);
	setenv("CMGR_CHUNK_SIZE", options, 1);
	hcache_init(&opt);
	my_open("/tmp/test.c", &filp);
	do_copy_some_stuff(&filp, argv[1]);
	my_close(&filp);
	exit_status1 = system("diff -q /tmp/orig /tmp/orig.hash");
	printf("Contents of computed recipe and hcache dump on disk: %s\n", (exit_status1 < 0) ? strerror(errno) : (exit_status1 == 0) ? "SIMILIAR" : "DIFFER");
	exit_status2 = system("diff -q /tmp/orig /tmp/orig.hcache");
	printf("Contents of computed recipe and hcache : %s\n", (exit_status2 < 0) ? strerror(errno) : (exit_status2 == 0) ? "SIMILIAR" : "DIFFER");
	exit_status3 = system("diff -q /tmp/orig.hash /tmp/orig.hcache");
	printf("Contents of hcache dump on disk and hcache: %s\n", (exit_status3 < 0) ? strerror(errno) : (exit_status3 == 0) ? "SIMILIAR" : "DIFFER");
	if (exit_status1 == 0 && exit_status2 == 0 && exit_status3 == 0)
	{
		printf("Hcache stress test passed!\n");
	}
	else
	{
		printf("Hcache stress test failed!\n");
	}
	hcache_get_stats(&stats, 0);
	printf("Hcache hits: %Ld Hcache misses: %Ld Hcache evicts: %Ld Hcache flushes: %Ld\n",
			stats.hits, stats.misses, stats.evicts, stats.flushes);
	hcache_finalize();
	return 0;
}

/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 *
 * vim: ts=3
 */


