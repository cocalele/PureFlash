//
// Created by lele on 5/24/20.
//
#include "pf_main.h"
#include "pf_restful_api.h"
#include "pf_error_handler.h"

//defined in pf_client_api.cpp
template<typename ReplyT>
int query_conductor(conf_file_t cfg, const std::string& query_str, ReplyT& reply);

int PfErrorHandler::submit_error(IoSubTask* t, PfMessageStatus sc)
{
	std::string query = format_string("op=handle_error&rep_id=%lld&sc=%d", t->rep->id, sc);
	RestfulReply r;
	int rc = query_conductor(app_context.conf, query, r);

    t->complete(PfMessageStatus::MSG_STATUS_REOPEN);
}
