#ifndef pf_restful_api_h__
#define pf_restful_api_h__

#include <string>
#include <vector>
#include <stdint.h>

class ReplicaArg
{
public:
	int index;
	int store_id;
	std::string tray_uuid;
	std::string status;
	std::string rep_ports;
};
class ShardArg
{
public:
	int index;
	std::vector<ReplicaArg> replicas;
	int primary_rep_index;
	std::string status;
};
class PrepareVolumeArg
{
public:
	std::string op;
	std::string status;
	std::string volume_name;
	uint64_t volume_size;
	uint64_t volume_id;
	uint64_t meta_ver;
	int snap_seq;
	int shard_count;
	int rep_count;
	std::vector<ShardArg> shards;
};

class RestfulReply
{
public:
	std::string reason;
	std::string op;
	int ret_code;//code defined in interface RetCode
	const static int OK = 0;
	const static int INVALID_OP = 1;
	const static int INVALID_ARG = 2;
	const static int DUPLICATED_NODE = 3;
	const static int DB_ERROR = 4;
	const static int REMOTE_ERROR = 5;
	const static int ALREADY_DONE = 6;
	const static int INVALID_STATE = 7;

	RestfulReply();
	RestfulReply(std::string op, int ret_code=RestfulReply::OK, const char* reason="");



};

struct mg_connection;
struct http_message;

std::string get_http_param_as_string(const struct mg_str *http_str, const char *name, const char* def_val, bool mandatory);

void handle_prepare_volume(struct mg_connection *nc, struct http_message * hm);
#endif // pf_restful_api_h__
