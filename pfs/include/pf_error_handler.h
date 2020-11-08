//
// Created by lele on 5/24/20.
//

#ifndef PUREFLASH_PFERRORHANDLER_H
#define PUREFLASH_PFERRORHANDLER_H


#include <pf_message.h>
#include "pf_dispatcher.h"

class RestfulReply;
class PfErrorHandler {
public:
    int submit_error(IoSubTask* t, PfMessageStatus sc);
	PfErrorHandler();
	int report_error_to_conductor(uint64_t rep_id, int sc, RestfulReply& reply);

	std::string zk_ip;
	std::string cluster_name;
	int http_timeout;
	std::string conductor_ip;

};


#endif //PUREFLASH_PFERRORHANDLER_H
