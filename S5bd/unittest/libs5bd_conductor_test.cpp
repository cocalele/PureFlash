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

void print_parent_info(s5bd_parent_info_t* parent)
{
	printf("Parent info:\n");
	printf("{\n");
	printf("\tname: %s\n", parent->volume_name);
	printf("\tsnap: %s\n", parent->snap_name);
	printf("\ttenant: %s\n", parent->tenant_name);
	printf("\tquotaset: %s\n", parent->quotaset_name);
	printf("}\n");
}

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

void print_quotaset(s5_quotaset_t* quotaset)
{
	assert(quotaset);
	printf("(\n");
	printf("\tname: %s\n", quotaset->name);
	printf("\ttenant: %s\n", quotaset->tenant_name);	
	printf("\tiops(M): %f\n", (double)quotaset->iops / 1024.0);
	printf("\tbw(M): %f\n", (double)quotaset->bw / 1024.0);
	printf(")\n\n");
}

void print_quotaset_list(s5_quotaset_list_t* quotasets)
{
	s5_quotaset_t* quotaset = quotasets->quotasets;
	printf("\n**********************************************************\n");
	printf("quotaset info:\n");
	for(int i = 0; i < quotasets->num; i++)
	{
		printf("quotaset 1:");
		print_quotaset(quotaset);
		quotaset++;
	}

	printf("\n**********************************************************\n");
}

void print_volume_info(s5_volume_info_t* volume)
{
	assert(volume);
	printf("(\n");
	printf("\tname: %s\n", volume->volume_name);
	printf("\ttenant: %s\n", volume->tenant_name);
	printf("\tquotaset: %s\n", volume->quotaset_name);
	printf("\tiops(M): %f\n", double(volume->iops) /(1 << 20));
	printf("\tbw(M): %f\n", double(volume->bw) / (1 << 20));
	printf("\tsize(M): %f\n", double(volume->size) / (1 << 20));
	printf(")\n\n");
}

void print_volume_list(s5bd_list_t* volumes)
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

void print_snap(s5bd_snap_info_t* snap)
{
	if (!snap)
	{
		return;
	}
	printf("{\n");
	printf("\tname: %s\n", snap->name);
	printf("\tid: %lu\n", snap->id);
	printf("\tsize: %lu\n", snap->size);
	printf("}\n");
}

