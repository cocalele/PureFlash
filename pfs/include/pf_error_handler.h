/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
//
// Created by lele on 5/24/20.
//

#ifndef PUREFLASH_PFERRORHANDLER_H
#define PUREFLASH_PFERRORHANDLER_H


#include <pf_message.h>
#include "pf_dispatcher.h"
#include "pf_restful_api.h"

class RestfulReply;
class PfErrorHandler : public PfEventThread {
public:
    int submit_error(IoSubTask* t, PfMessageStatus sc);
	//int submit_error(PfServerIocb* io, uint64_t rep_id, PfMessageStatus sc);
	PfErrorHandler();
	int report_error_to_conductor(uint64_t rep_id, int sc, ErrorReportReply& reply);

	virtual int process_event(int event_type, int arg_i, void* arg_p, void* arg_q) override;
	std::string zk_ip;
	std::string cluster_name;
	int http_timeout;
	std::string conductor_ip;

};


#endif //PUREFLASH_PFERRORHANDLER_H
