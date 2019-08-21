#include <gtest/gtest.h>
#include <libs5bd.h>
#include <s5message.h>
#include "libs5manager.h"

#include <errno.h>
#include <vector>
#include <semaphore.h>
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>

#include "md5.h"

#define CONF_PATH "/etc/s5/s5.conf"

using std::vector;
uint32_t replica_num = 1;
int32_t tray_id[MAX_REPLICA_NUM] = {-1, -1, -1};
const char* s5store_name[MAX_REPLICA_NUM] = {0};

void print_tenant(s5_tenant_t* tenant)
{
	assert(tenant);
	printf("\ntenant(\n");
//	printf("\tversion: %s\n", tenant->version);
	printf("\tname: %s\n", tenant->name);
	printf("\tpass_wd: %s\n", tenant->pass_wd);
	printf("\tiops(M): %f\n", (double)tenant->iops / 1024.0);
	printf("\tbw(M): %f\n", (double)tenant->bw / 1024.0);
	printf("\tvolume(G): %f\n", (double)tenant->volume / 1048576.0);	
	printf("\tauth: %s\n", tenant->auth ? "Admin" : "Common User");
	printf(")\n\n");
}

void print_tenant_list(s5_tenant_list_t* tenants)
{
	s5_tenant_t* tenant = tenants->tenants;
	for(int i = 0; i < tenants->num; i++)
	{
		print_tenant(tenant);
		tenant++;
	}
}

void print_volume_info(s5_volume_info_t* volume)
{
	assert(volume);
	printf("(\n");
	printf("\tname: %s\n", volume->volume_name);
	printf("\ttenant: %s\n", volume->tenant_name);
	printf("\tiops(M): %f\n", double(volume->iops) /(1 << 20));
	printf("\tbw(M): %f\n", double(volume->bw) / (1 << 20));
	printf("\tsize(M): %f\n", double(volume->size) / (1 << 20));
	printf(")\n\n");
}

void print_volume_list(s5_volume_list_t* volumes)
{
	s5_volume_info_t* volume = volumes->volumes;
	printf("\n**********************************************************\n");
	printf("volume num: %d\n", volumes->num);	
	for(int i = 0; i < volumes->num; i++)
	{
		printf("volume %i:\n", i + 1);
		print_volume_info(volume);
		volume++;
	}	
	printf("\n**********************************************************\n");
}

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


typedef struct simple_aio_cb_args
{
	uint64_t op_id;
	uint64_t io_offset;
	uint64_t io_len;
	sem_t*	 sem;
}simple_aio_cb_args_t; 

void aio_cb_free_arg(void* cb_args, uint64_t res_len)
{
	simple_aio_cb_args_t* cb_arg = (simple_aio_cb_args_t*)cb_args;
	printf("AIO %lu ofs: %lu length: %lu finish...\n", cb_arg->op_id, cb_arg->io_len, cb_arg->io_offset);
	sem_post(cb_arg->sem);
	free(cb_arg);
}

TEST(libs5bd, TestLogin)
{
	s5_ioctx_t ioctx;
	ASSERT_EQ(-129, s5_create_ioctx("admin","123256", CONF_PATH, &ioctx));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EINVAL, s5_release_ioctx(&ioctx));
	ASSERT_EQ(-129, s5_create_ioctx("admin","1234256", CONF_PATH, &ioctx));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EINVAL, s5_release_ioctx(&ioctx));
	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &ioctx));
	ASSERT_EQ(0, s5_release_ioctx(&ioctx));
}

