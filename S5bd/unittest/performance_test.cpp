#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "libs5bd.h"
#include "atomic_op.h"
#include "mutex/utime.h"
#include "mutex/clock.h"


#include <errno.h>
#include <vector>
#include <semaphore.h>
#include <assert.h>
#include <pthread.h>


#define	S5_OBJ_LEN		(4*1024*1024)
#define	LBA_LENGTH		4096



//add begin
typedef enum aio_type
{
	TEST_READ = 1,
	TEST_WRITE
}aio_type_t;

typedef struct aio_cb_args
{
	pthread_cond_t cond;
	pthread_mutex_t mutex;

	int count;
	int max_count;
}aio_cb_args_t; 


void aio_cb_count(void* cb_args, uint64_t res_len)
{
	aio_cb_args_t* cb_arg = (aio_cb_args_t*)cb_args;
	pthread_mutex_lock(&cb_arg->mutex);
	cb_arg->count++;
	pthread_mutex_unlock(&cb_arg->mutex);
}

//add end

static AtomicUInt32 num = AtomicUInt32(0);


void aio_callback (s5bd_completion_t cb, void *arg)
{
	AtomicUInt32* count = (AtomicUInt32*)arg;
	--(*count);
	return;
}

void aio_cb_count_init(const int max_count)
{
	pthread_mutex_t q_len_lock;
	pthread_mutex_init(&q_len_lock, NULL);

	aio_cb_args_t aio_arg;
	pthread_mutex_init(&aio_arg.mutex, NULL);
    aio_arg.count = 0;
	aio_arg.max_count = max_count;
}
//================s5_aio_write_volume 4M==================
void aio_write_4m(s5_volume_t image)
{
	const int kAioCount = 64;

	char *mem_4m = (char*)malloc(S5_OBJ_LEN * kAioCount);

	memset(mem_4m, -1, S5_OBJ_LEN);
	uint64 len = S5_OBJ_LEN;

	S5::utime_t start_time, elapsed;
	start_time = S5::ceph_clock_now();


	aio_cb_args_t aio_arg;
	pthread_mutex_init(&aio_arg.mutex, NULL);
    aio_arg.count = 0;

	for(int i = 0; i < kAioCount; ++i)
	{
		s5_aio_write_volume(image,  0, len,mem_4m + i * len, aio_cb_count, (void*)(&aio_arg));
	}
	while(aio_arg.count < kAioCount);

	pthread_mutex_destroy(&aio_arg.mutex);

	elapsed = S5::ceph_clock_now() - start_time;
	printf("aio_write: len = %llu bytes, times = %d, total time = %" PRIu64 " nsec.\n", len, kAioCount, elapsed.to_nsec());
	free(mem_4m);

}

//================s5_aio_write_volume 4k==================
void aio_write_4k(s5_volume_t image)
{

	const int kAioCount = 64;

	char *mem_4k = (char*)malloc(LBA_LENGTH * kAioCount);

	memset(mem_4k, -1, LBA_LENGTH);

	uint64 len = LBA_LENGTH;

	S5::utime_t start_time, elapsed;
	start_time = S5::ceph_clock_now();


	aio_cb_args_t aio_arg;
	pthread_mutex_init(&aio_arg.mutex, NULL);
    aio_arg.count = 0;



	for(int i = 0; i < kAioCount; ++i)
	{
		s5_aio_write_volume(image,  0, len,mem_4k + i * len, aio_cb_count, (void*)(&aio_arg));
	}

	while(aio_arg.count < kAioCount);

	pthread_mutex_destroy(&aio_arg.mutex);
	elapsed = S5::ceph_clock_now() - start_time;
	printf("aio_write: len = %llu bytes, times = %d, total time = %" PRIu64 " nsec.\n", len, kAioCount, elapsed.to_nsec());
	free(mem_4k);

}