void print_snap_list(s5bd_snapinfo_list_t* snaplist)
{
	if (!snaplist)
	{
		return;
	}

	s5bd_snap_info_t* cur_snap = NULL;
	printf("Total snaps count: %d\n", snaplist->num);
	printf("*******************************************************************\n");
	for (int i = 0; i < snaplist->num; i++)
	{
		cur_snap = snaplist->snap_list + i;
		print_snap(cur_snap);
		printf("*******************************************************************\n");
	}

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
	ASSERT_EQ(0, s5_release_ioctx(&ioctx));
	ASSERT_EQ(-129, s5_create_ioctx("admin","1234256", CONF_PATH, &ioctx));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(0, s5_release_ioctx(&ioctx));
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

TEST(libs5bd, TestQuotasetOp)
{
	s5_ioctx_t ioctx;
	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &ioctx));

	//create tenant1
	ASSERT_EQ(0, s5_create_tenant(ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));
	ASSERT_EQ(0, s5_create_tenant(ioctx, "tenant2", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30));

	//login tenant 
	s5_ioctx_t tenant1_ioctx;
	s5_ioctx_t tenant2_ioctx;
	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));
	ASSERT_EQ(0, s5_create_ioctx("tenant2","123456", CONF_PATH, &tenant2_ioctx));

	//create quotaset 1
	ASSERT_EQ(0, s5_quotaset_create(tenant1_ioctx, "quotaset_1_1", uint64_t(10) << 20, uint64_t(1) << 20));
	ASSERT_EQ(0, s5_quotaset_create(tenant2_ioctx, "quotaset_2_1", uint64_t(10) << 20, uint64_t(1) << 20));

	//create quotaset 2 with identical name with quotaset 1, and both of them belongs to tenant1
	ASSERT_EQ(-EINVAL, s5_quotaset_create(tenant1_ioctx, "quotaset_1_1", uint64_t(10) << 20, uint64_t(1) << 20));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//create quotaset 3, with iops exceeds capasity of tenant
	ASSERT_EQ(-EINVAL, s5_quotaset_create(tenant1_ioctx, "quotaset_1_3", uint64_t(10) << 30, uint64_t(1) << 30));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//create quotaset 4
	ASSERT_EQ(0, s5_quotaset_create(tenant1_ioctx, "quotaset_1_4", uint64_t(10) << 20, uint64_t(10) << 20));
	ASSERT_EQ(0, s5_quotaset_create(tenant2_ioctx, "quotaset_2_4", uint64_t(10) << 20, uint64_t(10) << 20));

	//list qutasets
	s5_quotaset_list_t quotaset_list;
	ASSERT_EQ(0, s5_list_quotasets_of_tenant(tenant1_ioctx, "tenant1", &quotaset_list));
	ASSERT_EQ(2, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	ASSERT_EQ(0, s5_quotasets_list(ioctx, &quotaset_list));
	ASSERT_EQ(4, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	//delete quotaset 2, quotaset 2 doesnot exist actually
	ASSERT_EQ(-ENOENT, s5_quotaset_delete(tenant1_ioctx, "quotaset_1_2"));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(0, s5_list_quotasets_of_tenant(tenant1_ioctx, "tenant1", &quotaset_list));
	ASSERT_EQ(2, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	ASSERT_EQ(0, s5_quotasets_list(ioctx, &quotaset_list));
	ASSERT_EQ(4, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	//delete quotaset 3
	ASSERT_EQ(-ENOENT, s5_quotaset_delete(tenant1_ioctx, "quotaset_1_3"));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(0, s5_list_quotasets_of_tenant(tenant1_ioctx, "tenant1", &quotaset_list));
	ASSERT_EQ(2, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	ASSERT_EQ(0, s5_quotasets_list(ioctx, &quotaset_list));
	ASSERT_EQ(4, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	//update quotaset 4, all update with valid value
	ASSERT_EQ(0, s5_quotaset_update(tenant1_ioctx, "quotaset_1_4", "quotaset_1_4_1", uint64_t(8) << 20, uint64_t(2) << 20));
	ASSERT_EQ(0, s5_list_quotasets_of_tenant(tenant1_ioctx, "tenant1", &quotaset_list));
	ASSERT_EQ(2, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	ASSERT_EQ(0, s5_quotasets_list(ioctx, &quotaset_list));
	ASSERT_EQ(4, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	//update quotaset 4, now, 'quotaset_4' does not exist;
	ASSERT_EQ(-ENOENT, s5_quotaset_update(tenant1_ioctx, "quotaset_1_4", "quotaset_4_2", uint64_t(8) << 40, uint64_t(2) << 20));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(0, s5_list_quotasets_of_tenant(tenant1_ioctx, "tenant1", &quotaset_list));
	ASSERT_EQ(2, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	ASSERT_EQ(0, s5_quotasets_list(ioctx, &quotaset_list));
	ASSERT_EQ(4, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	//all update with invalid value(iops of new setting exceeds capacity of tenant1)
	ASSERT_EQ(-EINVAL, s5_quotaset_update(tenant1_ioctx, "quotaset_1_4_1", NULL, uint64_t(8) << 40, uint64_t(2) << 20));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(0, s5_list_quotasets_of_tenant(tenant1_ioctx, "tenant1", &quotaset_list));
	ASSERT_EQ(2, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	ASSERT_EQ(0, s5_quotasets_list(ioctx, &quotaset_list));
	ASSERT_EQ(4, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	//update quotaset 4, partially update with default value
	//only item 'iops' update, name is NULL, and bw is invalid '-1'
	ASSERT_EQ(0, s5_quotaset_update(tenant1_ioctx, "quotaset_1_4_1", NULL, uint64_t(2) << 20, -1));
	ASSERT_EQ(0, s5_list_quotasets_of_tenant(tenant1_ioctx, "tenant1", &quotaset_list));
	ASSERT_EQ(2, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	ASSERT_EQ(0, s5_quotasets_list(ioctx, &quotaset_list));
	ASSERT_EQ(4, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	//try delete tenant 1
	//first with ioctx invalid
	ASSERT_EQ(-EBUSY, s5_delete_tenant(ioctx, "tenant1"));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EBUSY, s5_delete_tenant(ioctx, "tenant1"));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//delete quotaset 4
	ASSERT_EQ(0, s5_quotaset_delete(tenant1_ioctx, "quotaset_1_4_1"));
	ASSERT_EQ(0, s5_list_quotasets_of_tenant(tenant1_ioctx, "tenant1", &quotaset_list));
	ASSERT_EQ(1, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	ASSERT_EQ(0, s5_quotasets_list(ioctx, &quotaset_list));
	ASSERT_EQ(3, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	//delete quotaset 1
	ASSERT_EQ(0, s5_quotaset_delete(tenant1_ioctx, "quotaset_1_1"));
	ASSERT_EQ(0, s5_list_quotasets_of_tenant(tenant1_ioctx, "tenant1", &quotaset_list));
	ASSERT_EQ(0, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	ASSERT_EQ(0, s5_quotasets_list(ioctx, &quotaset_list));
	ASSERT_EQ(2, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	//delete quotaset 2_1
	ASSERT_EQ(0, s5_quotaset_delete(tenant2_ioctx, "quotaset_2_1"));
	ASSERT_EQ(0, s5_quotasets_list(ioctx, &quotaset_list));
	ASSERT_EQ(1, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	//delete quotaset 2_4
	ASSERT_EQ(0, s5_quotaset_delete(tenant2_ioctx, "quotaset_2_4"));
	ASSERT_EQ(0, s5_quotasets_list(ioctx, &quotaset_list));
	ASSERT_EQ(0, quotaset_list.num);
	print_quotaset_list(&quotaset_list);
	ASSERT_EQ(0, s5_quotasetlist_release(&quotaset_list));

	ASSERT_EQ(0, s5_delete_tenant(ioctx, "tenant1"));
	ASSERT_EQ(0, s5_delete_tenant(ioctx, "tenant2"));
	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
	ASSERT_EQ(0, s5_release_ioctx(&tenant2_ioctx));
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

	s5bd_list_t volume_list;

	//list volumes of cluster
	ASSERT_EQ(0, s5bd_list(admin_ioctx, &volume_list));
	ASSERT_EQ(0, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));

	//list volumes of tenant_1
	ASSERT_EQ(0, s5_list_volume_by_tenant(tenant1_ioctx, "tenant1", &volume_list));
	ASSERT_EQ(0, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));

	//create volume with no qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX));

	//create volume with no qutaset, with name conflict
	ASSERT_EQ(-EEXIST, s5_create_volume(tenant1_ioctx, "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//create volume with no qutaset, with value exceeds capacity of corresponding item tenant
	ASSERT_EQ(-EINVAL, s5_create_volume(tenant1_ioctx, "volume_2", uint64_t(20) << 40, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//list volumes of cluster
	ASSERT_EQ(0, s5bd_list(admin_ioctx, &volume_list));
	ASSERT_EQ(1, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));

	//list volumes of quotaset_1
	ASSERT_EQ(-EINVAL, s5bd_lists_of_quotaset(tenant1_ioctx, "tenant1", "quotaset1", &volume_list));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//list volumes of tenant_1
	ASSERT_EQ(0, s5_list_volume_by_tenant(tenant1_ioctx, "tenant1", &volume_list));
	ASSERT_EQ(1, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));

	//create quotaset 1
	ASSERT_EQ(0, s5_quotaset_create(tenant1_ioctx, "quotaset_1", uint64_t(10) << 20, uint64_t(1) << 20));

	//list volumes of quotaset_1
	ASSERT_EQ(0, s5bd_lists_of_quotaset(tenant1_ioctx, "tenant1", "quotaset_1", &volume_list));
	ASSERT_EQ(0, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));


	//create volume_2 with qutaset specified
	ASSERT_EQ(-EINVAL, s5_create_volume(tenant1_ioctx, "volume_2", uint64_t(20) << 30, uint64_t(2) << 20, uint64_t(1) << 10, "", 1, S5_RW_XX));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_2", uint64_t(20) << 30, uint64_t(2) << 20, uint64_t(1) << 10, "quotaset_1", 1, S5_RW_XX));

	//create volume_3 with qutaset specified, but volume iops exceeds quotaset capacity
	ASSERT_EQ(-EINVAL, s5_create_volume(tenant1_ioctx, "volume_3", uint64_t(20) << 30, uint64_t(20) << 20, uint64_t(1) << 10, "quotaset_1", 1, S5_RW_XX));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//create volume_3 with qutaset not exist
	ASSERT_EQ(-EINVAL, s5_create_volume(tenant1_ioctx, "volume_3", uint64_t(20) << 30, uint64_t(1) << 20, uint64_t(1) << 10, "quotaset_2", 1, S5_RW_XX));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//list volumes of cluster
	ASSERT_EQ(0, s5bd_list(admin_ioctx, &volume_list));
	ASSERT_EQ(2, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));

	//list volumes of quotaset_1
	ASSERT_EQ(-EINVAL, s5bd_lists_of_quotaset(tenant1_ioctx, "tenant1", "quotaset1", &volume_list));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//list volumes of tenant_1
	ASSERT_EQ(0, s5_list_volume_by_tenant(tenant1_ioctx, "tenant1", &volume_list));
	ASSERT_EQ(2, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));


	//delete tenant
	ASSERT_EQ(-EBUSY, s5_delete_tenant(admin_ioctx, "tenant1"));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//delete quotaset1
	ASSERT_EQ(-EBUSY, s5_quotaset_delete(tenant1_ioctx, "quotaset_1"));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	ASSERT_EQ(0, s5_rename_volume(tenant1_ioctx, "volume_2", "volume_2_2"));

	//list volumes of tenant_1
	ASSERT_EQ(0, s5_list_volume_by_tenant(tenant1_ioctx, "tenant1", &volume_list));
	ASSERT_EQ(2, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));

	//delete volume_2
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_2_2"));

	//list volumes of cluster
	ASSERT_EQ(0, s5bd_list(admin_ioctx, &volume_list));
	ASSERT_EQ(1, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));

	//list volumes of quotaset_1
	ASSERT_EQ(-EINVAL, s5bd_lists_of_quotaset(tenant1_ioctx, "tenant1", "quotaset1", &volume_list));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//list volumes of tenant_1
	ASSERT_EQ(0, s5_list_volume_by_tenant(tenant1_ioctx, "tenant1", &volume_list));
	ASSERT_EQ(1, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));

	//delete volume_3
	ASSERT_EQ(-EINVAL, s5_delete_volume(tenant1_ioctx, "volume_3"));	
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//delete volume_1
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_1"));

	ASSERT_EQ(0, s5bd_list(admin_ioctx, &volume_list));
	ASSERT_EQ(0, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));

	//delete quotaset1
	ASSERT_EQ(0, s5_quotaset_delete(tenant1_ioctx, "quotaset_1"));

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
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX));

	//open volume
	s5_volume_t volume_ctx;
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", NULL, &volume_ctx));

	//stat test
	s5_volume_info_t volume_info;
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx, &volume_info));
	print_volume_info(&volume_info);

	//delete volume_1
	ASSERT_EQ(-EBUSY, s5_delete_volume(tenant1_ioctx, "volume_1"));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//resize volume_1
	ASSERT_EQ(-EBUSY, s5_resize_volume(tenant1_ioctx, "volume_1", uint64_t(40) << 30));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//close volume
	ASSERT_EQ(0, s5_close_volume(&volume_ctx));

	//resize volume_1
	ASSERT_EQ(0, s5_resize_volume(tenant1_ioctx, "volume_1", uint64_t(40) << 30));

	//stat
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", NULL, &volume_ctx));
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx, &volume_info));
	print_volume_info(&volume_info);
	ASSERT_EQ(0, s5_close_volume(&volume_ctx));

	//delete volume_1
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_1"));

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

	//create quotaset 1
	ASSERT_EQ(0, s5_quotaset_create(tenant1_ioctx, "quotaset_1", uint64_t(40) << 20, uint64_t(10) << 20));

	//create volume with qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, "quotaset_1", 1, S5_RW_XX));

	//open volume
	s5_volume_t volume_ctx;
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", NULL, &volume_ctx));

	//stat test
	s5_volume_info_t volume_info;
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx, &volume_info));
	print_volume_info(&volume_info);

	//delete volume_1
	ASSERT_EQ(-EBUSY, s5_delete_volume(tenant1_ioctx, "volume_1"));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//resize volume_1
	ASSERT_EQ(-EBUSY, s5_resize_volume(tenant1_ioctx, "volume_1", uint64_t(40) << 30));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//close volume
	ASSERT_EQ(0, s5_close_volume(&volume_ctx));

	//resize volume_1
	ASSERT_EQ(0, s5_resize_volume(tenant1_ioctx, "volume_1", uint64_t(40) << 30));

	//stat
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", NULL, &volume_ctx));
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx, &volume_info));
	print_volume_info(&volume_info);
	ASSERT_EQ(0, s5_close_volume(&volume_ctx));

	//delete volume_1
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_1"));

	//delete quotaset_1
	ASSERT_EQ(0, s5_quotaset_delete(tenant1_ioctx, "quotaset_1"));

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

	//create quotaset 1
	ASSERT_EQ(0, s5_quotaset_create(tenant1_ioctx, "quotaset_1", uint64_t(40) << 20, uint64_t(10) << 20));

	//create volume with qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, "quotaset_1", 1, S5_RW_XX));

	//open volume
	s5_volume_t volume_ctx;
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", NULL, &volume_ctx));

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
	ASSERT_EQ(LBA_LENGTH, s5_aio_read_volume(volume_ctx, LBA_LENGTH, LBA_LENGTH, dst_buf, aio_cb, &aio_arg));

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
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_1"));

	//delete quotaset_1
	ASSERT_EQ(0, s5_quotaset_delete(tenant1_ioctx, "quotaset_1"));

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

	//create quotaset 1
	ASSERT_EQ(0, s5_quotaset_create(tenant1_ioctx, "quotaset_1", uint64_t(40) << 20, uint64_t(10) << 20));

	//create volume with qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, "quotaset_1", 1, S5_RW_XX));

	//open volume
	s5_volume_t volume_ctx;
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", NULL, &volume_ctx));

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
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_1"));

	//delete quotaset_1
	ASSERT_EQ(0, s5_quotaset_delete(tenant1_ioctx, "quotaset_1"));

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

	//create quotaset 1
	ASSERT_EQ(0, s5_quotaset_create(tenant1_ioctx, "quotaset_1", uint64_t(40) << 20, uint64_t(10) << 20));

	//create volume with qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, "quotaset_1", 1, S5_RW_XX));

	//open volume
	s5_volume_t volume_ctx;
	ASSERT_EQ(0, s5_open_volume_read_only(tenant1_ioctx, "volume_1", NULL, &volume_ctx));

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
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_1"));

	//delete quotaset_1
	ASSERT_EQ(0, s5_quotaset_delete(tenant1_ioctx, "quotaset_1"));

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

	//create quotaset 1
	ASSERT_EQ(0, s5_quotaset_create(tenant1_ioctx, "quotaset_1", uint64_t(40) << 20, uint64_t(10) << 20));

	//create volume with qutaset
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, "quotaset_1", 1, S5_RW_XX));
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_2", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX));

	//open volume
	s5_volume_t volume_ctx1, volume_ctx2;
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", NULL, &volume_ctx1));
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_2", NULL, &volume_ctx2));

	//stat test
	s5_volume_info_t volume_info;
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx1, &volume_info));
	print_volume_info(&volume_info);
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx2, &volume_info));
	print_volume_info(&volume_info);

	ASSERT_EQ(-EBUSY, s5_update_volume(tenant1_ioctx, "volume_1", "volume_1_2", uint64_t(4) << 20, uint64_t(1) << 20, uint64_t(40) << 10, 0));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EBUSY, s5_update_volume(tenant1_ioctx, "volume_2", "volume_2_2", uint64_t(4) << 20, uint64_t(1) << 20, uint64_t(40) << 10, 0));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//close volume
	ASSERT_EQ(0, s5_close_volume(&volume_ctx2));
	ASSERT_EQ(0, s5_close_volume(&volume_ctx1));

	ASSERT_EQ(0, s5_update_volume(tenant1_ioctx, "volume_1", "volume_1_2", -1, -1, -1, -1));
	ASSERT_EQ(0, s5_update_volume(tenant1_ioctx, "volume_2", "volume_2_2", -1, -1, -1, -1));

	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1_2", NULL, &volume_ctx1));
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx1, &volume_info));
	print_volume_info(&volume_info);
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_2_2", NULL, &volume_ctx2));
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx2, &volume_info));
	print_volume_info(&volume_info);

	//close volume
	ASSERT_EQ(0, s5_close_volume(&volume_ctx1));
	ASSERT_EQ(0, s5_close_volume(&volume_ctx2));

	ASSERT_EQ(0, s5_update_volume(tenant1_ioctx, "volume_1_2", NULL, -1, -1, -1, -1));
	ASSERT_EQ(0, s5_update_volume(tenant1_ioctx, "volume_2_2", NULL, -1, -1, -1, -1));

	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1_2", NULL, &volume_ctx1));
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx1, &volume_info));
	print_volume_info(&volume_info);
	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_2_2", NULL, &volume_ctx2));
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx2, &volume_info));
	print_volume_info(&volume_info);

	//close volume
	ASSERT_EQ(0, s5_close_volume(&volume_ctx1));
	ASSERT_EQ(0, s5_close_volume(&volume_ctx2));

	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "volume_2_2", NULL, -1, -1, -1, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_2_2", NULL, &volume_ctx2));
	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx2, &volume_info));
	print_volume_info(&volume_info);

	//close volume
	ASSERT_EQ(0, s5_close_volume(&volume_ctx2));


	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "volume_1_2", NULL, int64_t(10) << 40, -1, -1, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "volume_2_2", NULL, int64_t(10) << 40, -1, -1, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "volume_1_2", NULL, 10, -1, -1, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "volume_2_2", NULL, 10, -1, -1, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "volume_1_2", NULL, -1, int64_t(100) << 30, -1, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "volume_2_2", NULL, -1, int64_t(100) << 30, -1, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "volume_1_2", NULL, -1, -1, int64_t(100) << 40, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");
	ASSERT_EQ(-EINVAL, s5_update_volume(tenant1_ioctx, "volume_2_2", NULL, -1, -1, int64_t(100) << 40, -1));
	printf("*********************************************************************\n");
	printf("ERROR: %s\n", get_last_error_str());
	printf("*********************************************************************\n");

	//delete volume_1
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_2_2"));
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_1_2"));

	//delete quotaset_1
	ASSERT_EQ(0, s5_quotaset_delete(tenant1_ioctx, "quotaset_1"));

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

	//create quotaset 1
	ASSERT_EQ(0, s5_quotaset_create(tenant1_ioctx, "quotaset_1", uint64_t(40) << 20, uint64_t(10) << 20));

	//create test data
	ASSERT_EQ(0, system("dd if=/dev/urandom bs=4096 count=4096 of=/var/tmp/random.data"));

	//import 
	ASSERT_EQ(0, s5_import(tenant1_ioctx, "volume_1", "/var/tmp/random.data", uint64_t(10) << 20, uint64_t(1) << 20, "quotaset_1", 1, S5_RW_XX));

	//list volumes of cluster
	s5bd_list_t volume_list;
	ASSERT_EQ(0, s5bd_list(admin_ioctx, &volume_list));
	ASSERT_EQ(1, volume_list.num);
	print_volume_list(&volume_list);
	ASSERT_EQ(0, s5_release_volumelist(&volume_list));

	//export
	ASSERT_EQ(0, s5_export(tenant1_ioctx, "/var/tmp/import_test.data", "volume_1"));

	char src_f_md5[MD5_STR_LEN + 1] = {0};
	char dst_f_md5[MD5_STR_LEN + 1] = {0};

	ASSERT_EQ(0, compute_file_md5("/var/tmp/random.data", src_f_md5, MD5_STR_LEN + 1));
	ASSERT_EQ(0, compute_file_md5("/var/tmp/import_test.data", dst_f_md5, MD5_STR_LEN + 1));
	printf("md5 of src:%s\n", src_f_md5);
	printf("md5 of dst:%s\n", dst_f_md5);
	ASSERT_EQ(0, strcmp(dst_f_md5, src_f_md5));

	//delete volume_1
	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_1"));

	//delete quotaset_1
	ASSERT_EQ(0, s5_quotaset_delete(tenant1_ioctx, "quotaset_1"));

	//delete tenant
	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));

	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));

	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
}