TEST(libs5bd, TestTenantOp)
{
	s5_ioctx_t ioctx;
	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &ioctx));

	//create tenant1
	ASSERT_EQ(0, s5_create_tenant(ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//create tenant 2
	ASSERT_EQ(0, s5_create_tenant(ioctx, "tenant2", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//try to create a tenant with identical name with tenant2	
	ASSERT_EQ(-EEXIST, s5_create_tenant(ioctx, "tenant2", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//list all tenant
	s5_tenant_list_t tenants;
	ASSERT_EQ(0, s5_list_tenant(ioctx, &tenants));
	ASSERT_EQ(2, tenants.num);			//with no-admin included
	print_tenant_list(&tenants);
	ASSERT_EQ(0, s5_release_tenantlist(&tenants));

	//delete tenant2
	ASSERT_EQ(0, s5_delete_tenant(ioctx, "tenant2"));

	//create tenant3
	ASSERT_EQ(0, s5_create_tenant(ioctx, "tenant3", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//list all tenant
	ASSERT_EQ(0, s5_list_tenant(ioctx, &tenants));
	ASSERT_EQ(2, tenants.num);
	print_tenant_list(&tenants);
	ASSERT_EQ(0, s5_release_tenantlist(&tenants));

	//update tenant1 with info of tenant2
	ASSERT_EQ(-EINVAL, s5_update_tenant(ioctx, "tenant1", "tenant3", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//list all tenant
	ASSERT_EQ(0, s5_list_tenant(ioctx, &tenants));
	ASSERT_EQ(2, tenants.num);
	print_tenant_list(&tenants);
	ASSERT_EQ(0, s5_release_tenantlist(&tenants));

	//remove all tenants created
	ASSERT_EQ(0, s5_delete_tenant(ioctx, "tenant1"));
	ASSERT_EQ(0, s5_delete_tenant(ioctx, "tenant3"));

	//list all tenant
	ASSERT_EQ(0, s5_list_tenant(ioctx, &tenants));
	ASSERT_EQ(0, tenants.num);
	print_tenant_list(&tenants);
	ASSERT_EQ(0, s5_release_tenantlist(&tenants));
	
	ASSERT_EQ(0, s5_release_ioctx(&ioctx));
}


TEST(libs5bd, TestTenantList)
{
	s5_ioctx_t ioctx;
	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &ioctx));

	ASSERT_EQ(0, s5_create_tenant(ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));
	ASSERT_EQ(0, s5_create_tenant(ioctx, "tenant2", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));
	ASSERT_EQ(0, s5_create_tenant(ioctx, "tenant3", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));
	ASSERT_EQ(0, s5_create_tenant(ioctx, "tenant4", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));
	ASSERT_EQ(0, s5_create_tenant(ioctx, "tenant5", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));
	ASSERT_EQ(0, s5_create_tenant(ioctx, "tenant6", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));
	ASSERT_EQ(0, s5_create_tenant(ioctx, "tenant7", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));
	ASSERT_EQ(0, s5_create_tenant(ioctx, "tenant8", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));
	ASSERT_EQ(0, s5_create_tenant(ioctx, "tenant9", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//list all tenant
	s5_tenant_list_t tenants;
	ASSERT_EQ(0, s5_list_tenant(ioctx, &tenants));
	ASSERT_EQ(9, tenants.num);			//with admin included
	print_tenant_list(&tenants);
	ASSERT_EQ(0, s5_release_tenantlist(&tenants));

	//remove all tenants created
	ASSERT_EQ(0, s5_delete_tenant(ioctx, "tenant1"));
	ASSERT_EQ(0, s5_delete_tenant(ioctx, "tenant2"));
	ASSERT_EQ(0, s5_delete_tenant(ioctx, "tenant3"));
	ASSERT_EQ(0, s5_delete_tenant(ioctx, "tenant4"));
	ASSERT_EQ(0, s5_delete_tenant(ioctx, "tenant5"));
	ASSERT_EQ(0, s5_delete_tenant(ioctx, "tenant6"));
	ASSERT_EQ(0, s5_delete_tenant(ioctx, "tenant7"));
	ASSERT_EQ(0, s5_delete_tenant(ioctx, "tenant8"));
	ASSERT_EQ(0, s5_delete_tenant(ioctx, "tenant9"));

	ASSERT_EQ(0, s5_release_ioctx(&ioctx));
}

TEST(libs5bd, TestVolumeOp)
{
	s5_ioctx_t admin_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &admin_ioctx));

	//create tenant1
	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//create tenant ioctx
	s5_ioctx_t tenant1_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));

	s5_volume_list_t volume_list;

	//list volumes of cluster
	ASSERT_EQ(0, s5_list_volume(admin_ioctx, &volume_list));
	ASSERT_EQ(0, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));
	
	//list volumes of tenant_1
	ASSERT_EQ(0, s5_list_volume_by_tenant(tenant1_ioctx, "tenant1", &volume_list));
	ASSERT_EQ(0, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));

	//create volume with no qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "tenant1", "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, 1, replica_num, tray_id, s5store_name));

	//create volume with no qutaset, with name conflict
	ASSERT_EQ(-EEXIST, s5_create_volume(tenant1_ioctx, "tenant1", "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, 1, replica_num, tray_id, s5store_name));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//create volume with no qutaset, with value exceeds capacity of corresponding item tenant
	ASSERT_EQ(-EINVAL, s5_create_volume(tenant1_ioctx, "tenant1", "volume_2", uint64_t(20) << 40, uint64_t(10) << 20, uint64_t(1) << 20, 1, replica_num, tray_id, s5store_name));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//list volumes of cluster
	ASSERT_EQ(0, s5_list_volume(admin_ioctx, &volume_list));
	ASSERT_EQ(1, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));
	
	//list volumes of tenant_1
	ASSERT_EQ(0, s5_list_volume_by_tenant(tenant1_ioctx, "tenant1", &volume_list));
	ASSERT_EQ(1, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));

	//list volumes of tenant_1
	ASSERT_EQ(0, s5_list_volume_by_tenant(tenant1_ioctx, "tenant1", &volume_list));
	ASSERT_EQ(1, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));

	
	//delete tenant
	ASSERT_EQ(-EBUSY, s5_delete_tenant(admin_ioctx, "tenant1"));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	ASSERT_EQ(0, s5_rename_volume(tenant1_ioctx, "tenant1", "volume_1", "volume_2_2"));

	//list volumes of tenant_1
	ASSERT_EQ(0, s5_list_volume_by_tenant(tenant1_ioctx, "tenant1", &volume_list));
	ASSERT_EQ(1, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));

	//delete volume_2
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "tenant1", "volume_2_2"));

	//list volumes of cluster
	ASSERT_EQ(0, s5_list_volume(admin_ioctx, &volume_list));
	ASSERT_EQ(0, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));
	
	//list volumes of tenant_1
	ASSERT_EQ(0, s5_list_volume_by_tenant(tenant1_ioctx, "tenant1", &volume_list));
	ASSERT_EQ(0, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));
	
	//delete volume_3
	ASSERT_EQ(-EINVAL, s5_delete_volume(tenant1_ioctx, "tenant1", "volume_3"));	
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//delete volume_1
	ASSERT_EQ(-EINVAL, s5_delete_volume(tenant1_ioctx, "tenant1", "volume_1"));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");


	ASSERT_EQ(0, s5_list_volume(admin_ioctx, &volume_list));
	ASSERT_EQ(0, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));

	//delete tenant
	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));
	
	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
}


