#include <gtest/gtest.h>
#include <libs5bd.h>
#include <libs5bd.hpp>
#include <s5_meta.h>

#include <errno.h>
#include <vector>
#include <semaphore.h>
#include <assert.h>
#include <pthread.h>


using std::vector;

#define TEST_IO_BLOCK_SIZE 4096*1024 //1M 
#define TEST_IO_SIZE 4096*4 //16k 

int add(const int &a, const int &b)
{
	return a + b;
}

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
	uint64_t op_id;
	uint64_t io_offset;
	uint64_t io_len;
	aio_type_t io_type;
	bool is_finished;
	int count;
}aio_cb_args_t; 

void aio_cb(void* cb_args, uint64_t res_len)
{
	aio_cb_args_t* cb_arg = (aio_cb_args_t*)cb_args;
	printf("Aio(id: %lu type: %s offset: %lu len: %lu res: %lu) finish...\n", cb_arg->op_id,
		cb_arg->io_type == TEST_READ ? "TEST_READ" : "TEST_WRITE", cb_arg->io_offset, cb_arg->io_len, res_len);
	pthread_mutex_lock(&cb_arg->mutex);
	cb_arg->is_finished = true;
	pthread_cond_signal(&cb_arg->cond);
	pthread_mutex_unlock(&cb_arg->mutex);
}

void aio_cb_count(void* cb_args, uint64_t res_len)
{
	aio_cb_args_t* cb_arg = (aio_cb_args_t*)cb_args;
	printf("Aio(id: %lu type: %s offset: %lu len: %lu res: %lu) finish...\n", cb_arg->op_id,
		cb_arg->io_type == TEST_READ ? "TEST_READ" : "TEST_WRITE", cb_arg->io_offset, cb_arg->io_len, res_len);
	pthread_mutex_lock(&cb_arg->mutex);
	cb_arg->count++;
	pthread_mutex_unlock(&cb_arg->mutex);
}

//add end

TEST(libs5bd, add)
{
	EXPECT_EQ(2, add(1, 1));
	EXPECT_EQ(8, add(3, 5));
}


void aio_write_test_data(s5_volume_t image, const char *test_data, uint64_t off, size_t len)
{
	aio_cb_args_t aio_arg;
	aio_arg.op_id = 0;
	aio_arg.io_len = len;
	aio_arg.io_offset = off;
	aio_arg.io_type = TEST_WRITE;
	pthread_mutex_init(&aio_arg.mutex, NULL);
	pthread_cond_init(&aio_arg.cond, NULL);
	aio_arg.is_finished = false;
	s5_aio_write_volume(image, aio_arg.io_offset, aio_arg.io_len,test_data, aio_cb, (void*)(&aio_arg));

	pthread_mutex_lock(&aio_arg.mutex);
	if (!aio_arg.is_finished)
	{
		pthread_cond_wait(&aio_arg.cond, &aio_arg.mutex);
	}	
	pthread_mutex_unlock(&aio_arg.mutex);
	
	printf("started write\n");

}

void write_test_data(s5_volume_t image, const char *test_data, uint64_t off, size_t len)
{
	ssize_t written;
	written = s5_write_volume(image, off, len, test_data);
	printf("wrote: %d\n", (int) written);
}


void aio_read_test_data(s5_volume_t image, const char *expected, uint64_t off, size_t len)
{
	char *result = (char *)malloc(len + 1);
	assert(result);
	aio_cb_args_t aio_arg;
	aio_arg.op_id = 0;
	aio_arg.io_len = len;
	aio_arg.io_offset = off;
	aio_arg.io_type = TEST_READ;
	pthread_mutex_init(&aio_arg.mutex, NULL);
	pthread_cond_init(&aio_arg.cond, NULL);
	aio_arg.is_finished = false;
	s5_aio_read_volume(image, aio_arg.io_offset, aio_arg.io_len, result, aio_cb, &aio_arg);


	pthread_mutex_lock(&aio_arg.mutex);
	if (!aio_arg.is_finished)
	{
		pthread_cond_wait(&aio_arg.cond, &aio_arg.mutex);
	}	
	pthread_mutex_unlock(&aio_arg.mutex);
	
	printf("started read\n");

	ASSERT_EQ(0, memcmp(result, expected, len));

	free(result);
}

