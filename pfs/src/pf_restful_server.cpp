/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
//@encoding=GBK

#include <iostream>
#include <memory>
#include "mongoose.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "pf_log.h"
#include "pf_utils.h"
#include "pf_restful_api.h"

using namespace std;
using nlohmann::json;


static void handle_api(struct mg_connection *nc, int ev, void *p) {

	char opcode[64];
	struct http_message *hm = (struct http_message *) p;
	switch (ev) {
	case MG_EV_HTTP_REQUEST:
		mg_get_http_var(&hm->query_string, "op", opcode, sizeof(opcode));
		S5LOG_INFO("api op:%s, %.*s", opcode, hm->query_string.len, hm->query_string.p);
		//mg_send_head(nc, 200, hm->message.len, "Content-Type: text/plain");
		//mg_printf(nc, "%.*s", (int)hm->message.len, hm->message.p);
		//mg_printf(nc, "%.*s", (int)hm->body.len, hm->body.p);
		try {
			if (strcmp(opcode, "prepare_volume") == 0)
				handle_prepare_volume(nc, hm);
			else if (strcmp(opcode, "set_meta_ver") == 0)
				handle_set_meta_ver(nc, hm);
			else if (strcmp(opcode, "set_snap_seq") == 0)
				handle_set_snap_seq(nc, hm);
			else if (strcmp(opcode, "delete_snapshot") == 0)
				handle_delete_snapshot(nc, hm);
			else if (strcmp(opcode, "begin_recovery") == 0)
				handle_begin_recovery(nc, hm);
			else if (strcmp(opcode, "end_recovery") == 0)
				handle_end_recovery(nc, hm);
			else if (strcmp(opcode, "recovery_replica") == 0)
				handle_recovery_replica(nc, hm);
			else if (strcmp(opcode, "get_snap_list") == 0)
				handle_get_snap_list(nc, hm);
			else if (strcmp(opcode, "delete_replica") == 0)
				handle_delete_replica(nc, hm);
			else if (strcmp(opcode, "query_task") == 0)
				handle_query_task(nc, hm);
			else if (strcmp(opcode, "calculate_replica_md5") == 0)
				handle_cal_replica_md5(nc, hm);
			else if (strcmp(opcode, "calculate_object_md5") == 0)
				handle_cal_object_md5(nc, hm);
			else if (strcmp(opcode, "prepare_shards") == 0)
				handle_prepare_shards(nc, hm);
			else {
				S5LOG_ERROR("Unknown op:%s", opcode);
				string cstr = format_string("Unknown op:%s", opcode);
				mg_send_head(nc, 500, cstr.length(), "Content-Type: text/plain");
				mg_printf(nc, "%s", cstr.c_str());
			}
		}
		catch(std::exception& e) {
			S5LOG_ERROR("Error during handle op:%s, exception:%s", opcode, e.what());
			string cstr = format_string("Error during handle op:%s, exception:%s", opcode, e.what());
			mg_send_head(nc, 500, cstr.length(), "Content-Type: text/plain");
			mg_printf(nc, "%s", cstr.c_str());
		}

	}

}

static void handle_debug(struct mg_connection *nc, int ev, void *p) {
	char opcode[64];
	struct http_message *hm = (struct http_message *) p;
	switch (ev) {
	case MG_EV_HTTP_REQUEST:
		mg_get_http_var(&hm->query_string, "op", opcode, sizeof(opcode));
		S5LOG_INFO("debug op:%s", opcode);
		if(strcmp(opcode, "get_obj_count") == 0)
			handle_get_obj_count(nc, hm);
		else if(strcmp(opcode, "clean_disk") == 0){
			handle_clean_disk(nc, hm);
		}
		else if (strcmp(opcode, "perf") == 0)
			handle_perf_stat(nc, hm);
		else if (strcmp(opcode, "disp_io") == 0)
			handle_disp_io_stat(nc, hm);
		else if (strcmp(opcode, "save_md") == 0) {
			handle_save_md_disk(nc, hm);
		}
		else if (strcmp(opcode, "stat_conn") == 0) {
			handle_stat_conn(nc, hm);//statistics connection
		}
		else
		{
			S5LOG_ERROR("Unknown debug op:%s", opcode);
			string cstr = format_string("Unknown debug op:%s", opcode);
			mg_send_head(nc, 500, cstr.length(), "Content-Type: text/plain");
			mg_printf(nc, "%s", cstr.c_str());
		}
	}

}

static void ev_handler(struct mg_connection *c, int ev, void *p) {
	if (ev == MG_EV_HTTP_REQUEST) {
		struct http_message *hm = (struct http_message *) p;

		S5LOG_INFO("query:%s", hm->query_string.p);
		// We have received an HTTP request. Parsed request is contained in `hm`.
		// Send HTTP reply to the client which shows full original request.
		mg_send_head(c, 200, hm->message.len, "Content-Type: text/plain");
		mg_printf(c, "%.*s", (int)hm->message.len, hm->message.p);
	}
}

int init_restful_server()
{
	struct mg_mgr mgr;
	struct mg_connection *c;
	const char* port = "49181";
	mg_mgr_init(&mgr, NULL);
	c = mg_bind(&mgr, port, ev_handler);
	if(c == NULL)
	{
		S5LOG_FATAL("Failed to bind on port:%s", port);
	}

	mg_register_http_endpoint(c, "/api", handle_api);
	mg_register_http_endpoint(c, "/debug", handle_debug);
	// Set up HTTP server parameters
	mg_set_protocol_http_websocket(c);
	S5LOG_INFO("Start restful server on port:%s", port);
	while (1)
		mg_mgr_poll(&mgr, 1000);

	return 0;
}

/**
 * NOTE: this function will throw exception if param is lack and mandatory is true
 */
std::string get_http_param_as_string(const struct mg_str *http_content, const char *name, const char* def_val, bool mandatory)
{
	char varbuf[256];

	int rc = mg_get_http_var(http_content, name, varbuf, sizeof(varbuf));
	if (rc > 0)
		return std::string(varbuf);
	else if(rc == 0)
	{
		if (mandatory)
			throw std::invalid_argument(format_string("parameter:%s is missing", name));
		else
			return std::string(def_val);
	}
	else
	{
		S5LOG_ERROR("Internal error, buffer too small for param:%s", name);
		throw std::overflow_error("Internal error, buffer too small");
	}
}

int64_t get_http_param_as_int64(const struct mg_str *http_content, const char *name, int64_t def_val, bool mandatory)
{
	char varbuf[256];
	char* endbuf;
	int rc = mg_get_http_var(http_content, name, varbuf, sizeof(varbuf));
	if (rc > 0) {
		int64_t v = strtoll(varbuf, &endbuf,10);
		if(*endbuf != '\0')
			throw std::invalid_argument(format_string("parameter:%s=%s is not a valid number", name, varbuf));
		return v;
	}
	else if(rc == 0)
	{
		if (mandatory)
			throw std::invalid_argument(format_string("parameter:%s is missing", name));
		else
			return def_val;
	}
	else
	{
		S5LOG_ERROR("Internal error, buffer too small to get param:%s", name);
		throw std::overflow_error("Internal error, buffer too small");
	}
	return 0;//never run here
}
