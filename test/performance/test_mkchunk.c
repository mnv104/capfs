#include <stdio.h>

extern int mkchunk(int argc, char *argv[]);
int main(int argc, char *argv[])
{
	return mkchunk(argc, argv);
}
