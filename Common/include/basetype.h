#ifndef __BASE_TYPE_H__
#define __BASE_TYPE_H__

/**
* Copyright (C), 2014-2020.
* @file
* Base type definitions.
*
* This file includes all base type definition, which are used by S5 modules.
*/


#include <linux/types.h>
#include <stdint.h>

typedef unsigned int BOOL;
#define TRUE 1
#define FALSE 0

#define SHARD_LBA_CNT (16LL << 20) //a LBA is 4K, 1<<SHARD_LBA_CNT_ORDER
#define SHARD_LBA_CNT_ORDER 24
#define SHARD_SIZE_ORDER 36 //64G, = 1<<36
#define	LBA_LENGTH		4096	///< LBA's length.
#define LBA_LENGTH_ORDER 12
#define	S5_OBJ_LEN		(4*1024*1024)	///< S5 object's length.

#define S5_MAX_IO_SIZE (16<<10)
#endif //__BASE_TYPE_H__

