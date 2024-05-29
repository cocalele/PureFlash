/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
//
// Created by lele on 5/24/20.
//
#include "pf_main.h"
#include "pf_restful_api.h"
#include "pf_client_priv.h"
#include "pf_error_handler.h"

int PfErrorHandler::report_error_to_conductor(uint64_t rep_id, int sc,ErrorReportReply& reply)
{


	int retry_times = 5;
	for (int i = 0; i < retry_times; i++)
	{
		if(conductor_ip.empty()) {
			conductor_ip = get_master_conductor_ip(zk_ip.c_str(), cluster_name.c_str());
		}
		std::string query = format_string("http://%s:49180/s5c/?op=handle_error&rep_id=%lld&sc=%d",
		                                  conductor_ip.c_str(), rep_id, sc);
		void* reply_buf = pf_http_get(query, http_timeout, 1);
		if( reply_buf != NULL) {
			DeferCall _rel([reply_buf]() { free(reply_buf); });
			auto j = nlohmann::json::parse((char*)reply_buf);
			if(j["ret_code"].get<int>() != 0) {
				throw std::runtime_error(format_string("Failed %s, reason:%s", query.c_str(), j["reason"].get<std::string>().c_str()));
			}
			j.get_to<ErrorReportReply>(reply);
			return 0;
		}
		if (i < retry_times - 1)
		{
			conductor_ip.clear();
			S5LOG_ERROR("Failed query %s, will retry", query.c_str());
			::sleep(DEFAULT_HTTP_QUERY_INTERVAL);
		}
	}

	return -1;
}

//submit_error should work in asynchronous mode, though now it in synchronized mode
int PfErrorHandler::submit_error(IoSubTask* t, PfMessageStatus sc)
{
	int rc = this->event_queue->post_event(EVT_ASK_CONDUCTOR, sc, t);
	if(unlikely(rc)){
		S5LOG_ERROR("Failed to submit error, rc:%d", rc);
	}
	return rc;
}
int PfErrorHandler::submit_error(PfServerIocb* io, uint64_t rep_id, PfMessageStatus sc)
{
	assert(sc == PfMessageStatus::MSG_STATUS_NOT_PRIMARY);
	int rc = this->event_queue->post_event(EVT_ASK_CONDUCTOR, sc, (void*) rep_id, io);
	if (unlikely(rc)) {
		S5LOG_ERROR("Failed to submit error, rc:%d", rc);
	}
	return rc;

}

PfErrorHandler::PfErrorHandler()
{
	this->zk_ip = conf_get(app_context.conf, "zookeeper", "ip", "", TRUE);;
	cluster_name = conf_get(app_context.conf, "cluster", "name", "cluster1", FALSE);
	http_timeout = conf_get_int(app_context.conf, "client", "handle_error_timeout", 30, FALSE);
}

int PfErrorHandler::process_event(int event_type, int arg_i, void* arg_p, void* arg_q){
	switch(event_type){
	case EVT_ASK_CONDUCTOR:
		ErrorReportReply r;
		int rc = 0;
		if(sc == PfMessageStatus::MSG_STATUS_NOT_PRIMARY){
			uint64_t rep_id = uintptr_t(arg_p);
			rc = report_error_to_conductor(rep_id, sc, r);
		} else {
			rc = report_error_to_conductor(t->rep_id, sc, r);
		}

		if (rc) {
			S5LOG_ERROR("Failed report error to conductor, rc:%d", rc);
		}
		else {
			S5LOG_INFO("Error report get sc:%s, meta_ver:%d", PfMessageStatus2Str(r.action_code), r.meta_ver);
		}
		if (sc == PfMessageStatus::MSG_STATUS_NOT_PRIMARY) {
			PfServerIocb* iocb = (PfServerIocb*)arg_q;
			iocb->complete_status = r.action_code;
			iocb->complete_meta_ver = r.meta_ver;
			S5LOG_DEBUG("Get action:0x%lx from conductor for error handling of IO cid:%d", r.action_code, iocb->cmd_bd->cmd_bd->command_id);
			app_context.disps[iocb->disp_index]->sync_invoke([iocb]()->int {
				S5LOG_DEBUG("Reply IO after error handle"); 
				reply_io_to_client(iocb); 
				return 0; 
			});

		} else {
			IoSubTask* t = (IoSubTask * )arg_p;
			t->ops->complete_meta_ver(t, r.action_code, r.meta_ver);
		}
		

	}
}