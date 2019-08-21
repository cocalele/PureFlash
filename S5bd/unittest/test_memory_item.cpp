#include <string>
#include <fstream>
#include <unistd.h>

#include <gtest/gtest.h>
#include <libs5bd.h>
#include <libs5bd.hpp>
#include "threadpool.h"
#include "atomic_op.h"
#include "memory_item.h"


int add(const int &a, const int &b)
{
	return a + b;
}

TEST(threadpool, add)
{
	EXPECT_EQ(2, add(1, 1));
	EXPECT_EQ(8, add(3, 5));
}

enum TestTaskType
{
	TEST_TYPE_SEND_TASK = 0,
	TEST_TYPE_RECV_TASK,
	TEST_TYPE_MAX
};

Threadpool* tp[TEST_TYPE_MAX];

uint32 get_core_num()
{
	int processor_num;
	std::ifstream iff;
	/// number of logical processors
	if(system(" cat /proc/cpuinfo | grep \"processor\" | wc -l>/tmp/processor_count.txt") == -1)
	{
		std::cerr << " number of logical processors FAILED" << std::endl;
	}
	iff.open("/tmp/processor_count.txt");
	iff >> processor_num;
	iff.close();
	std::remove("/tmp/processor_count.txt");

	return processor_num;
}

struct info
{
	int id;
	int num;
};

memory_item<info> *minfo;

void* send_thread_entry(void *node)
{
	int *n = (int*)node;
	for(int i = 0; i != *n; ++i)
	{
		info *mi = minfo->item_alloc();
		while(NULL == mi)
		{
			printf("warning-alloc info error in send_thread_entry.\n");
			usleep(1000);
			mi = minfo->item_alloc();
		}
		tp[TEST_TYPE_RECV_TASK]->feed_task(mi);
	}

	return NULL;
}

void* recv_thread_entry(void *node)
{
	info *mi = (info*)node;
	minfo->item_free(mi);

	return NULL;
}

int32 init_tpm()
{
	uint32 core_num = get_core_num();
	tp[TEST_TYPE_SEND_TASK] = new Threadpool(core_num);
	tp[TEST_TYPE_RECV_TASK] = new Threadpool(core_num);

	tp[TEST_TYPE_SEND_TASK]->set_entry_func(send_thread_entry);
	tp[TEST_TYPE_RECV_TASK]->set_entry_func(recv_thread_entry);

	minfo = new memory_item<info>;
	return 0;
}

int32 release_tpm()
{
	delete tp[TEST_TYPE_SEND_TASK];
	delete tp[TEST_TYPE_RECV_TASK];

	delete minfo;
	return 0;
}

TEST(memory_item, memory_alloc_free)
{
	init_tpm();
	int a[1000];
	for(int i = 0; i != 1000; ++i)
	{
		a[i] = i;
		tp[TEST_TYPE_SEND_TASK]->feed_task(a + i);
	}

	sleep(1);

	release_tpm();
}


int main(int argc, char **argv)
{
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
