/* 
 * randread.c
 *
 * MPI program for spawning tasks to traverse directory trees.
 *
 * Example compile cmd:
 * mpicc -Wall mpi-randread.c -o mpi-randread
 *
 * Originally derived from single process program by Mike Westall and
 * Robert Geist of Clemson University; here are their notes:
 *
 * Syntax is:
 * 
 * mpi-randread <start_dir> <seed> <file_prob.> <dir_factor>
 * - file_prob is the probability that we will read a given file
 * - dir_factor controls how likely we are to go into subdirectories
 * - if one isn't supplied, they are both set to 1.0
 * 
 * define RANDREAD_DEBUG to enable some extra output as the program runs
 *
 * Enjoy!
 * -RG
 * p.s. the code is largely due to Mike Westall, so please give him
 * credit if you're writing up experiments on it ...
 */

/* mpi-randread.c */

/* This function reads a unix directory tree... It can be */
/* used as a base for a file finder or space computer     */
/* or general command sweeper.                            */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <mpi.h>

/* These constants control the workload generation */
static int     rseed;           /* Seed for generator              */
static float   fileprob;        /* Probability a file will be read */
static float   dirprob;         /* Probability a directory will be */
                                /* processed.                      */
static float   dirfactor;       /* Factor used to reduce dirprob   */
                                /* at each recursive call.         */


/* These are used for statistics gathering */
static int dirct = 0, filect = 0, statct = 0;
static long long bytect = 0;

static int tot_dirct = 0, tot_filect = 0, tot_statct = 0;
static long long tot_bytect = 0;

float unival(void);
void readfile(char *fname);
void statfile(char *fname, struct stat *sbuf, char *cwdbuf);
void procdir(DIR *dirp, char *cwdbuf);
void nextdir(char *dirname);

/* I/O buffer location and size */
static char *databuf;
static int datasz = 65536;

int main(int argc, char **argv)
{
    int mynod, nprocs, startsz;
    char startdir[512];
    double starttime, endtime;

    rseed     = 1;
    fileprob  = 1.0;
    dirprob   = 1.0;
    dirfactor = 1.0;

    MPI_Init(&argc,&argv);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &mynod);
	
    if (mynod == 0) {
	/* MPI doesn't guarantee that everyone gets cmd line parameters */
	if (argc < 2) {
	    fprintf(stderr, "error: no filename\n");
	    MPI_Abort(MPI_COMM_WORLD, 0);
	}
	else {
	    strncpy(startdir, argv[1], 511);
	    startsz = strlen(startdir) + 1;
	}
	if (argc > 2)
	    sscanf(argv[2], "%d", &rseed);
	if (argc > 3)
	    sscanf(argv[3], "%f", &fileprob);
	if (argc > 4)
	    sscanf(argv[4], "%f", &dirfactor);
	fprintf(stderr, "Random reader processing directory %s \n", argv[1]);
	fprintf(stderr, "Srand = %d, Fileprob = %1.4f, Dirfactor = %1.4f\n",
		rseed, fileprob, dirfactor);
    }

    /* distribute cmd line parameters in a naive way */
    MPI_Bcast(&rseed, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&fileprob, 1, MPI_FLOAT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&dirprob, 1, MPI_FLOAT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&dirfactor, 1, MPI_FLOAT, 0, MPI_COMM_WORLD);

    MPI_Bcast(&startsz, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(startdir, startsz, MPI_CHAR, 0, MPI_COMM_WORLD);

    /* seed RNG; don't use the same seed everywhere */
    srandom(rseed + mynod);

    /* grab a buffer for I/O */
    if ((databuf = malloc(datasz)) == NULL) {
	perror("malloc");
	return 1;
    }

    /* roughly synchronize */
    MPI_Barrier(MPI_COMM_WORLD);
    starttime = MPI_Wtime();

    /* start working */
    nextdir(startdir);

    if (mynod == 0) {
	printf("Waiting on others to finish...\n");
    }
    MPI_Barrier(MPI_COMM_WORLD);

    /* if i cared about speed i would use a derived type... */
    MPI_Reduce(&dirct, &tot_dirct, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&filect, &tot_filect, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&statct, &tot_statct, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&bytect, &tot_bytect, 1, MPI_LONG_LONG_INT, MPI_SUM, 0, 
	       MPI_COMM_WORLD);

    endtime = MPI_Wtime();

    printf("total directories read = %d, total files read = %d, total stat operations = %d, total bytes accessed = %Ld\n", tot_dirct, tot_filect, tot_statct, tot_bytect);

    MPI_Finalize();
    return 1;
}