//================s5_aio_read_volume 4M==================
void aio_read_4m(s5_volume_t image)
{

	const int kAioReadCount = 64;

	char *mem_readbuf4m = (char*)malloc(S5_OBJ_LEN * kAioReadCount);
	assert(NULL != mem_readbuf4m);
	memset(mem_readbuf4m, -1, S5_OBJ_LEN * kAioReadCount);

	uint64 len = S5_OBJ_LEN;

	S5::utime_t start_time, elapsed;
	start_time = S5::ceph_clock_now();


	aio_cb_args_t aio_arg;
	pthread_mutex_init(&aio_arg.mutex, NULL);
    aio_arg.count = 0;

	for(int i = 0; i < kAioReadCount; ++i)
	{
		s5_aio_read_volume(image, 0, len, mem_readbuf4m  + i * len, aio_cb_count, &aio_arg);
	}

	while(aio_arg.count < kAioReadCount);

	pthread_mutex_destroy(&aio_arg.mutex);
	elapsed = S5::ceph_clock_now() - start_time;
	printf("aio_read: len = %llu bytes, times = %d, total time = %" PRIu64 " nsec.\n", len, kAioReadCount, elapsed.to_nsec());


	free(mem_readbuf4m);

}

//================s5_aio_read_volume 4k==================
void aio_read_4k(s5_volume_t image)
{

	const int kAioReadCount = 64;

	char *mem_readbuf4k = (char*)malloc(LBA_LENGTH * kAioReadCount);
	assert(NULL != mem_readbuf4k);
	memset(mem_readbuf4k, -1, LBA_LENGTH * kAioReadCount);


	uint64 len = LBA_LENGTH;

	S5::utime_t start_time, elapsed;
	start_time = S5::ceph_clock_now();


	aio_cb_args_t aio_arg;
	pthread_mutex_init(&aio_arg.mutex, NULL);
    aio_arg.count = 0;


	for(int i = 0; i < kAioReadCount; ++i)
	{
		s5_aio_read_volume(image, 0, len, mem_readbuf4k + i * len, aio_cb_count, &aio_arg);
	}

	while(aio_arg.count < kAioReadCount);

	pthread_mutex_destroy(&aio_arg.mutex);

	elapsed = S5::ceph_clock_now() - start_time;
	printf("aio_read: len = %llu bytes, times = %d, total time = %" PRIu64 " nsec.\n", len, kAioReadCount, elapsed.to_nsec());

	free(mem_readbuf4k);

}

int main()
{
	if(s5_init("/ceph_run/s5.conf") != 0)
	{
		printf("s5_init:failure\r\n");
	}
	s5_ioctx_t admin_ioctx;
	if(s5_create_ioctx("admin","123456",&admin_ioctx) != 0)
	{
		printf("s5_create_ioctx:failure\r\n");
	}
	if(s5_login(admin_ioctx) != 0)
	{
		printf("s5_login:failure\r\n");		
	}

	//create tenant1
	if(s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30) != 0)
	{
		printf("s5_create_tenant:failure\r\n");	
	}

	//create tenant ioctx
	s5_ioctx_t tenant1_ioctx;
	if(s5_create_ioctx("tenant1","123456",&tenant1_ioctx) != 0)
	{
		printf("s5_create_ioctx:failure\r\n");
	}
	if(s5_login(tenant1_ioctx) != 0)
	{
		printf("s5_login:failure\r\n");
	}

	//create volume with no qutaset
	if(s5_create_volume(tenant1_ioctx, "volume-1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX) != 0)
	{
		printf("s5_create_volume:failure\r\n");
	}
	//open volume
	s5_volume_t volume_ctx;
	if(s5_open_volume(tenant1_ioctx, "volume-1", NULL, &volume_ctx) != 0)
	{
		printf("s5_open_volume:failure\r\n");
	}

	aio_write_4m(volume_ctx);
	aio_read_4m(volume_ctx);	
	aio_write_4k(volume_ctx);
	aio_read_4k(volume_ctx);

    if(s5_close_volume(&volume_ctx) != 0)
    {
		printf("s5_close_volume:failure\r\n");
	}
	//delete volume-1
	if(s5_delete_volume(tenant1_ioctx,"volume-1") != 0)
	{
		printf("s5_delete_volume:failure\r\n");
	}
	//delete tenant
	if(s5_delete_tenant(admin_ioctx, "tenant1") != 0)
	{
		printf("s5_delete_tenant:failure\r\n");
	}
	if(s5_release_ioctx(&admin_ioctx) != 0)
	{
		printf("s5_release_ioctx:failure\r\n");
	}
	if(s5_release_ioctx(&tenant1_ioctx) != 0)
	{
		printf("s5_release_ioctx:failure\r\n");
	}


}
