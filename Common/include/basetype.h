#ifndef __BASE_TYPE_H__
#define __BASE_TYPE_H__

/**
* Copyright (C), 2014-2015.
* @file
* Base type definitions.
*
* This file includes all base type definition, which are used by S5 modules.
*/


#include <linux/types.h>

#ifndef EOK
#define EOK 0	///< represent return value is OK.
#endif

typedef unsigned char uchar;
typedef short int16;
typedef unsigned short uint16;
typedef int int32;
typedef unsigned int uint32;
typedef long int  int64;
typedef unsigned long int uint64;
typedef unsigned char BOOL;
#define TRUE 1
#define FALSE 0
#endif //__BASE_TYPE_H__