TEST(libs5bd, TestVolumeStatNoQuotaset)
{
	s5_ioctx_t admin_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &admin_ioctx));

	//create tenant1
	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//create tenant ioctx
	s5_ioctx_t tenant1_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));

	//create volume with no qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "tenant1", "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, 1, replica_num, tray_id, s5store_name));

	//open volume
	s5_volume_t volume_ctx;
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "tenant1", "volume_1", NULL, &volume_ctx));

	//stat test
	s5_volume_info_t volume_info;
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx, &volume_info));
	print_volume_info(&volume_info);

	//delete volume_1
	ASSERT_EQ(-EBUSY, s5_delete_volume(tenant1_ioctx, "tenant1", "volume_1"));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//resize volume_1
	ASSERT_EQ(-EBUSY, s5_resize_volume(tenant1_ioctx, "tenant1", "volume_1", uint64_t(40) << 30));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//close volume
	ASSERT_EQ(0, s5_close_volume(&volume_ctx));

	//resize volume_1
	ASSERT_EQ(0, s5_resize_volume(tenant1_ioctx, "tenant1", "volume_1", uint64_t(40) << 30));

	//stat
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "tenant1", "volume_1", NULL, &volume_ctx));
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx, &volume_info));
	print_volume_info(&volume_info);
	ASSERT_EQ(0, s5_close_volume(&volume_ctx));

	//delete volume_1
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "tenant1", "volume_1"));

	//delete tenant
	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));

	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
}


