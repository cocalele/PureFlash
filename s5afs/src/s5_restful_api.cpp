#include "mongoose.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "s5_log.h"
#include "s5_utils.h"
#include "s5_restful_api.h"
using nlohmann::json;
using namespace std;

void from_json(const json& j, replica_arg& p) {
	j.at("index").get_to(p.index);
	j.at("store_id").get_to(p.store_id);
	j.at("tray_uuid").get_to(p.tray_uuid);
}

void from_json(const json& j, shard_arg& p) {
	j.at("index").get_to(p.index);
	j.at("replicas").get_to(p.replicas);
}

void from_json(const json& j, prepare_volume_arg& p) {
	j.at("op").get_to(p.op);
	j.at("volume_name").get_to(p.volume_name);
	j.at("volume_size").get_to(p.volume_size);
	j.at("volume_id").get_to(p.volume_id);
	j.at("shard_count").get_to(p.shard_count);
	j.at("rep_count").get_to(p.rep_count);
	//p.shards = j["shards"].get<std::vector<shard_arg> >();
	j.at("shards").get_to(p.shards);
}



//void to_json(json& j, const prepare_volume_arg& p) {
//	j = json{ { "name", p.name },{ "address", p.address },{ "age", p.age } };
//}
/**
* for prepare volume, jconductor will send a json like:
*  {
*      "op":"prepare_volume",
*      "volume_name":"myvolname",
*      "volume_size":10000000,
*      "volume_id":12345678,
*      "shard_count":1,
*      "rep_count":3,
*      "shards":[
*               { "index":0, "replicas":[
* 					{ "index":0, "tray_uuid":"xxxxxxxx", "store_id":1},
* 					{ "index":1, "tray_uuid":"xxxxxxxx", "store_id":2},
* 					{ "index":2, "tray_uuid":"xxxxxxxx", "store_id":3}
*					]
* 			 },
*               { "index":0, "replicas":[
* 					{ "index":0, "tray_uuid" :"xxxxxxxx", "store_id":1},
* 					{ "index":1, "tray_uuid" :"xxxxxxxx", "store_id":2},
* 					{ "index":2, "tray_uuid" :"xxxxxxxx", "store_id":3}
*					]
* 			 }
* 			]
*   }
*/
void handle_prepare_volume(struct mg_connection *nc, struct http_message * hm)
{
	string vol_name = get_http_param_as_string(&hm->query_string, "name", NULL, true);
	auto j = json::parse(hm->body.p);
	auto arg = j.get<prepare_volume_arg>();

}