void read_test_data(s5_volume_t image, const char *expected, uint64_t off, size_t len)
{
	ssize_t read;
	char *result = (char *)malloc(len + 1);

	assert(result);
	read = s5_read_volume(image, off, len, result);
	printf("read: %d\n", (int) read);

	result[len] = '\0';
	if (memcmp(result, expected, len))
	{
		printf("read: %s\nexpected: %s\n", result, expected);
		assert(memcmp(result, expected, len) == 0);
	}
	free(result);
}

TEST(libs5bd, TestCopy)
{

     
	ASSERT_EQ(0, s5_init("/ceph_run/s5.conf"));
	s5_ioctx_t admin_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("admin","123456",&admin_ioctx));
	ASSERT_EQ(0, s5_login(admin_ioctx));

	//create tenant1
	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//create tenant ioctx
	s5_ioctx_t tenant1_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456",&tenant1_ioctx));
	ASSERT_EQ(0, s5_login(tenant1_ioctx));

	//create volume with no qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume-1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX));
    
	//open volume
	s5_volume_t volume_ctx;
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume-1", NULL, &volume_ctx));

    ASSERT_EQ(0, s5_close_volume(&volume_ctx));

	//delete volume-1
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume-1"));
	//delete tenant
	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));

	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
}

TEST(libs5bd, TestIO)
{
	char orig_data[TEST_IO_BLOCK_SIZE + 1];
	memset(orig_data, '*', TEST_IO_BLOCK_SIZE);
	orig_data[TEST_IO_BLOCK_SIZE] = '\0';

	char *result = (char *)malloc(TEST_IO_BLOCK_SIZE + 1);
	result[TEST_IO_BLOCK_SIZE] = '\0';
 

	ASSERT_EQ(0, s5_init("/ceph_run/s5.conf"));
	s5_ioctx_t admin_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("admin","123456",&admin_ioctx));
	ASSERT_EQ(0, s5_login(admin_ioctx));

	//create tenant1
	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//create tenant ioctx
	s5_ioctx_t tenant1_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456",&tenant1_ioctx));
	ASSERT_EQ(0, s5_login(tenant1_ioctx));

	//create volume with no qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume-1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX));

		
	//open volume
	s5_volume_t volume_ctx;
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume-1", NULL, &volume_ctx));

	int offset = 0;
	for(int j = 0; j != 7; j++)
	{
		offset = TEST_IO_BLOCK_SIZE * j;

		printf(">>>>>>>>write block[%d]...\n", j);

		ASSERT_EQ(TEST_IO_BLOCK_SIZE, s5_write_volume(volume_ctx, offset, TEST_IO_BLOCK_SIZE, orig_data));

		ASSERT_EQ(TEST_IO_BLOCK_SIZE, s5_read_volume(volume_ctx, offset, TEST_IO_BLOCK_SIZE, result));
	}

	ASSERT_EQ(0, memcmp(orig_data, result, TEST_IO_BLOCK_SIZE));

	char test_data[TEST_IO_SIZE + 1];
	char zero_data[TEST_IO_SIZE + 1];

	memset(test_data, '+', TEST_IO_SIZE);
	test_data[TEST_IO_SIZE] = '\0';

	memset(zero_data, 0, sizeof(zero_data));
	zero_data[TEST_IO_SIZE] = '\0';


	for (int i = 0; i < 10; ++i)
	{
		aio_write_test_data(volume_ctx, test_data, TEST_IO_SIZE * i, TEST_IO_SIZE);
	}


	for (int i = 0; i < 10; ++i)
	{
		aio_read_test_data(volume_ctx, test_data, TEST_IO_SIZE * i, TEST_IO_SIZE);
	}


	ASSERT_EQ(0, s5_close_volume(&volume_ctx));
	
	//delete volume-1
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume-1"));

	//delete tenant
	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));

	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));


	free(result);
}


