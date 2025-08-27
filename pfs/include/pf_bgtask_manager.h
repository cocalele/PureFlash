/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
//
// Created by liu_l on 10/24/2020.
//

#ifndef PUREFLASH_PF_BGTASK_MANAGER_H
#define PUREFLASH_PF_BGTASK_MANAGER_H
#include <stdint.h>
#include <unordered_map>
#include <string>
#include <ctime>
#include <functional>
#include <memory>

#include "pf_threadpool.h"

enum TaskType {RECOVERY, SCRUB, GC};
enum TaskStatus{WAITING, RUNNING, SUCCEEDED, FAILED };
const char* TaskStatusToStr(TaskStatus s);

class RestfulReply;
class BackgroundTask;

typedef std::function<RestfulReply*(BackgroundTask*)> TaskExecutor;
class BackgroundTask{
public:
	int64_t id;
	TaskType type;
	std::string desc;
	TaskStatus status;
	std::time_t  start_time;
	std::time_t finish_time;
	RestfulReply* result;

	void* arg;
	TaskExecutor exec;
};
class BackgroundTaskManager {
public:
	long id_seed;
	std::unordered_map<int64_t, BackgroundTask*> task_map;
	ThreadPool recovery_thread_pool;
	BackgroundTaskManager():id_seed(0), recovery_thread_pool(1)
	{}

	BackgroundTask* initiate_task(TaskType type, std::string desc, TaskExecutor exe, void* arg);
	void commit_task(BackgroundTask*);
};

#endif //PUREFLASH_PF_BGTASK_MANAGER_H
