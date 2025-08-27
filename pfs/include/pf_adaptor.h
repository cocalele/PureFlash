/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
/**
 * Copyright (C), 2014-2015.
 * @file  
 * The common definition of s5afs
 */

#ifndef _ADAPTOR_H_
#define _ADAPTOR_H_

#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/**
 * brief Define the size of one node in S5afs
 *  
 * Defines the size of one node in S5afs, which is measured in MB.
 * This Macro is used for computing available capacity. 
 */
#define NODE_SIZE_IN_MB		4

#define HASH_BUCKET_COUNT 	10
#define HASH_NODE_COUNT 	128

/**
 * brief Macro pthread_nutex_t	
 *
 * Declare the afs lock
 */
#define afs_lock_mutex          pthread_mutex_t

/**
 * brief Macro initialize afs lock
 */
#define afs_lock_init(lock)     pthread_mutex_init(lock, NULL)

/**
 * brief Macro lock afs mutex
 */
#define afs_lock(lock)          pthread_mutex_lock(lock)

/**
 * brief Macro unlock afs mutex
 */
#define afs_unlock(lock)        pthread_mutex_unlock(lock)

#endif /* _ADAPTOR_H_ */