TEST(libs5bd, TestVolumeStatWithQuotaset)
{
	s5_ioctx_t admin_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("admin","123456",CONF_PATH, &admin_ioctx));

	//create tenant1
	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//create tenant ioctx
	s5_ioctx_t tenant1_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));

	//create volume with qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "tenant1", "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, 1, replica_num, tray_id, s5store_name));

	//open volume
	s5_volume_t volume_ctx;
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "tenant1", "volume_1", NULL, &volume_ctx));

	//stat test
	s5_volume_info_t volume_info;
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx, &volume_info));
	print_volume_info(&volume_info);

	//delete volume_1
	ASSERT_EQ(-EBUSY, s5_delete_volume(tenant1_ioctx, "tenant1", "volume_1"));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//resize volume_1
	ASSERT_EQ(-EBUSY, s5_resize_volume(tenant1_ioctx, "tenant1", "volume_1", uint64_t(40) << 30));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//close volume
	ASSERT_EQ(0, s5_close_volume(&volume_ctx));

	//resize volume_1
	ASSERT_EQ(0, s5_resize_volume(tenant1_ioctx, "tenant1", "volume_1", uint64_t(40) << 30));

	//stat
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "tenant1", "volume_1", NULL, &volume_ctx));
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx, &volume_info));
	print_volume_info(&volume_info);
	ASSERT_EQ(0, s5_close_volume(&volume_ctx));

	//delete volume_1
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "tenant1", "volume_1"));
	
	//delete tenant
	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));

	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
}

TEST(libs5bd, TestVolumeWithQuotasetAIO)
{
	s5_ioctx_t admin_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &admin_ioctx));

	//create tenant1
	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//create tenant ioctx
	s5_ioctx_t tenant1_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));

	//create volume without qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "tenant1", "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, 1, replica_num, tray_id, s5store_name));

	//open volume
	s5_volume_t volume_ctx;
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "tenant1", "volume_1", NULL, &volume_ctx));

	//stat test
	s5_volume_info_t volume_info;
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx, &volume_info));
	print_volume_info(&volume_info);

	char src_buf[LBA_LENGTH] = {0};
	strcpy(src_buf, "s5bd_aio_test: hello, s5bd!");
	aio_cb_args_t aio_arg;
	aio_arg.op_id = 0;
	aio_arg.io_len = LBA_LENGTH;
	aio_arg.io_offset = LBA_LENGTH;
	aio_arg.io_type = TEST_WRITE;
	pthread_mutex_init(&aio_arg.mutex, NULL);
	pthread_cond_init(&aio_arg.cond, NULL);
	aio_arg.is_finished = false;
	ASSERT_EQ(0, s5_aio_write_volume(volume_ctx, aio_arg.io_offset, aio_arg.io_len,
		src_buf, aio_cb, (void*)(&aio_arg)));

	pthread_mutex_lock(&aio_arg.mutex);
	if (!aio_arg.is_finished)
	{
		pthread_cond_wait(&aio_arg.cond, &aio_arg.mutex);
	}	
	pthread_mutex_unlock(&aio_arg.mutex);

	aio_arg.io_type = TEST_READ;
	aio_arg.is_finished = false;
	char dst_buf[LBA_LENGTH] = {0};
	ASSERT_EQ(0, s5_aio_read_volume(volume_ctx, LBA_LENGTH, LBA_LENGTH, dst_buf, aio_cb, &aio_arg));
	
	pthread_mutex_lock(&aio_arg.mutex);
	if (!aio_arg.is_finished)
	{
		pthread_cond_wait(&aio_arg.cond, &aio_arg.mutex);
	}	
	pthread_mutex_unlock(&aio_arg.mutex);

	printf("data write: %s\n", src_buf);
	printf("data read: %s\n", dst_buf);
	ASSERT_EQ(0, strcmp(src_buf, dst_buf));

	//close volume
	ASSERT_EQ(0, s5_close_volume(&volume_ctx));

	//delete volume_1
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "tenant1", "volume_1"));

	//delete tenant
	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));

	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
}

TEST(libs5bd, TestVolumeWithQuotasetSIO)
{
	s5_ioctx_t admin_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &admin_ioctx));

	//create tenant1
	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//create tenant ioctx
	s5_ioctx_t tenant1_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));

	//create volume with qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "tenant1", "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, 1, replica_num, tray_id, s5store_name));

	//open volume
	s5_volume_t volume_ctx;
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "tenant1", "volume_1", NULL, &volume_ctx));

	//stat test
	s5_volume_info_t volume_info;
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx, &volume_info));
	print_volume_info(&volume_info);

	char src_buf[LBA_LENGTH] = {0};
	strcpy(src_buf, "s5_write_volume_test: hello, s5bd!");
	
	ASSERT_EQ(LBA_LENGTH, s5_write_volume(volume_ctx, LBA_LENGTH, LBA_LENGTH, src_buf));

	char dst_buf[LBA_LENGTH] = {0};
	ASSERT_EQ(LBA_LENGTH, s5_read_volume(volume_ctx, LBA_LENGTH, LBA_LENGTH, dst_buf));


	printf("data write: %s\n", src_buf);
	printf("data read: %s\n", dst_buf);
	ASSERT_EQ(0, strcmp(src_buf, dst_buf));

	//close volume
	ASSERT_EQ(0, s5_close_volume(&volume_ctx));

	//delete volume_1
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "tenant1", "volume_1"));

	//delete tenant
	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));

	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
}

