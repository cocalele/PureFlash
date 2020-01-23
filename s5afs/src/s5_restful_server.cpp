//@encoding=GBK

#include <iostream>
#include <memory>
#include "mongoose.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "s5_log.h"
#include "s5_utils.h"
#include "s5_restful_api.h"

using namespace std;
using nlohmann::json;


static void handle_api(struct mg_connection *nc, int ev, void *p) {

	char opcode[64];
	struct http_message *hm = (struct http_message *) p;
	switch (ev) {
	case MG_EV_HTTP_REQUEST:
		mg_get_http_var(&hm->query_string, "op", opcode, sizeof(opcode));
		S5LOG_INFO("api op:%s", opcode);
		//mg_send_head(nc, 200, hm->message.len, "Content-Type: text/plain");
		//mg_printf(nc, "%.*s", (int)hm->message.len, hm->message.p);
		//mg_printf(nc, "%.*s", (int)hm->body.len, hm->body.p);
		handle_prepare_volume(nc, hm);
	}

}

static void handle_debug(struct mg_connection *nc, int ev, void *p) {
	char opcode[64];
	struct http_message *hm = (struct http_message *) p;
	switch (ev) {
	case MG_EV_HTTP_REQUEST:
		mg_get_http_var(&hm->query_string, "op", opcode, sizeof(opcode));
		S5LOG_INFO("debug op:%s", opcode);
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
		S5LOG_FATAL("Failed to bind on port:%d", port);
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
		S5LOG_ERROR("Internal error, buffer too small");
		throw std::overflow_error("Internal error, buffer too small");
	}
}