/* unival()
 *
 * I think this returns a value between 0 and 1...
 */
float unival(void)
{
    float fval;
    fval = (float)random() / (float) RAND_MAX;
    return(fval);
}

/* readfile()
 *
 * Read a file in sequential hunks of datasz bytes
 *
 * fname - name of file to open and read
 */
void readfile(char *fname)
{
    int input;
    int blocks;
    int amt;
	 int input_flags=O_RDONLY;

    /* update file count */
    filect++;

#ifdef LARGE_FILE_SUPPORT
	 input_flags |= O_LARGEFILE;
#endif
    input = open(fname, input_flags);

    if (input >= 0) {
	blocks = 0;
	do {
	    amt = read(input, databuf, datasz);

	    /* update byte count */
	    if (amt > 0) {
		bytect += amt;
		blocks += 1;
	    }
	} while(amt > 0);
#ifdef RANDREAD_DEBUG
	printf("  %s %8d \n", fname, blocks);
#endif
	close(input);
    }
}

/* statfile()
 *
 * Recover the inode data for the current file or directory
 *
 * fname - name of the file to stat
 * sbuf - address of stat return buffer
 * cwdbuf - name of active directory
 */
void statfile(char *fname, struct stat *sbuf, char *cwdbuf)
{

    int rc;
    char namebuf[512]; /* Fully qualified name buffer. */

    strcpy(namebuf, cwdbuf);
    strcat(namebuf, "/");
    strcat(namebuf, fname);

    /* update stat count */
    statct++;

    rc = lstat(namebuf, sbuf);
}

/* procdir()
 *
 * Process all the files in the directory pointed to by DIRP
 *
 * dirp - open directory handle
 * cwdbuf - currently active directory
 */
void procdir(DIR *dirp, char *cwdbuf)
{
    struct dirent *dbuf;
    struct stat    sbuf;

    /* update directory count */
    dirct++;

    /* Read a file or directory name */
    dbuf = readdir(dirp);

    while (dbuf != 0) {
	/* Get the inode data */

	statfile(dbuf->d_name, &sbuf, cwdbuf);
#ifdef RANDREAD_DEBUG
	printf("%04x  %s \n", sbuf.st_mode, dbuf->d_name);
	fflush(stdout);
#endif

   	/* Recursively process subdirectories.. This is the easy  */
   	/* way to aviod recursively processing self or parent but */
   	/* it misses any real directories starting with .         */

   	/* more rmg mods to prevent hang */
	if (dbuf->d_name[0] != '.' && (S_ISDIR(sbuf.st_mode) ||
				       S_ISREG(sbuf.st_mode)) )
	{

      	/* The recursive call to nextdir will change the cwd  */
      	/* chdir("..") can't be used to get back because of   */
      	/* links... 					    */

	    if (S_ISDIR(sbuf.st_mode)) {
		nextdir(dbuf->d_name);    /* Process sub directory */
		chdir(cwdbuf);            /* Change back to current */
	    }
	    else if (S_ISREG(sbuf.st_mode)) {
		if (unival() <= fileprob)
		    readfile(dbuf->d_name);
	    }
	}
	dbuf = readdir(dirp);
    }
}

/* nextdir()
 *
 * Start a new directory
 */
void nextdir(char *dirname)
{
    DIR *dirp;               /* Directory handle */
    char cwdbuf[512];        /* Name buffer.     */

   if (unival() >= dirprob) return;

   dirprob *= dirfactor;
   dirp = opendir(dirname);

   if (dirp != 0) {
       chdir(dirname);
       getcwd(cwdbuf, sizeof(cwdbuf));
#ifdef RANDREAD_DEBUG
       printf("\n---> Processing directory %s \n", cwdbuf);
#endif
		/* quick check -- don't go into /proc */
       if (strcmp(cwdbuf,"/proc")) procdir(dirp, cwdbuf);
       closedir(dirp);
       /* chdir("..");   */
   }
   else {
       printf("Failed to open directory %s\n", dirname);
       printf("From directory %s \n", cwdbuf);
   }
   dirprob /= dirfactor;
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
