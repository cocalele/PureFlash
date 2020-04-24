#ifndef __S5IPCDEF__
#define __S5IPCDEF__

/**
* Copyright (C), 2014-2015.
* @file
* s5ipc C API.
*
* This file includes all s5ipc data structures and APIs.
*/

#ifdef __cplusplus
extern "C" {
#endif

#include "pf_message.h"
#include <semaphore.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define	  MOE_NOTIFY_ITEMS_MAX		128	///< moe items count.
#define   SEM_NAME_MOE         "pf_lock-moe"	///< request lock's name of semaphore.
#define   SEM_NAME_MOE_ACK     "pf_lock-moe-ack"	///< reply lock's name of semaphore.

#define	  MOE_DEFAULT_FILE 		"/usr/tmp/.pf_shr_moe"	///< ipc's file name.

/**
 * moe share memory type.
 */
typedef enum shm_type
{
	SHM_TYPE_MOE 	 = 0,	///< request share mem type.
	SHM_TYPE_MOE_ACK = 1,	///< reply share memory type
	SHM_TYPE_MAX
} shm_type_t;

/**
 * moe's share memory structure.
 */
typedef struct moe_notify_shm
{
	int count;
	pf_message_head_t	msg_head[MOE_NOTIFY_ITEMS_MAX];
} moe_notify_shm_t;



/**
 * init semaphore lock for protecting moe's share memory.
 *
 * @param[in] type 	share memory type.
 * @param[in] value	the default value of lock.
 * @return 	pointer 	to sem_t on success.
 * @retval     NULL   	SEM_FAILED the result of sem_open().
 */
sem_t*  s5shm_init_lock(shm_type_t type, int value);

#define s5shm_release_lock(lock) sem_close(lock)	///< release lock.
#define s5shm_wait(lock)		 sem_wait(lock)		///< wait lock.
#define s5shm_post(lock)		 sem_post(lock)		///< post lock.

/**
 * open the share memory for type's moe share memory.
 *
 * the length of share memory is sizeof(moe_notify_shm_t).
 * @param[in] type	share memory type.
 * @return 	pointer to share memory on success.
 * @retval     NULL	MAP_FAILED the result of mmap().
 */
void* s5shm_open(shm_type_t type);


/**
 * close the share memory for type's moe share memory.
 *
 * @param[in] shrmem	pointer to share memory.
 * @param[in] len		the length of share memory.
 * @return 	the result of munmap().
 */
int	s5shm_close(void* shrmem, size_t len);


/**
 * init semaphore lock for protecting moe's share memory.
 *
 * @param[in] start		start address of share memory want to sync.
 * @param[in] len		the length of share memory want to sync.
 * @return 	the result of msync().
 */
int s5shm_sync(void* start, size_t len);


#ifdef __cplusplus
}
#endif

#endif	/*__S5IPCDEF__*/