TEST(libs5bd, TestVolumeWithQuotasetReadOnlyOpen)
{
	s5_ioctx_t admin_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &admin_ioctx));

	//create tenant1
	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//create tenant ioctx
	s5_ioctx_t tenant1_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));

	//create volume with qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "tenant1", "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, 1, replica_num, tray_id, s5store_name));

	//open volume
	s5_volume_t volume_ctx;
	ASSERT_EQ(0, s5_open_volume_read_only(tenant1_ioctx, "tenant1", "volume_1", NULL, &volume_ctx));

	//stat test
	s5_volume_info_t volume_info;
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx, &volume_info));
	print_volume_info(&volume_info);

	char src_buf[LBA_LENGTH] = {0};
	strcpy(src_buf, "s5_write_volume_test: hello, s5bd!");

	ASSERT_EQ(-EACCES, s5_write_volume(volume_ctx, 0, LBA_LENGTH, src_buf));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	char dst_buf[LBA_LENGTH] = {0};
	ASSERT_EQ(LBA_LENGTH, s5_read_volume(volume_ctx, 0, LBA_LENGTH, dst_buf));


	printf("data write: %s\n", src_buf);
	printf("data read: %s\n", dst_buf);

	//close volume
	ASSERT_EQ(0, s5_close_volume(&volume_ctx));

	//delete volume_1
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "tenant1", "volume_1"));

	//delete tenant
	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));

	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
}


TEST(libs5bd, TestVolumeUpdate)
{
	s5_ioctx_t admin_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &admin_ioctx));
	
	//create tenant1
	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//create tenant ioctx
	s5_ioctx_t tenant1_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));

	//create volume with qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "tenant1", "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, 1, replica_num, tray_id, s5store_name));
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "tenant1", "volume_2", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, 1, replica_num, tray_id, s5store_name));

	//open volume
	s5_volume_t volume_ctx1, volume_ctx2;
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "tenant1", "volume_1", NULL, &volume_ctx1));
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "tenant1", "volume_2", NULL, &volume_ctx2));

	//stat test
	s5_volume_info_t volume_info;
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx1, &volume_info));
	print_volume_info(&volume_info);
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx2, &volume_info));
	print_volume_info(&volume_info);

	ASSERT_EQ(-EBUSY, s5_update_volume(tenant1_ioctx, "tenant1", "volume_1", "volume_1_2", uint64_t(4) << 20, uint64_t(1) << 20, uint64_t(40) << 10, 0));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EBUSY, s5_update_volume(tenant1_ioctx, "tenant1", "volume_2", "volume_2_2", uint64_t(4) << 20, uint64_t(1) << 20, uint64_t(40) << 10, 0));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//close volume
	ASSERT_EQ(0, s5_close_volume(&volume_ctx2));
	ASSERT_EQ(0, s5_close_volume(&volume_ctx1));

	ASSERT_EQ(0, s5_update_volume(tenant1_ioctx, "tenant1", "volume_1", "volume_1_2", -1, -1, -1, -1));
	ASSERT_EQ(0, s5_update_volume(tenant1_ioctx, "tenant1", "volume_2", "volume_2_2", -1, -1, -1, -1));

	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "tenant1", "volume_1_2", NULL, &volume_ctx1));
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx1, &volume_info));
	print_volume_info(&volume_info);
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "tenant1", "volume_2_2", NULL, &volume_ctx2));
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx2, &volume_info));
	print_volume_info(&volume_info);

	//close volume
	ASSERT_EQ(0, s5_close_volume(&volume_ctx1));
	ASSERT_EQ(0, s5_close_volume(&volume_ctx2));

	ASSERT_EQ(0, s5_update_volume(tenant1_ioctx, "tenant1", "volume_1_2", NULL, -1, -1, -1, -1));
	ASSERT_EQ(0, s5_update_volume(tenant1_ioctx, "tenant1", "volume_2_2", NULL, -1, -1, -1, -1));

	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "tenant1", "volume_1_2", NULL, &volume_ctx1));
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx1, &volume_info));
	print_volume_info(&volume_info);
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "tenant1", "volume_2_2", NULL, &volume_ctx2));
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx2, &volume_info));
	print_volume_info(&volume_info);
	
	//close volume
	ASSERT_EQ(0, s5_close_volume(&volume_ctx1));
	ASSERT_EQ(0, s5_close_volume(&volume_ctx2));

	ASSERT_EQ(0, s5_update_volume(tenant1_ioctx, "tenant1", "volume_2_2", NULL, -1, -1, -1, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "tenant1", "volume_2_2", NULL, &volume_ctx2));
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx2, &volume_info));
	print_volume_info(&volume_info);

	//close volume
	ASSERT_EQ(0, s5_close_volume(&volume_ctx2));


	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "tenant1", "volume_1_2", NULL, int64_t(10) << 40, -1, -1, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "tenant1", "volume_2_2", NULL, int64_t(10) << 40, -1, -1, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "tenant1", "volume_1_2", NULL, 10, -1, -1, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "tenant1", "volume_2_2", NULL, 10, -1, -1, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "tenant1", "volume_1_2", NULL, -1, int64_t(100) << 30, -1, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "tenant1", "volume_2_2", NULL, -1, int64_t(100) << 30, -1, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "tenant1", "volume_1_2", NULL, -1, -1, int64_t(100) << 40, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "tenant1", "volume_2_2", NULL, -1, -1, int64_t(100) << 40, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//delete volume_1
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "tenant1", "volume_2_2"));
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "tenant1", "volume_1_2"));

	//delete tenant
	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));

	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
}


