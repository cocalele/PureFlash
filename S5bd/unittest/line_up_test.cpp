#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "libs5bd.h"
#include "atomic_op.h"
#include "mutex/utime.h"
#include "mutex/clock.h"

#define	S5_OBJ_LEN		(4*1024*1024)
#define	LBA_LENGTH		4096

static AtomicInt32 buffer1writelocation = AtomicInt32(-1);
static AtomicInt32 buffer2writelocation = AtomicInt32(-1);


#define MAX_LINE_UP_NUM 64
char mem_4k_write1[LBA_LENGTH * MAX_LINE_UP_NUM];
char mem_4k_read1[LBA_LENGTH * MAX_LINE_UP_NUM];
char mem_4k_write2[LBA_LENGTH * MAX_LINE_UP_NUM];
char mem_4k_read2[LBA_LENGTH * MAX_LINE_UP_NUM];
char mem_4k[LBA_LENGTH];
char char_beginer = '!';

typedef struct aio_cb_args
{
	pthread_mutex_t mutex;
	int count;
}aio_cb_args_t; 


struct mop
{
	int flag;//read(0) or write(1)
	int read_location;//where is buffer located
	int write_location;
	int buffer_num;// 1: buffer1, 2: buffer2
	mop()
	{
		flag = -1;
		read_location = 0;
		write_location = -1;
		buffer_num = 0;
	}
	aio_cb_args_t *aio_args;
};
typedef void *image_ctx_t;

typedef void *s5bd_completion_t;


mop mopargs1[MAX_LINE_UP_NUM];
mop mopargs2[MAX_LINE_UP_NUM];


void aio_callback ( void *arg, uint64_t res_len)
{
	mop* mp = (mop*)arg;

	int r = -1;

	if(0 == mp->flag)
	{
		if(1 == mp->buffer_num)
		{
			if(mp->write_location < 0)
			{
				r = memcmp(mem_4k, mem_4k_read1 + (mp->read_location * LBA_LENGTH), LBA_LENGTH);
				printf("\n==buffer1 write_location=%d read_location=%d return=%d\n", mp->write_location, mp->read_location, r);
			}
			else
			{
				r = memcmp(mem_4k_write1 + (mp->write_location * LBA_LENGTH), mem_4k_read1 + (mp->read_location * LBA_LENGTH), LBA_LENGTH);
				printf("\n==buffer1 write_location(%c)=%d read_location(%c)=%d return=%d\n", *(mem_4k_write1 + (mp->write_location * LBA_LENGTH)),
				       mp->write_location, *(mem_4k_read1 + (mp->read_location * LBA_LENGTH)), mp->read_location, r);
			}
		}
		else if(2 == mp->buffer_num)
		{
			if(mp->write_location < 0)
			{
				r = memcmp(mem_4k, mem_4k_read2 + (mp->read_location * LBA_LENGTH), LBA_LENGTH);
				printf("\n==buffer1 write_location=%d read_location=%d return=%d\n", mp->write_location, mp->read_location, r);
			}
			else
			{
				r = memcmp(mem_4k_write2 + (mp->write_location * LBA_LENGTH), mem_4k_read2 + (mp->read_location * LBA_LENGTH), LBA_LENGTH);
				printf("\n==buffer2 write_location(%c)=%d read_location(%c)=%d return=%d\n", *(mem_4k_write2 + (mp->write_location * LBA_LENGTH)),
				       mp->write_location,  *(mem_4k_read2 + (mp->read_location * LBA_LENGTH)), mp->read_location, r);
			}
		}
		else
		{
			assert(0);
		}
	}

	pthread_mutex_lock(&mp->aio_args->mutex);
	mp->aio_args->count++;
	pthread_mutex_unlock(&mp->aio_args->mutex);

	return;
}

void init()
{
	memset(mem_4k_write1, 0, LBA_LENGTH * MAX_LINE_UP_NUM);
	memset(mem_4k_read1, 0, LBA_LENGTH * MAX_LINE_UP_NUM);
	memset(mem_4k_write2, 0, LBA_LENGTH * MAX_LINE_UP_NUM);
	memset(mem_4k_read2, 0, LBA_LENGTH * MAX_LINE_UP_NUM);
	memset(mem_4k, char_beginer, LBA_LENGTH);

	for(int i = 0; i < MAX_LINE_UP_NUM; ++i)
	{
		memset(mem_4k_write1 + (i * LBA_LENGTH), char(char_beginer + i), LBA_LENGTH);
		memset(mem_4k_write2 + (i * LBA_LENGTH), char(char_beginer + i), LBA_LENGTH);
	}

}

