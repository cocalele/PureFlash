#ifndef s5_restful_api_h__
#define s5_restful_api_h__

#include <string>
#include <vector>
#include <stdint.h>

class replica_arg
{
public:
	int index;
	int store_id;
	std::string ssd_uuid;
};
class shard_arg
{
public:
	int index;
	std::vector<replica_arg> replicas;
};
class prepare_volume_arg
{
public:
	std::string op;
	std::string volume_name;
	uint64_t size;
	uint64_t id;
	int shard_count;
	int rep_count;
	std::vector<shard_arg> shards;
};

struct mg_connection;
struct http_message;

std::string get_http_param_as_string(const struct mg_str *http_str, const char *name, const char* def_val, bool mandatory);

void handle_prepare_volume(struct mg_connection *nc, struct http_message * hm);
#endif // s5_restful_api_h__