TEST(libs5bd, TestVolumeImportExport)
{
	s5_ioctx_t admin_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &admin_ioctx));

	//create tenant1
	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//create tenant ioctx
	s5_ioctx_t tenant1_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));

	//create test data
	ASSERT_EQ(0, system("dd if=/dev/urandom bs=4096 count=4096 of=/var/tmp/random.data"));

	//import 
	ASSERT_EQ(0, s5_import_image(tenant1_ioctx, "tenant1", "volume_1", "/var/tmp/random.data", uint64_t(10) << 20, uint64_t(1) << 20, 1, replica_num, tray_id, s5store_name));

	//list volumes of cluster
	s5_volume_list_t volume_list;
	ASSERT_EQ(0, s5_list_volume(admin_ioctx, &volume_list));
	ASSERT_EQ(1, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));

	//export
	ASSERT_EQ(0, s5_export_image(tenant1_ioctx, "tenant1", "/var/tmp/import_test.data", "volume_1"));

	char src_f_md5[MD5_STR_LEN + 1] = {0};
	char dst_f_md5[MD5_STR_LEN + 1] = {0};

	ASSERT_EQ(0, compute_file_md5("/var/tmp/random.data", src_f_md5, MD5_STR_LEN + 1));
	ASSERT_EQ(0, compute_file_md5("/var/tmp/import_test.data", dst_f_md5, MD5_STR_LEN + 1));
	printf("md5 of src:%s\n", src_f_md5);
	printf("md5 of dst:%s\n", dst_f_md5);
	ASSERT_EQ(0, strcmp(dst_f_md5, src_f_md5));

	//delete volume_1
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "tenant1", "volume_1"));

	//delete tenant
	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));

	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));

	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
}

TEST(libs5bd, TestOpen)
{
	s5_ioctx_t admin_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &admin_ioctx));

	//create tenant1
	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 30, uint64_t(40) << 30));

	//create tenant ioctx
	s5_ioctx_t tenant1_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));

	//create volume with no qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "tenant1", "volume_1", uint64_t(2) << 30, uint64_t(10) << 20, uint64_t(1) << 20, 1, replica_num, tray_id, s5store_name));

	//open volume
	s5_volume_t volume_ctx1;

	for (int i = 0; i < 20; i++)
	{
		printf("Volume open: %d\n", i);
		ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "tenant1", "volume_1", NULL, &volume_ctx1));
		//close volume

		printf("Volume close: %d\n", i);
		ASSERT_EQ(0, s5_close_volume(&volume_ctx1));
	}
	

	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "tenant1", "volume_1"));

	//delete tenant
	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));

	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));

	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
}

int main(int argc, char **argv)
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