TEST(libs5bd, TestOpen)
{
	ASSERT_EQ(0, s5_init("/ceph_run/s5.conf"));
	s5_ioctx_t admin_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("admin","123456",&admin_ioctx));
	ASSERT_EQ(0, s5_login(admin_ioctx));

	//create tenant1
	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//create tenant ioctx
	s5_ioctx_t tenant1_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456",&tenant1_ioctx));
	ASSERT_EQ(0, s5_login(tenant1_ioctx));

	//create volume with no qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume-1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX));

		//open volume
	s5_volume_t volume_ctx[100];

	uint64_t count = 100;
	uint64_t i = 0;
	while(i < count)
	{
		ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume-1", NULL, &volume_ctx[i++]));
	}
	i = 0;
	while(i < count)
    {
		ASSERT_EQ(0, s5_close_volume(&volume_ctx[i++]));
    }
	
	//delete volume-1
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume-1"));

	//delete tenant
	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));

	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));


}



TEST(libs5bd, TestAIO)
{
	char *result = (char *)malloc(TEST_IO_BLOCK_SIZE + 1);
	char *expected = (char *)malloc(TEST_IO_BLOCK_SIZE + 1);
	memset(result, 0, TEST_IO_BLOCK_SIZE);
	memset(expected, 0, TEST_IO_BLOCK_SIZE);
	result[TEST_IO_BLOCK_SIZE] = '\0';
	expected[TEST_IO_BLOCK_SIZE] = '\0';

	char orig_data[TEST_IO_BLOCK_SIZE + 1];
	memset(orig_data, '*', TEST_IO_BLOCK_SIZE);
	orig_data[TEST_IO_BLOCK_SIZE] = '\0';

	ASSERT_EQ(0, s5_init("/ceph_run/s5.conf"));
	s5_ioctx_t admin_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("admin","123456",&admin_ioctx));
	ASSERT_EQ(0, s5_login(admin_ioctx));

	//create tenant1
	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//create tenant ioctx
	s5_ioctx_t tenant1_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456",&tenant1_ioctx));
	ASSERT_EQ(0, s5_login(tenant1_ioctx));

	//create volume with no qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume-1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX));

	//open volume
	s5_volume_t volume_ctx;
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume-1", NULL, &volume_ctx));

	const int testAioCount = 10;
	int len = 4096;

	pthread_mutex_t q_len_lock;
	pthread_mutex_init(&q_len_lock, NULL);

	aio_cb_args_t aio_arg;
	aio_arg.op_id = 0;
	aio_arg.io_type = TEST_WRITE;
	pthread_mutex_init(&aio_arg.mutex, NULL);
    aio_arg.count = 0;

	for (int i = 0; i < testAioCount; ++i)
	{
		s5_aio_write_volume(volume_ctx, 0, len, orig_data+ i * len, aio_cb_count, &aio_arg);
	}

    while(1)
    {
        if(aio_arg.count == testAioCount)
            break; //all finished
    }
	aio_arg.count = 0;
	aio_arg.io_type = TEST_READ;
	for(int i = 0; i != testAioCount; ++i)
	{
  	    s5_aio_read_volume(volume_ctx, 0, len, expected+ i * len, aio_cb_count, &aio_arg);
	}

	while(1)
	{
		if(aio_arg.count == testAioCount)
			break; //all finished
	}
	printf("Finish all aio read!\n");

	ASSERT_EQ(0, memcmp(orig_data, expected, len*10));

	free(result);
	free(expected);

	ASSERT_EQ(0, s5_close_volume(&volume_ctx));
	
	//delete volume-1
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx,  "volume-1"));

	//delete tenant
	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));

	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
	
}

int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);

	return RUN_ALL_TESTS();
}