//TEST(libs5bd, TestVolumeAIOClose)
//{
//	s5_ioctx_t admin_ioctx;
//	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &admin_ioctx));
//
//	//create tenant1
//	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30, 0));
//
//	//create tenant ioctx
//	s5_ioctx_t tenant1_ioctx;
//	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));
//
//	//create quotaset 1
//	ASSERT_EQ(0, s5_quotaset_create(tenant1_ioctx, "quotaset_1", uint64_t(40) << 20, uint64_t(10) << 20));
//
//	//create volume with qutaset
//	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_1", uint64_t(20) << 20, uint64_t(10) << 20, uint64_t(1) << 20, "quotaset_1", 1, S5_RW_XX));
//
//	//open volume
//	s5_volume_t volume_ctx;
//	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", NULL, &volume_ctx));
//
//	//stat test
//	s5_volume_info_t volume_info;
//	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx, &volume_info));
//	print_volume_info(&volume_info);
//
//	char src_buf[LBA_LENGTH] = {0};
//	strcpy(src_buf, "s5bd_aio_test: hello, s5bd!");
//	int ofs = 0;
//	sem_t q_sem;
//	sem_init(&q_sem, 0, 500);
//	while (ofs + LBA_LENGTH < volume_info.size)
//	{
//		while(sem_wait(&q_sem) != 0)
//		{
//			if(errno == EINTR) 
//				return;
//		}
//		simple_aio_cb_args_t* aio_arg = (simple_aio_cb_args_t*)malloc(sizeof(simple_aio_cb_args_t));
//		aio_arg->op_id = ofs / LBA_LENGTH;
//		aio_arg->io_len = LBA_LENGTH;
//		aio_arg->io_offset = ofs;
//		aio_arg->sem = &q_sem;
//		ASSERT_EQ(0, s5_aio_write_volume(volume_ctx, aio_arg->io_offset, aio_arg->io_len,
//			src_buf, aio_cb_free_arg, (void*)aio_arg));
//		ofs += LBA_LENGTH;
//	}
//
//	//close volume
//	printf("*****************  Close Volume  ***************************\n");
//	ASSERT_EQ(0, s5_close_volume(&volume_ctx));
//
//	//delete volume_1
//	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_1"));
//
//	//delete quotaset_1
//	ASSERT_EQ(0, s5_quotaset_delete(tenant1_ioctx, "quotaset_1"));
//
//	//delete tenant
//	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));
//
//	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
//	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
//}

