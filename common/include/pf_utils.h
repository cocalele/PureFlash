#ifndef __S5_UTILS_H
#define __S5_UTILS_H

/**
* Copyright (C), 2014-2015.
* @file
* Miscellaneous-functions API.
*
* This file includes all APIs for miscellaneous functions.
*/


#include "assert.h"
#include <stdint.h>
#include <arpa/inet.h>
#include <string>
#include <functional>
#include <vector>
#include <stdexcept>
#include <numeric> //std::accumulate
#include <iostream>
#include "pf_conf.h"
#include "pf_log.h"
#include "basetype.h"
#include "pthread.h"

#ifdef DEBUG

/**
* S5ASSERT assert macro log trace information and call assert.
*
* @param[in] x, verify statement x is true or not.
*/
#define S5ASSERT(x) if(!(x)) { S5LOG_ERROR("Assertion %s", #x); assert(x);}
#else
#define S5ASSERT(x)
#endif

//#define STATIC_ASSERT(cond, msg) typedef char static_assertion_##msg[(cond)?1:-1];
#define STATIC_ASSERT static_assert

#ifdef __cplusplus
extern "C" {
#endif
#define S5ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))	///<get the item's count of array.

//#define max(a,b)    (((a) > (b)) ? (a) : (b))		///<get the bigger from two numbers.
//#define min(a,b)    (((a) < (b)) ? (a) : (b))		///<get the smaller from two numbers.
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#define pf_offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)


#define pf_container_of(ptr, type, member) \
((type *) ((uint8_t *)(ptr) - pf_offsetof(type, member)))


/**
* free memory macro.
*
* @param[in] p	pointer to pointer to memory.
*/
#define freeP(p)        \
    do{                 \
        if(*p){         \
            free(*p);   \
            (*p) = NULL;\
        }               \
    }while(0)

/**
 * Dump s5message's head.
 *
 * @param[in] phead  pointer to s5message's head.
 * @return	0 on success.
 */
#define DUMP_MSG_HEAD(phead)   \
	do{ 					  \
	S5LOG_DEBUG("Debug info: magic:%d, msg_type:%d, transaction_id:%d, user_id:%d, pool_id:%d,"    \
	"data_len:%d, image_id:%lu, slba:%lu, nlba:%d, obj_ver:%d, " 	 \
	"listen_port:%d, snap_seq:%d, status:%d, iops_density:%d, is_head:%d",								\
	phead.magic_num, phead.msg_type, phead.transaction_id, phead.user_id, phead.pool_id,  \
	phead.data_len, phead.volume_id, phead.slba, phead.nlba, phead.obj_ver, 	\
	phead.listen_port, phead.snap_seq, phead.status, phead.iops_density, phead.is_head);  \
	}while(0)


/**
 * Add function for atomic.
 */
#define pf_atomic_add(target, value)	__sync_add_and_fetch(target, value)

/**
 * Sub function for atomic.
 */
#define pf_atomic_sub(target, value)	__sync_sub_and_fetch(target, value)

/**
 * Get the value of atomic.
 */
#define pf_atomic_get(target)			__sync_add_and_fetch(target, 0)

/**
 * Set the value to atomic.
 */
#define pf_atomic_set(target, value)	__sync_lock_test_and_set(target, value)

/**
 * Common function to exit one thread, pthread_cancel and pthread_join.
 *
 * @param[in] pid		pid of thread to exit.
 * @return	0			on success, error code on failure.
 * @retval	0			success.
 * @retval	EDEADLK	A deadlock was detected,  or thread specifies the calling thread.
 * @retval	EINVAL 		thread is not a joinable thread.
 * @retval	EINVAL 		Another thread is already waiting to join with this thread.
 * @retval	ESRCH  		No thread with the ID thread could be found.
 */
int exit_thread(pthread_t pid);


/**
* write a pid file refer to filename.
*
* @param[in] filename	the name of pid file.
*/
void write_pid_file(const char* filename);

/**
 * copy string safely. Safely means never buffer overflow, so sizeof the destination buffer needed.
 * safe_strcpy will copy at most dest_size-1 characters, make sure there's a '\0' at the end of string.
 * Not as strncpy, that doesn't add terminating '\0' when string length exceed dest_size-1
 *
 * @param dest[out] the buffer to copy string to
 * @param src[in] the string to copy from
 * @param dest_size[in] the memory space of dest buffer, including the terminating '\0' to place
 */
char* safe_strcpy(char* dest, const char* src, size_t dest_size);

/**
 * Check the validation of input ip
 *
 * @param ip[in] input ip address
 * @return if the input ip is valid or not.
 */
BOOL isIpValid(const char* ip);

/**
 * compute cbs by iops
 */
uint64_t get_cbs_by_iops(uint64_t iops);

inline uint64_t up_align(uint64_t number, uint64_t alignment)
{
	return ((number + alignment - 1) / alignment)*alignment;
}
const char* get_git_ver();

#ifdef __cplusplus
}
#endif
uint64_t now_time_usec();
#define US2MS(u) ((u)/1000)
#define SEC2US(s) (s*1000000L)
const std::string format_string(const char * format, ...);
const std::string get_socket_desc(int sock_fd, bool is_client);
const std::string get_rdma_desc(struct rdma_cm_id* id, bool is_client);
void split_string(const std::string& str, char delim, std::vector<std::string>& tokens);
std::vector<std::string> split_string(const std::string& str, char delim);
std::vector<std::string> split_string(const std::string& str, const std::string& delim);
template<typename T>
std::string join(std::vector<T> const& vec)
{
	if (vec.empty()) {
		return std::string();
	}

	std::string delim = ",";
	return std::accumulate(vec.begin() + 1, vec.end(), std::to_string(vec[0]),
		[](const std::string& a, T b) {
			return a + ", " + std::to_string(b);
		});
}

class DeferCall {
	std::function<void(void)> f;
public:
	DeferCall(std::function<void(void)> _f) :f(_f){}
	~DeferCall() { f(); }
};


class Cleaner {
	std::vector<std::function<void(void)> > clean_func;
public:
	void push_back(std::function<void(void)> f) noexcept {
		clean_func.push_back(f);
	}
	void cancel_all() noexcept {
		clean_func.clear();
	}
	~Cleaner() {
		for (auto i = clean_func.rbegin(); i != clean_func.rend(); ++i)
			(*i)();
	}
};

void * align_malloc_spdk(size_t align, size_t size, uint64_t *phys_addr);

void free_spdk(void *buf);
#endif
