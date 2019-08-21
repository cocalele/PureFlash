#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include "bitarray.h"

#define BITSIZE 8
#define SHIFT 3


typedef struct _bitarray
{
	void *arr;		// size = len / BITSIZE
	int len;		// (SLOT_SIZE)length = 1024
} _bitarray_t;


int bitarray_init(bitarray *barr, int length)
{
	int r = 0;
	struct _bitarray *a = (struct _bitarray*)malloc(sizeof(struct _bitarray));
	if(a == NULL)
	{
		r = -ENOMEM;
		goto FINALLY;
	}

	a->len = length;
	size_t len = (size_t)(length / BITSIZE);
	if(length % BITSIZE)
	{
		++len;
	}
	a->arr = malloc(len);

	if(a->arr == NULL)
	{
		free(a);
		a = NULL;
		r = -ENOMEM;
		goto FINALLY;
	}
	else
	{
		memset(a->arr, 0, len);
		goto FINALLY;
	}

FINALLY:
	*barr = a;
	return r;
}

void bitarray_release(bitarray barr)
{
	if(barr)
	{
		struct _bitarray *a = (struct _bitarray*)barr;
		if(a->arr)
		{
			free(a->arr);
			a->arr = NULL;
		}
		free(a);
	}
}

BOOL bitarray_set(bitarray barr, int off, int len)
{
	struct _bitarray *a = (struct _bitarray*)barr;
	assert(off >= 0 && off < a->len && len > 0 && len <= a->len);
	char *arr = (char*)a->arr;
	BOOL collision = FALSE;
	int length = len;
	int offset = off;
	int slot_off;
	int slot_len;

	while(length > 0)
	{
		slot_off = offset % BITSIZE;
		slot_len = BITSIZE - slot_off;
		if(slot_len > length)
		{
			slot_len = length;
		}

		int mask = (1 << slot_len) - 1;
		if(arr[offset >> SHIFT] & (char)(mask << slot_off))
		{
			collision = TRUE;
			break;
		}
		length -= slot_len;
		offset += (BITSIZE - slot_off);
	}

	if(collision)
	{
		return FALSE;
	}

	length = len;
	offset = off;

	while(length > 0)
	{
		slot_off = offset % BITSIZE;
		slot_len = BITSIZE - slot_off;
		if(slot_len > length)
		{
			slot_len = length;
		}

		int mask = (1 << slot_len) - 1;
		arr[offset >> SHIFT] |= (char)(mask << slot_off);

		length -= slot_len;
		offset += (BITSIZE - slot_off);
	}

	return TRUE;
}

void bitarray_reset(bitarray barr, int off, int len)
{
	struct _bitarray *a = (struct _bitarray*)barr;
	assert(off >= 0 && off < a->len && len > 0 && len <= a->len);
	char *arr = (char*)a->arr;
	int length = len;
	int offset = off;
	int slot_off;
	int slot_len;

	while(length > 0)
	{
		slot_off = offset % BITSIZE;
		slot_len = BITSIZE - slot_off;
		if(slot_len > length)
		{
			slot_len = length;
		}

		// get the mask value of the operation
		// if slot len = 10(uint: 4k), the mask is: 111111111
		int mask = (1 << slot_len) - 1;
		
		// offset >> SHIFT : transfer the index from offset to arr index
		// mask << slot_off: fill the lower bits with zero 
		arr[offset >> SHIFT] &= (char)(~(mask << slot_off));

		length -= slot_len;
		offset += (BITSIZE - slot_off);
	}

	return;
}