//
//TEST(libs5bd, TestSnapOp)
//{
//	s5_ioctx_t admin_ioctx;
//	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &admin_ioctx));
//
//	//create tenant1
//	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30, 0));
//
//	//create tenant ioctx
//	s5_ioctx_t tenant1_ioctx;
//	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));
//
//	//create quotaset 1
//	ASSERT_EQ(0, s5_quotaset_create(tenant1_ioctx, "quotaset_1", uint64_t(40) << 20, uint64_t(10) << 20));
//
//	//create volume with no qutaset
//	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX));
//
//	//open volume
//	s5_volume_t volume_ctx;
//	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", NULL, &volume_ctx));
//	
//	s5bd_snapinfo_list_t snaplist;
//	ASSERT_EQ(0, s5bd_snap_list(volume_ctx, &snaplist));
//	print_snap_list(&snaplist);
//	s5bd_snapinfo_list_release(&snaplist);
//
//	char snap_name[MAX_NAME_LEN] = {0};
//	int off;
//	for (int i = 0; i < MAX_SNAP_CNT; i++)
//	{
//		off = snprintf(snap_name, MAX_NAME_LEN, "snap_%d", i);
//		snap_name[off] = 0;
//		ASSERT_EQ(0, s5bd_snap_create(volume_ctx, snap_name));
//	}
//	ASSERT_EQ(MAX_SNAP_CNT, s5bd_snap_list(volume_ctx, &snaplist));
//	print_snap_list(&snaplist);
//	s5bd_snapinfo_list_release(&snaplist);
//	
//	ASSERT_EQ(-ENODEV, s5bd_snap_create(volume_ctx, "snap_1111"));
//
//	//close volume
//	ASSERT_EQ(0, s5_close_volume(&volume_ctx));
//
//	//delete volume_1
//	ASSERT_EQ(-ENOTEMPTY, s5_delete_volume(tenant1_ioctx, "volume_1"));
//
//	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", NULL, &volume_ctx));
//
//	for (int i = 0; i < MAX_SNAP_CNT; i++)
//	{
//		off = snprintf(snap_name, MAX_NAME_LEN, "snap_%d", i);
//		snap_name[off] = 0;
//		ASSERT_EQ(0, s5bd_snap_remove(volume_ctx, snap_name));
//	}
//
//	ASSERT_EQ(0, s5_close_volume(&volume_ctx));
//
//	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_1"));
//
//	//delete quotaset_1
//	ASSERT_EQ(0, s5_quotaset_delete(tenant1_ioctx, "quotaset_1"));
//
//	//delete tenant
//	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));
//
//	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
//
//	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
//}
//
//TEST(libs5bd, TestSnapProtectOp)
//{
//	s5_ioctx_t admin_ioctx;
//	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &admin_ioctx));
//
//	//create tenant1
//	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30, 0));
//
//	//create tenant ioctx
//	s5_ioctx_t tenant1_ioctx;
//	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));
//
//	//create quotaset 1
//	ASSERT_EQ(0, s5_quotaset_create(tenant1_ioctx, "quotaset_1", uint64_t(40) << 20, uint64_t(10) << 20));
//
//	//create volume with no qutaset
//	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX));
//
//	//open volume
//	s5_volume_t volume_ctx;
//	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", NULL, &volume_ctx));
//
//	s5bd_snapinfo_list_t snaplist;
//	ASSERT_EQ(0, s5bd_snap_create(volume_ctx, "snap_0"));
//	ASSERT_EQ(0, s5bd_snap_list(volume_ctx, &snaplist));
//	print_snap_list(&snaplist);
//	s5bd_snapinfo_list_release(&snaplist);
//
//	//snap protect test
//	ASSERT_EQ(-EINVAL, s5bd_snap_unprotect(volume_ctx, "snap_0"));
//
//	ASSERT_EQ(0, s5bd_snap_protect(volume_ctx, "snap_0"));
//
//	ASSERT_EQ(-EBUSY, s5bd_snap_protect(volume_ctx, "snap_0"));
//
//	ASSERT_EQ(-EBUSY, s5bd_snap_remove(volume_ctx, "snap_0"));
//
//	ASSERT_EQ(0, s5bd_snap_unprotect(volume_ctx, "snap_0"));
//	
//	ASSERT_EQ(0, s5bd_snap_remove(volume_ctx, "snap_0"));
//
//	//close volume
//	ASSERT_EQ(0, s5_close_volume(&volume_ctx));
//
//	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_1"));
//
//	//delete quotaset_1
//	ASSERT_EQ(0, s5_quotaset_delete(tenant1_ioctx, "quotaset_1"));
//
//	//delete tenant
//	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));
//
//	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
//
//	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
//}
//
//TEST(libs5bd, TestVolumeClone)
//{
//	s5_ioctx_t admin_ioctx;
//	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &admin_ioctx));
//
//	//create tenant1
//	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 30, uint64_t(40) << 30, 0));
//
//	//create tenant ioctx
//	s5_ioctx_t tenant1_ioctx;
//	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));
//
//	//create quotaset 1
//	ASSERT_EQ(0, s5_quotaset_create(tenant1_ioctx, "quotaset_1", uint64_t(40) << 20, uint64_t(10) << 20));
//
//	//create volume with no qutaset
//	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX));
//
//	//open volume
//	s5_volume_t volume_ctx1;
//	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", NULL, &volume_ctx1));
//
//	char data_buf[4096] = {0};
//	snprintf(data_buf, 4096, "%s", "Test data in parent snap of volume 'volume_1' in quotaset 'quotaset_1' of tenant 'tenant1'");
//	ASSERT_EQ(4096, s5_write_volume(volume_ctx1, 4 << 10, 4096, data_buf));
//
//	s5bd_snapinfo_list_t snaplist;
//	ASSERT_EQ(0, s5bd_snap_create(volume_ctx1, "snap_0"));
//	ASSERT_EQ(0, s5bd_snap_list(volume_ctx1, &snaplist));
//	print_snap_list(&snaplist);
//	s5bd_snapinfo_list_release(&snaplist);
//
//	//clone
//	ASSERT_EQ(-EINVAL, s5bd_clone(tenant1_ioctx, "volume_1", "snap_0", "volume_2", uint64_t(40) << 30, uint64_t(10) << 30, NULL, 1, S5_RW_XX));
//	
//	//snap protect test
//	ASSERT_EQ(0, s5bd_snap_protect(volume_ctx1, "snap_0"));
//
//	ASSERT_EQ(0, s5bd_clone(tenant1_ioctx, "volume_1", "snap_0", "volume_2", uint64_t(40) << 30, uint64_t(10) << 30, NULL, 1, S5_RW_XX));
//
//	s5bd_list_t children_list;
//	s5_volume_t volume_ctx1_snap;
//	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", "snap_0", &volume_ctx1_snap));
//	ASSERT_EQ(0, s5bd_children_list(volume_ctx1_snap, &children_list));
//	ASSERT_EQ(1, children_list.num);
//	print_volume_list(&children_list);
//	ASSERT_EQ(0, s5_release_volumelist(&children_list));
//
//	ASSERT_EQ(0, s5bd_clone(tenant1_ioctx, "volume_1", "snap_0", "volume_3", uint64_t(40) << 30, uint64_t(10) << 30, NULL, 1, S5_RW_XX));
//
//	ASSERT_EQ(0, s5bd_children_list(volume_ctx1_snap, &children_list));
//	ASSERT_EQ(2, children_list.num);
//	print_volume_list(&children_list);
//	ASSERT_EQ(0, s5_release_volumelist(&children_list));
//
//	s5_volume_t volume_ctx2;
//	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_2", NULL, &volume_ctx2));
//
//	//get parent info
//	s5bd_parent_info_t parent_info;
//	ASSERT_EQ(0, s5bd_parent_stat(volume_ctx2, &parent_info));
//	print_parent_info(&parent_info);
//
//	char data_read[4096] = {0};
//	ASSERT_EQ(4096, s5_read_volume(volume_ctx2, 4 << 10, 4096, data_read));
//	printf("data_buf: %s\n", data_buf);
//	printf("data_read: %s\n", data_read);
//	ASSERT_EQ(0, memcmp(data_buf, data_read, 4096));
//
//	s5bd_list_t volume_list;
//
//	//list volumes of cluster
//	ASSERT_EQ(0, s5bd_list(admin_ioctx, &volume_list));
//	ASSERT_EQ(3, volume_list.num);
//	print_volume_list(&volume_list);
//	ASSERT_EQ(0, s5_release_volumelist(&volume_list));
//
//	ASSERT_EQ(0, s5_close_volume(&volume_ctx2));
//
//	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_2"));
//
//	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_3"));
//
//	ASSERT_EQ(0, s5_close_volume(&volume_ctx1_snap));
//
//	ASSERT_EQ(0, s5bd_snap_unprotect(volume_ctx1, "snap_0"));
//
//	ASSERT_EQ(0, s5bd_snap_remove(volume_ctx1, "snap_0"));
//
//	//close volume
//	ASSERT_EQ(0, s5_close_volume(&volume_ctx1));
//
//	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_1"));
//
//	//delete quotaset_1
//	ASSERT_EQ(0, s5_quotaset_delete(tenant1_ioctx, "quotaset_1"));
//
//	//delete tenant
//	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));
//
//	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
//
//	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
//}
//
//
//TEST(libs5bd, TestOverlapStat)
//{
//	s5_ioctx_t admin_ioctx;
//	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &admin_ioctx));
//
//	//create tenant1
//	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 30, uint64_t(40) << 30, 0));
//
//	//create tenant ioctx
//	s5_ioctx_t tenant1_ioctx;
//	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));
//
//	//create quotaset 1
//	ASSERT_EQ(0, s5_quotaset_create(tenant1_ioctx, "quotaset_1", uint64_t(40) << 20, uint64_t(10) << 20));
//
//	//create volume with no qutaset
//	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_1", uint64_t(20) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX));
//
//	//open volume
//	s5_volume_t volume_ctx1;
//	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", NULL, &volume_ctx1));
//
//	ASSERT_EQ(0, s5bd_snap_create(volume_ctx1, "snap_0"));
//
//	//snap protect test
//	ASSERT_EQ(0, s5bd_snap_protect(volume_ctx1, "snap_0"));
//
//	ASSERT_EQ(0, s5bd_clone(tenant1_ioctx, "volume_1", "snap_0", "volume_2", uint64_t(40) << 30, uint64_t(10) << 30, NULL, 1, S5_RW_XX));
//
//	s5_volume_t volume_ctx1_snap;
//	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", "snap_0", &volume_ctx1_snap));
//
//	s5_volume_t volume_ctx2;
//	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_2", NULL, &volume_ctx2));
//
//	s5_volume_info_t volume1_info, volume2_info;
//	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx1_snap, &volume1_info));
//	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx2, &volume2_info));
//	print_volume_info(&volume1_info);
//	print_volume_info(&volume2_info);
//
//	uint64_t overlap;
//	ASSERT_EQ(0, s5bd_get_overlap(volume_ctx2, &overlap));
//	printf("overlap after clone: %lu\n", overlap);
//
//	ASSERT_EQ(0, s5_close_volume(&volume_ctx2));
//
//	ASSERT_EQ(0, s5_update_volume(tenant1_ioctx, "volume_2", NULL, uint64_t(10) << 30, -1, -1, 0, NULL, S5_XX_XX));
//
//	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_2", NULL, &volume_ctx2));
//
//	ASSERT_EQ(0, s5bd_get_overlap(volume_ctx2, &overlap));
//	printf("overlap after resize: %lu\n", overlap);
//
//	ASSERT_EQ(0, s5_stat_opened_volume(volume_ctx2, &volume2_info));
//	print_volume_info(&volume2_info);
//
//	ASSERT_EQ(0, s5_close_volume(&volume_ctx2));
//
//	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_2"));
//
//	ASSERT_EQ(0, s5_close_volume(&volume_ctx1_snap));
//
//	ASSERT_EQ(0, s5bd_snap_unprotect(volume_ctx1, "snap_0"));
//
//	ASSERT_EQ(0, s5bd_snap_remove(volume_ctx1, "snap_0"));
//
//	//close volume
//	ASSERT_EQ(0, s5_close_volume(&volume_ctx1));
//
//	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_1"));
//
//	//delete quotaset_1
//	ASSERT_EQ(0, s5_quotaset_delete(tenant1_ioctx, "quotaset_1"));
//
//	//delete tenant
//	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));
//
//	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
//
//	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
//}
//
//
//TEST(libs5bd, TestFlatten)
//{
//	s5_ioctx_t admin_ioctx;
//	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &admin_ioctx));
//
//	//create tenant1
//	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 30, uint64_t(40) << 30, 0));
//
//	//create tenant ioctx
//	s5_ioctx_t tenant1_ioctx;
//	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));
//
//	//create quotaset 1
//	ASSERT_EQ(0, s5_quotaset_create(tenant1_ioctx, "quotaset_1", uint64_t(40) << 20, uint64_t(10) << 20));
//
//	//create volume with no qutaset
//	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_1", uint64_t(2) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX));
//
//	//open volume
//	s5_volume_t volume_ctx1;
//	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", NULL, &volume_ctx1));
//
//	char src_buf[LBA_LENGTH] = {0};
//	strcpy(src_buf, "s5_write_volume_test: hello, s5bd!");
//
//	ASSERT_EQ(LBA_LENGTH, s5_write_volume(volume_ctx1, LBA_LENGTH, LBA_LENGTH, src_buf));
//
//	ASSERT_EQ(LBA_LENGTH, s5_write_volume(volume_ctx1, LBA_LENGTH * 1025, LBA_LENGTH, src_buf));
//
//	ASSERT_EQ(0, s5bd_snap_create(volume_ctx1, "snap_0"));
//
//	//snap protect test
//	ASSERT_EQ(0, s5bd_snap_protect(volume_ctx1, "snap_0"));
//
//	ASSERT_EQ(0, s5bd_clone(tenant1_ioctx, "volume_1", "snap_0", "volume_2", uint64_t(40) << 30, uint64_t(10) << 30, NULL, 1, S5_RW_XX));
//
//	s5_volume_t volume_ctx1_snap;
//	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", "snap_0", &volume_ctx1_snap));
//
//	s5_volume_t volume_ctx2;
//	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_2", NULL, &volume_ctx2));
//
//	//get parent info
//	s5bd_parent_info_t parent_info;
//	ASSERT_EQ(0, s5bd_parent_stat(volume_ctx2, &parent_info));
//	print_parent_info(&parent_info);
//
//	uint64_t overlap;
//	ASSERT_EQ(0, s5bd_get_overlap(volume_ctx2, &overlap));
//	printf("overlap after clone: %lu\n", overlap);
//
//	ASSERT_EQ(0, s5bd_flatten(volume_ctx2));
//
//	ASSERT_EQ(-ENOENT, s5bd_parent_stat(volume_ctx2, &parent_info));
//	print_parent_info(&parent_info);
//
//	ASSERT_EQ(0, s5bd_get_overlap(volume_ctx2, &overlap));
//	printf("overlap after clone: %lu\n", overlap);
//
//	ASSERT_EQ(0, s5_close_volume(&volume_ctx2));
//
//	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_2"));
//
//	ASSERT_EQ(0, s5_close_volume(&volume_ctx1_snap));
//
//	ASSERT_EQ(0, s5bd_snap_unprotect(volume_ctx1, "snap_0"));
//
//	ASSERT_EQ(0, s5bd_snap_remove(volume_ctx1, "snap_0"));
//
//	//close volume
//	ASSERT_EQ(0, s5_close_volume(&volume_ctx1));
//
//	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_1"));
//
//	//delete quotaset_1
//	ASSERT_EQ(0, s5_quotaset_delete(tenant1_ioctx, "quotaset_1"));
//
//	//delete tenant
//	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));
//
//	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
//
//	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
//}
//
//TEST(libs5bd, TestRollback)
//{
//	s5_ioctx_t admin_ioctx;
//	ASSERT_EQ(0, s5_create_ioctx("admin","123456", CONF_PATH, &admin_ioctx));
//
//	//create tenant1
//	ASSERT_EQ(0, s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 30, uint64_t(40) << 30, 0));
//
//	//create tenant ioctx
//	s5_ioctx_t tenant1_ioctx;
//	ASSERT_EQ(0, s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx));
//	
//	//create volume with no qutaset
//	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_1", uint64_t(2) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX));
//
//	//open volume
//	s5_volume_t volume_ctx1;
//	ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", NULL, &volume_ctx1));
//
//	char src_buf[LBA_LENGTH] = {0};
//	strcpy(src_buf, "data in snap_0: s5_write_volume_test: hello, s5bd!");
//
//	ASSERT_EQ(LBA_LENGTH, s5_write_volume(volume_ctx1, 0, LBA_LENGTH, src_buf));
//
//	ASSERT_EQ(LBA_LENGTH, s5_write_volume(volume_ctx1, LBA_LENGTH * 1024, LBA_LENGTH, src_buf));
//
//	ASSERT_EQ(0, s5bd_snap_create(volume_ctx1, "snap_0"));
//
//	//memset(src_buf, 0, LBA_LENGTH);
//
//	//strcpy(src_buf, "data in snap_1: s5_write_volume_test: hello, s5bd!");
//
//	//ASSERT_EQ(LBA_LENGTH, s5_write_volume(volume_ctx1, LBA_LENGTH, LBA_LENGTH, src_buf));
//
//	//ASSERT_EQ(LBA_LENGTH, s5_write_volume(volume_ctx1, LBA_LENGTH * 1025, LBA_LENGTH, src_buf));
//
//	//ASSERT_EQ(0, s5bd_snap_create(volume_ctx1, "snap_1"));
//
//	memset(src_buf, 0, LBA_LENGTH);
//
//	strcpy(src_buf, "data in header: s5_write_volume_test: hello, s5bd!");
//
//	ASSERT_EQ(LBA_LENGTH, s5_write_volume(volume_ctx1, 0, LBA_LENGTH, src_buf));
//
//	ASSERT_EQ(LBA_LENGTH, s5_write_volume(volume_ctx1, LBA_LENGTH * 1024, LBA_LENGTH, src_buf));
//
//	char read_data[LBA_LENGTH] = {0};
//
//	ASSERT_EQ(LBA_LENGTH, s5_read_volume(volume_ctx1, 0, LBA_LENGTH, read_data));
//	printf("Data in header(offset: %d): %s\n", LBA_LENGTH, read_data);
//	memset(read_data, 0, LBA_LENGTH);
//	ASSERT_EQ(LBA_LENGTH, s5_read_volume(volume_ctx1, LBA_LENGTH * 1024, LBA_LENGTH, read_data));
//	printf("Data in header(offset: %d): %s\n", LBA_LENGTH * 1024, read_data);
//
//	//printf("rollback header to snap_1.\n");
//	//ASSERT_EQ(0, s5bd_snap_rollback(volume_ctx1, "snap_1"));
//
//	//memset(read_data, 0, LBA_LENGTH);
//	//ASSERT_EQ(LBA_LENGTH, s5_read_volume(volume_ctx1, LBA_LENGTH, LBA_LENGTH, read_data));
//	//printf("Data in header(offset: %d): %s\n", LBA_LENGTH, read_data);
//	//memset(read_data, 0, LBA_LENGTH);
//	//ASSERT_EQ(LBA_LENGTH, s5_read_volume(volume_ctx1, LBA_LENGTH * 1025, LBA_LENGTH, read_data));
//	//printf("Data in header(offset: %d): %s\n", LBA_LENGTH * 1025, read_data);
//
//	printf("rollback header to snap_0.\n");
//	ASSERT_EQ(0, s5bd_snap_rollback(volume_ctx1, "snap_0"));
//
//	memset(read_data, 0, LBA_LENGTH);
//	ASSERT_EQ(LBA_LENGTH, s5_read_volume(volume_ctx1, 0, LBA_LENGTH, read_data));
//	printf("Data in header(offset: %d): %s\n", LBA_LENGTH, read_data);
//	memset(read_data, 0, LBA_LENGTH);
//	ASSERT_EQ(LBA_LENGTH, s5_read_volume(volume_ctx1, LBA_LENGTH * 1024, LBA_LENGTH, read_data));
//	printf("Data in header(offset: %d): %s\n", LBA_LENGTH * 1024, read_data);
//
//	ASSERT_EQ(0, s5bd_snap_remove(volume_ctx1, "snap_0"));
//	//ASSERT_EQ(0, s5bd_snap_remove(volume_ctx1, "snap_1"));
//
//	//close volume
//	ASSERT_EQ(0, s5_close_volume(&volume_ctx1));
//
//	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_1"));
//
//	//delete tenant
//	ASSERT_EQ(0, s5_delete_tenant(admin_ioctx, "tenant1"));
//
//	ASSERT_EQ(0, s5_release_ioctx(&tenant1_ioctx));
//
//	ASSERT_EQ(0, s5_release_ioctx(&admin_ioctx));
//}


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
	ASSERT_EQ(0, s5_create_volume(tenant1_ioctx, "volume_1", uint64_t(2) << 30, uint64_t(10) << 20, uint64_t(1) << 20, NULL, 1, S5_RW_XX));

	//open volume
	s5_volume_t volume_ctx1;

	for (int i = 0; i < 20; i++)
	{
		printf("Volume open: %d\n", i);
		ASSERT_EQ(0, s5_open_volume(tenant1_ioctx, "volume_1", NULL, &volume_ctx1));
		//close volume

		printf("Volume close: %d\n", i);
		ASSERT_EQ(0, s5_close_volume(&volume_ctx1));
	}


	ASSERT_EQ(0, s5_delete_volume(tenant1_ioctx, "volume_1"));

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
