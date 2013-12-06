#ifndef _BIT_VECTOR_H
#define _BIT_VECTOR_H

#include <string.h>

static unsigned char flags_to_AND[8] = {1,2,4,8,16,32,64,128};

/* use this function only for powers of 2 */
static __inline__ int LOG_2(int x)
{
	int count = 0, val = 1;
	if (!x)
	{
		return -2;
	}
	if ((x) & ((x)-1)) 
	{
		return -1;
	}
	do
	{
		count++;
		val = (x) & 1;
		x = x >> 1;
	} while (!val);
	return (count - 1);
}

static __inline__ int SetBit(unsigned char *bitmap, int bitmap_size, int nr)
{
	unsigned char *byte = NULL;

	if (!bitmap || nr < 0 || bitmap_size <= 0) {
	 	return -1;
	}
	if (nr / 8 >= bitmap_size) {
		return -1;
	}
	byte = &bitmap[nr / 8]; 
	*byte |= flags_to_AND[nr % 8]; 
	return 0;
}

static __inline__ int ClearBit(unsigned char *bitmap, int bitmap_size, int nr)
{
   unsigned char *byte = NULL;

   if (!bitmap || nr < 0 || bitmap_size <= 0) {
      return -1;
	}
	if (nr / 8 >= bitmap_size) {
		return -1;
	}
	byte = &bitmap[nr / 8];
	*byte &= (unsigned char)(~flags_to_AND[nr % 8]);
   return 0;
}

static __inline__ void PrintBit(unsigned char *bitmap,int bitmap_size)
{
   int i = 0;
   unsigned char *byte=NULL;

	if (!bitmap || bitmap_size <= 0) 
	{
		return;
	}
	for (i = 0;i < bitmap_size; i++) 
	{
      byte = &bitmap[i];
		if(*byte)
		{
			printf("Copy set byte %d: %x\n", i, *byte);
		}
	}
   return;
}

static __inline__ int TestBit(unsigned char *bitmap, int bitmap_size, int nr)
{
   unsigned char *byte = NULL;

	if (!bitmap || nr < 0 || bitmap_size <= 0) 
	{
		return -1;
	}
	if (nr / 8 >= bitmap_size) {
		return -1;
	}
   byte = &bitmap[nr / 8];
	return (*byte & flags_to_AND[nr % 8]) ? 1 : 0;
}

static __inline__ int NumBit(unsigned char *bitmap, int bitmap_size)
{
   int count = 0, i = 0;

	if (!bitmap || bitmap_size <= 0) 
	{
		return -1;
	}
   for (i = 0;i < (8 * bitmap_size); i++) 
	{
		int ret = 0;
		if ((ret = TestBit(bitmap, bitmap_size, i)) == 1)
			 count++;
		else if (ret < 0) {
			return -1;
		}
	}
   return count;
}

static __inline__ int ClearAll(unsigned char *bitmap, int bitmap_size)
{
	if (!bitmap || bitmap_size <= 0) 
	{
		return -1;
	}
	memset(bitmap, 0, bitmap_size);
   return 0;
}

/*
 * This routine tries to find out if there is 1 other bit set in bitmap
 * other than "which_bit" and returns that position.
 */
static __inline__ int OnlyOtherBitSet(unsigned char *bitmap, int bitmap_size, int which_bit)
{
	int flag = 0, position = 0, ret = 0, i = 0;
	unsigned char *tmp_bitmap = NULL;

	if (!bitmap || bitmap_size <= 0) 
	{
		return -1;
	}
	tmp_bitmap = (unsigned char *) calloc(1, bitmap_size);
	if (tmp_bitmap == NULL)
	{
		return -1;
	}
	memcpy(tmp_bitmap, bitmap, bitmap_size);
	/* Clear out the indicated bit */
	ClearBit(tmp_bitmap, bitmap_size, which_bit);
	/* and then search... */
	for (i = 0;i < bitmap_size; i++) 
	{
		  unsigned char *byte = NULL;
		  byte = (unsigned char *) &tmp_bitmap[i];
		  if((ret = LOG_2(*byte)) == -2) 
		  {
			  if(!flag)
				  position += 8;
			  continue;
		  }
		  else if(ret == -1) 
		  {
			  free(tmp_bitmap);
			  return -1;
		  }
		  else {
			  /* detected a non zero byte previously */
			  if (flag) {
				  free(tmp_bitmap);
				  return -1;
			  }
			  flag=1;
			  position += ret;
		  }
	}
	free(tmp_bitmap);
	return (position == (bitmap_size * 8)) ? -1 : position;
}

static __inline__ int FindFirstBit(unsigned char *bitmap, int bitmap_size)
{
	int i;
	if (!bitmap || bitmap_size <= 0) 
	{
		return -1;
	}
	for (i = 0; i < (8 * bitmap_size); i++) 
	{
		int ret = 0;
		if ((ret = TestBit(bitmap, bitmap_size, i)) == 1)
		{
			return i;
		}
		else if (ret < 0)
			return -1;
	}
	return -1;
}

static __inline__ int FindNextBit(unsigned char *bitmap, int bitmap_size, int offset)
{
	int i;
	if (!bitmap || bitmap_size <= 0 || offset < 0 || offset >= (8 * bitmap_size)) 
	{
		return -1;
	}
	for (i = offset;i < (8 * bitmap_size); i++)
	{
		int ret = 0;
		if ((ret = TestBit(bitmap, bitmap_size, i)) == 1)
		{
			return i;
		}
		else if (ret < 0)
			return -1;
	}
	return -1;
}

#endif
/*
 * Local variables:
 *  c-indent-level: 3
 *  c-basic-offset: 3
 *  tab-width: 3
 * End:
 *
 * vim: ts=3
 */

