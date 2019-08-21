/**
 * Copyright (C), 2014-2015.
 * @file
 * This file declares the callback completion data structure, and APIs to operate completions.
 */

#ifndef __S5AIOCOMPLETION_H__
#define __S5AIOCOMPLETION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "basetype.h"
#include "s5imagectx.h"
#include "libs5bd.h"

/**
 * Declares the callback function format.
 */
typedef s5bd_callback_t callback_t;

/**
 * 	Define the Async completion
 */
typedef struct s5_aiocompletion
{
	pthread_mutex_t mutex;		 ///< The mutex for pthread_cond_t
	pthread_cond_t cond;		 ///< The conditional varable for notify completion is done
	volatile BOOL done;			 ///< Whether the aiocompletion is done or not
	uint32 nlba;				 ///< The number of LBA_LENGTH which this completion is waiting for.
	uint32 filled;				 ///< The number of finished nlba.
	callback_t complete_cb;		 ///< The callback_t for this completion.
	void *complete_arg;			 ///< The argument of complete_cb.
	BOOL sync_or_not;
	int status;				 ///< Return status of IO, 0 for success, other value means Error
} s5_aiocompletion_t;

/**
 *  Create async IO completion
 *
 *  This function will create an async completion function, according to the input argument and callback.
 *
 *  @param[in]	 cb_arg		The input argument for creating aio completion.
 *  @param[in]   cb_complete	The user specified callback function.
 *  @return      The pointer to created async IO completion. This pointer should be maintained by user, and deleted by using s5_aio_release_completion.
 */
s5_aiocompletion_t *s5_aio_create_completion(void *cb_arg, callback_t cb_complete, BOOL sync_or_not);

/**
 *  Delete one async IO completion
 *
 *  This function will delete an async completion function.
 *
 *  @param[in]   aiocompletion	The pointer to an existing aio completion.
 *  @return      No return.
 */
void s5_aio_release_completion(s5_aiocompletion_t* aiocompletion);

/**
 *  Wait for the aio completion done.
 *
 *  This function will return 0 until aio completion is done, otherwise, keep waiting.
 *
 *  @param[in]   aiocompletion  The pointer to an existing aio completion.
 *  @return      0		Success
 */
int s5_aiocompletion_wait_for_complete(s5_aiocompletion_t* aiocompletion);

/**
 *  Notify the aio completion done.
 *
 *  This function will notify to the user: input aiocompletion is done.
 *
 *  @param[in]   aiocompletion  The pointer to an existing aio completion.
 *  @return      No return.
 */
void s5_aiocompletion_complete(s5_aiocompletion_t* aiocompletion);

/**
 *  Return the byte of finished operation data length.
 *
 *  Each IO request will have a user expected operation data length. This function will return the finished operation data length.
 *
 *  @param[in]   aiocompletion  The pointer to an existing aio completion.
 *  @return      The byte number of finished operation data length.
 */
ssize_t s5_aiocompletion_get_return_value(s5_aiocompletion_t* aiocompletion);

#ifdef __cplusplus
}
#endif

#endif
