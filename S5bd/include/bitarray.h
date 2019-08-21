/**
 * Copyright (C), 2014-2015.
 * @file
 * This file declares the basic operation on the bit array
 */

#ifndef __BIT_ARRAY_H__
#define __BIT_ARRAY_H__


#ifdef __cplusplus
extern "C" {
#endif

#include "basetype.h"

typedef void *bitarray;

/**
 * Init bit array.
 *
 * This function is used to init bit array.
 *
 * @param[in, out]		barr		init sz as id could be alloced.
 * @param[in]			length	bits to be initlized.
 *
 * @return		0 on success, negative error code on failure
 * @retval		0			success
 * @retval		-ENOMEM 	run out of memory.
 */
int bitarray_init(bitarray *barr, int length);

/**
 * Release bit array.
 *
 * This function is used to release bit array.
 *
 * @param[in, out]		barr		bit array to release.
 */
void bitarray_release(bitarray barr);

/**
 * Set bits.
 *
 * This function is used to set bits.
 *
 * @param[in]		barr		bit array to set.
 * @param[in]		off		offset in bit array.
 * @param[in]		len		length of bits to set.
 *
 * @return		bits set success or failed.
 * @retval		TRUE	set bits [offset, offset+len)
 * @retval		FALSE 	set no bits.
 */
BOOL bitarray_set(bitarray barr, int off, int len);

/**
 * Reset bits.
 *
 * This function is used to reset bits.
 *
 * @param[in]		barr	bit array to reset.
 * @param[in]		off		offset in bit array.
 * @param[in]		len		length of bits to reset.
 */
void bitarray_reset(bitarray barr, int off, int len);

#ifdef __cplusplus
}
#endif

#endif