void line_up_test(s5_volume_t volume, int *slota, int slota_len, int *slotb, int slotb_len)
{
	s5_write_volume(volume, 0, LBA_LENGTH, mem_4k);
	s5_write_volume(volume, S5_OBJ_LEN, LBA_LENGTH, mem_4k);
	aio_cb_args_t aio_arg;
	aio_arg.count = 0;
	pthread_mutex_init(&aio_arg.mutex, NULL);
	int count = 0;
	int i =0;
	for(i = 0; i < MAX_LINE_UP_NUM; ++i)
	{
		mopargs1[i].aio_args = &aio_arg;
		mopargs2[i].aio_args = &aio_arg;		
	}
	for(i = 0; i != slota_len; ++i)
	{
		count++;
		if(0 == slota[i])
		{
			mopargs1[i].flag = 0;
			mopargs1[i].buffer_num = 1;
			mopargs1[i].read_location = i;
			mopargs1[i].write_location = buffer1writelocation.value();
			s5_aio_read_volume(volume, 0, LBA_LENGTH, mem_4k_read1 + i * LBA_LENGTH, aio_callback, (void*)&mopargs1[i]);
		}
		else
		{
			mopargs1[i].flag = 1;
			mopargs1[i].buffer_num = 1;
			buffer1writelocation = i;
			s5_aio_write_volume(volume, 0, LBA_LENGTH, mem_4k_write1 + i * LBA_LENGTH, aio_callback,  (void*)&mopargs1[i]);
		}
	}

	for(i = 0; i != slotb_len; ++i)
	{
		count++;
		if(0 == slotb[i])
		{
			mopargs2[i].flag = 0;
			mopargs2[i].buffer_num = 2;
			mopargs2[i].read_location = i;
			mopargs2[i].write_location = buffer2writelocation.value();
			s5_aio_read_volume(volume, S5_OBJ_LEN, LBA_LENGTH, mem_4k_read2 + i * LBA_LENGTH, aio_callback,  (void*)&mopargs2[i]);
		}
		else
		{
			mopargs2[i].flag = 1;
			mopargs2[i].buffer_num = 2;
			buffer2writelocation = i;
			s5_aio_write_volume(volume, S5_OBJ_LEN, LBA_LENGTH, mem_4k_write2 + i * LBA_LENGTH,aio_callback,  (void*)&mopargs2[i]);
		}
	}
	while(aio_arg.count < count);
	pthread_mutex_destroy(&aio_arg.mutex);
}

int main()
{
	s5_init("/ceph_run/s5.conf");
	s5_ioctx_t admin_ioctx;
	s5_create_ioctx("admin","123456",&admin_ioctx);
	s5_login(admin_ioctx);

	//create tenant1
	s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30);

	//create tenant ioctx
	s5_ioctx_t tenant1_ioctx;
	s5_create_ioctx("tenant1","123456",&tenant1_ioctx);
	s5_login(tenant1_ioctx);

	//create volume with no qutaset
	s5_create_volume(tenant1_ioctx, "volume-1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX);
    
	//open volume
	s5_volume_t volume_ctx;
	s5_open_volume(tenant1_ioctx, "volume-1", NULL, &volume_ctx);


	int slot1[] = {0, 0, 0, 1, 1, 1, 0, 1, 1, 0};
	int slot2[] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 0};

	init();
	line_up_test(volume_ctx, slot1, sizeof(slot1) / sizeof(slot1[0]), slot2, sizeof(slot2) / sizeof(slot2[0]));


    s5_close_volume(&volume_ctx);

	//delete volume-1
	s5_delete_volume(tenant1_ioctx, "volume-1");
	//delete tenant
	s5_delete_tenant(admin_ioctx, "tenant1");

	s5_release_ioctx(&admin_ioctx);
	s5_release_ioctx(&tenant1_ioctx);

}


