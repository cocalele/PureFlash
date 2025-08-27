/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
//
// Created by liu_l on 10/25/2020.
//

#include <pf_log.h>
#include "pf_bgtask_manager.h"
#include "pf_restful_api.h"

const char* TaskStatusToStr(TaskStatus s)
{
#define C_NAME(x) case x: return #x;
	static __thread char buf[64];
	switch(s) {
		C_NAME(WAITING)
		C_NAME(RUNNING)
		C_NAME(SUCCEEDED)
		C_NAME(FAILED)
		default:
			snprintf(buf, sizeof(buf), "Unknown TaskStatus:%d", s);
			return buf;
	}
}

BackgroundTask* BackgroundTaskManager::initiate_task(TaskType type, std::string desc, TaskExecutor exe, void* arg){
	BackgroundTask *t = new BackgroundTask();
	t->id = ++id_seed;
	t->type = type;
	t->desc = desc;
	t->start_time = std::time(nullptr);
	t->arg = arg;
	t->exec = exe;
	t->status = TaskStatus::WAITING;

	S5LOG_INFO("Initiate background task id:%d", t->id);
	task_map[t->id] = t;
	return t;
}
void BackgroundTaskManager::commit_task(BackgroundTask* t)
{
	recovery_thread_pool.commit([t]()->int {
		t->status = TaskStatus::RUNNING;
		S5LOG_DEBUG("Begin background task id:%d, %s", t->id, t->desc.c_str());
		t->result = t->exec(t);
		S5LOG_DEBUG("Finish background task id:%d, ret_code:%d", t->id, t->result->ret_code);
		t->finish_time = std::time(nullptr);
		t->status = (t->result->ret_code == 0 ? TaskStatus::SUCCEEDED : TaskStatus::FAILED);
		return 0;
		});

}