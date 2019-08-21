
#include "libs5bd.h"
#include "s5message.h"
#include "libs5manager.h"
#include "rt_info.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#define CONF_PATH "/etc/s5/s5.conf"
#define RT_INFO_LOG "/etc/s5/rt_info.log"

#define TEST_SCALE (4 << 20)
#define S5LOG_DUMP_INTERVAL 2
int should_stop = 0;
uint32_t replica_num = 1;
int32_t tray_id[MAX_REPLICA_NUM] = {-1, -1, -1};
const char* s5store_name[MAX_REPLICA_NUM] = {0};
void* rt_info_statistic(void* arg)
{
	pid_t pid = *((pid_t *)arg);	
	FILE* rt_log = fopen(RT_INFO_LOG, "w+");
	if(!rt_log)
	{
		printf("ERROR: Failed to open file %s.\n", RT_INFO_LOG);
		exit(-1);
	}
	fprintf(rt_log, "*************************************************************\n");
	fprintf(rt_log, "*************************************************************\n");
	fprintf(rt_log, "Runtime Info of process %d.\n", pid);
	fprintf(rt_log, "*************************************************************\n");
	fprintf(rt_log, "*************************************************************\n");
	char line_buffer[512];
	int ofs = 0;
	time_t cur_time;
	while(!should_stop)
	{
		time(&cur_time);
		ofs += snprintf(line_buffer, 512, "[%s", asctime(gmtime(&cur_time)));
		ofs--;
		ofs += snprintf(line_buffer + ofs, 512 - ofs, "] ");
		ofs += snprintf(line_buffer + ofs, 512 - ofs, "memory usage: %f KB   cpu usage: %f\n", get_pmem(pid), get_pcpu(pid));
		line_buffer[ofs] = 0;
		ofs = 0;
		fprintf(rt_log, "%s\n", line_buffer);
		sleep(S5LOG_DUMP_INTERVAL);
	}
	fclose(rt_log);
	return (void*)10;
}

void* test_memory_leak(void*)
{
	int run_idx = 0;
	int rc = 0;
	while(run_idx++ < TEST_SCALE)
	{
		printf("%d run start...\n", run_idx);
		s5_ioctx_t admin_ioctx;
		rc = s5_create_ioctx("admin","123456", CONF_PATH, &admin_ioctx);
		if(rc < 0)
		{
			printf("ERROR: %s.\n", get_last_error_str());
			break;
		}
		rc = s5_create_tenant(admin_ioctx, "tenant1", "123456", uint64_t(4) << 40, uint64_t(100) << 20, uint64_t(4) << 30);
		if(rc < 0)
		{
			printf("ERROR: %s.\n", get_last_error_str());
			break;
		}
		s5_ioctx_t tenant1_ioctx;
		rc = s5_create_ioctx("tenant1","123456", CONF_PATH, &tenant1_ioctx);
		if(rc < 0)
		{
			printf("ERROR: %s.\n", get_last_error_str());
			break;
		}
		
		//volume create and delete
		rc = s5_create_volume(tenant1_ioctx, "tenant1", "volume_1", 4 << 20, 1 << 20, 1 << 20, 1, replica_num, tray_id, s5store_name);
		if(rc < 0)
		{
			printf("ERROR: %s.\n", get_last_error_str());
			break;
		}
		rc = s5_delete_volume(tenant1_ioctx, "tenant1", "volume_1");
		if(rc < 0)
		{
			printf("ERROR: %s.\n", get_last_error_str());
			break;
		}

		//volume import and delete
		rc = system("dd if=/dev/urandom bs=4096 count=4096 of=/var/tmp/random.data");
		if(rc < 0)
		{
			printf("ERROR: Failed to generate data block for test.\n");
			break;
		}
		rc = s5_import_image(tenant1_ioctx, "tenant1", "volume_1", "/var/tmp/random.data", uint64_t(10) << 20, uint64_t(1) << 20, 1, replica_num, tray_id, s5store_name);
		if(rc < 0)
		{
			printf("ERROR: %s.\n", get_last_error_str());
			break;
		}
		rc = s5_delete_volume(tenant1_ioctx, "tenant1", "volume_1");
		if(rc < 0)
		{
			printf("ERROR: %s.\n", get_last_error_str());
			break;
		}

		//volume export and delete
		rc = s5_create_volume(tenant1_ioctx, "tenant1", "volume_1", 4 << 20, 1 << 20, 1 << 20, 1, replica_num, tray_id, s5store_name);
		if(rc < 0)
		{
			printf("ERROR: %s.\n", get_last_error_str());
			break;
		}
		rc = s5_export_image(tenant1_ioctx, "tenant1", "/var/tmp/import_test.data", "volume_1");
		if(rc < 0)
		{
			printf("ERROR: %s.\n", get_last_error_str());
			break;
		}
		rc = s5_delete_volume(tenant1_ioctx, "tenant1", "volume_1");
		if(rc < 0)
		{
			printf("ERROR: %s.\n", get_last_error_str());
			break;
		}

		rc = s5_delete_tenant(admin_ioctx, "tenant1");
		if(rc < 0)
		{
			printf("ERROR: %s.\n", get_last_error_str());
			break;
		}
		rc = s5_release_ioctx(&tenant1_ioctx);
		if(rc < 0)
		{
			printf("ERROR: %s.\n", get_last_error_str());
			break;
		}
		rc = s5_release_ioctx(&admin_ioctx);
		if(rc < 0)
		{
			printf("ERROR: %s.\n", get_last_error_str());
			break;
		}
	}
	should_stop = 1;
	return (void*)1;
}

int main()
{
	pid_t pid = getpid();
	pthread_t statistic_thread, taskprocess_thread;
	should_stop = 0;
	int ret = pthread_create(&taskprocess_thread, NULL, test_memory_leak, NULL);
	if(ret)
	{
		printf("ERROR: Failed to start memory leak test thread.\n");
		return -1;
	}
	printf("Memory leak test thread starts...\n");

	ret = pthread_create(&statistic_thread, NULL, rt_info_statistic, (void*)&pid);
	if(ret)
	{
		printf("ERROR: Failed to start runtime info statistic thread...");
		pthread_kill(taskprocess_thread, SIGKILL);
		void* tret;
		pthread_join(taskprocess_thread, &tret);
		return -1;
	}

	void* tret;
	pthread_join(statistic_thread, &tret);
	return 0;
}













