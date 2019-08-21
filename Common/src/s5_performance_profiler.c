
#include "heap-profiler.h"
#include "profiler.h"
#include "s5_performance_profiler.h"

#define SA_NODEFER		0x40000000
#define SA_RESETHAND	0x80000000

#define SA_NOMASK		SA_NODEFER
#define SA_ONESHOT	SA_RESETHAND 

#define APP_PREFIX_LEN	64
#define PROFILE_LEN			512
char heap_profile_name_buf[PROFILE_LEN] = {};
char cpu_profile_name_buf[PROFILE_LEN] = {};

void sig_start_profile(int sig)
{
	printf("Receive signal 'SIGUSR1'\n");
#ifdef HEAP_PROFILE
	HeapProfilerStart(heap_profile_name_buf);
#endif
#ifdef CPU_PROFILE
	ProfilerStart(cpu_profile_name_buf);
#endif
}

void sig_stop_profile(int sig)
{
	printf("Receive signal 'SIGUSR2'\n");
#ifdef HEAP_PROFILE
	HeapProfilerStop();
#endif
#ifdef CPU_PROFILE
	ProfilerStop();
#endif
}

int s5_profiler_init(const char* prefix)
{
	int prefix_len = strlen(prefix);
	if (prefix_len >= APP_PREFIX_LEN)
	{
		S5LOG_ERROR("Prefix for profile file name is too long!\n");
		return -EINVAL;
	}
	sprintf(heap_profile_name_buf, "/tmp/%s_heap.pro", prefix);
	sprintf(cpu_profile_name_buf, "/tmp/%s_cpu.pro", prefix);

	struct sigaction act1;
	act1.sa_handler = sig_start_profile;
	sigemptyset(&act1.sa_mask);
	act1.sa_flags = SA_RESETHAND;
	int ret = sigaction(SIGUSR1, &act1, 0);
	if (ret < 0)
	{
		S5LOG_ERROR("Failed to register signal 'SIGUSR1' to op: heap_profile start!\n");
		return -errno;
	}

	struct sigaction act2;
	act2.sa_handler = sig_stop_profile;
	sigemptyset(&act2.sa_mask);
	act2.sa_flags = SA_RESETHAND;
	ret = sigaction(SIGUSR2, &act2, 0);
	if (ret < 0)
	{
		S5LOG_ERROR("Failed to register signal 'SIGUSR2' to op: heap_profile stop!");
		return -errno;
	}
	return 0;
}
