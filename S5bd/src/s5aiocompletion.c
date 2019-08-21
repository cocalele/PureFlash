/**
 * Copyright (C), 2014-2015.
 * @file  
 * This file implements the APIs to operate completions.
 */

#include <errno.h>
#include "s5aiocompletion.h"

void s5_aiocompletion_set_complete_cb(s5_aiocompletion_t* aiocompletion, void *cb_arg, callback_t cb) 
{
    aiocompletion->complete_cb = cb; 
    aiocompletion->complete_arg = cb_arg;
}

void s5_aiocompletion_init(s5_aiocompletion_t* aiocompletion, BOOL sync_or_not)
{
    aiocompletion->done = FALSE;
	aiocompletion->sync_or_not = sync_or_not;
	aiocompletion->status = 0;
	pthread_mutex_init(&aiocompletion->mutex, NULL);
    pthread_cond_init(&aiocompletion->cond, NULL);
}

void s5_aiocompletion_destroy(s5_aiocompletion_t* aiocompletion)
{
    pthread_mutex_destroy(&aiocompletion->mutex);
    pthread_cond_destroy(&aiocompletion->cond);

    free(aiocompletion);
}

s5_aiocompletion_t *s5_aio_create_completion(void *cb_arg, callback_t cb_complete, BOOL sync_or_not)
{
	s5_aiocompletion_t *c = (s5_aiocompletion_t*)malloc(sizeof(s5_aiocompletion_t));
	s5_aiocompletion_init(c, sync_or_not);
	s5_aiocompletion_set_complete_cb(c, cb_arg, cb_complete);
	return c;
}

void s5_aio_release_completion(s5_aiocompletion_t* aiocompletion)
{
	s5_aiocompletion_destroy(aiocompletion);
}

int s5_aiocompletion_wait_for_complete(s5_aiocompletion_t* aiocompletion)
{
	pthread_mutex_lock(&aiocompletion->mutex);
	while(!aiocompletion->done)
	{
		pthread_cond_wait(&aiocompletion->cond, &aiocompletion->mutex);
	}
	pthread_mutex_unlock(&aiocompletion->mutex);

	return 0;
}

void s5_aiocompletion_complete(s5_aiocompletion_t* aiocompletion)
{
	BOOL tmp_sync_or_not;
	pthread_mutex_lock(&aiocompletion->mutex);
	if(aiocompletion->complete_cb)
	{
		aiocompletion->complete_cb(aiocompletion->complete_arg,
			aiocompletion->status == 0 ? aiocompletion->filled * LBA_LENGTH : 0);//return length 0 to indicate error
	}
	aiocompletion->done = TRUE;
	tmp_sync_or_not = aiocompletion->sync_or_not;
	pthread_cond_signal(&aiocompletion->cond);
	pthread_mutex_unlock(&aiocompletion->mutex);
	

	if(tmp_sync_or_not == FALSE)
	{	
		s5_aiocompletion_destroy(aiocompletion);
	}
	
}

ssize_t s5_aiocompletion_get_return_value(s5_aiocompletion_t* aiocompletion)
{
	return aiocompletion->filled * LBA_LENGTH;// LBA_LENGTH to byte
}

