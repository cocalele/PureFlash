#include "mongoose.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "pf_log.h"
#include "pf_utils.h"
#include "pf_restful_api.h"
#include "pf_volume.h"
#include "pf_main.h"
#include "pf_replica.h"

using nlohmann::json;
using namespace std;

RestfulReply::RestfulReply() : ret_code(0){}

RestfulReply::RestfulReply(std::string _op, int _ret_code, const char* _reason) : reason(_reason) , op(_op), ret_code(_ret_code){}


void from_json(const json& j, ReplicaArg& p) {
	j.at("index").get_to(p.index);
	j.at("store_id").get_to(p.store_id);
	j.at("tray_uuid").get_to(p.tray_uuid);
	j.at("status").get_to(p.status);

}

void from_json(const json& j, ShardArg& p) {
	j.at("index").get_to(p.index);
	j.at("replicas").get_to(p.replicas);
	j.at("primary_rep_index").get_to(p.primary_rep_index);
	j.at("status").get_to(p.status);

}

void from_json(const json& j, PrepareVolumeArg& p) {
	j.at("op").get_to(p.op);
	j.at("status").get_to(p.status);
	j.at("volume_name").get_to(p.volume_name);
	j.at("volume_size").get_to(p.volume_size);
	j.at("volume_id").get_to(p.volume_id);
	j.at("shard_count").get_to(p.shard_count);
	j.at("rep_count").get_to(p.rep_count);
	j.at("meta_ver").get_to(p.meta_ver);
	j.at("snap_seq").get_to(p.snap_seq);
	//p.shards = j["shards"].get<std::vector<ShardArg> >();
	j.at("shards").get_to(p.shards);
}

void to_json(json& j, const RestfulReply& r)
{
	j = json{ { "ret_code", r.ret_code },{ "reason", r.reason },{ "op", r.op } };

}


static PfVolume* convert_argument_to_volume(const PrepareVolumeArg& arg)
{
	Cleaner _c;
	PfVolume *vol = new PfVolume();
	_c.push_back([vol](){delete vol;});
	vol->id = arg.volume_id;
	strncpy(vol->name, arg.volume_name.c_str(), sizeof(vol->name));
	vol->size = arg.volume_size;
	vol->rep_count = arg.rep_count;
	vol->shard_count = arg.shard_count;
	vol->snap_seq = arg.snap_seq;
	vol->meta_ver = arg.meta_ver;
	vol->status = health_status_from_str(arg.status);

	for (int i = 0; i < arg.shard_count; i++)
	{
		PfShard* shard = new PfShard(); //will be release on ~PfVolume
		vol->shards.push_back(shard);
		shard->id = arg.volume_id | (arg.shards[i].index << 4);
		shard->shard_index = arg.shards[i].index;
		S5ASSERT(shard->shard_index == i);
		shard->primary_replica_index = arg.shards[i].primary_rep_index;
		shard->is_primary_node = FALSE;
		shard->rep_count = vol->rep_count;
		shard->snap_seq = vol->snap_seq;
		shard->status = health_status_from_str(arg.shards[i].status);
		for (int j = 0; j < arg.shards[i].replicas.size(); j++)
		{
			const ReplicaArg& rarg = arg.shards[i].replicas[j];
			S5ASSERT(rarg.index == j);
			PfReplica * r = new PfLocalReplica(); //will be released on ~PfShard
			shard->replicas[j] = r;
			r->rep_index = rarg.index;
			r->id = shard->id | r->rep_index;
			r->store_id = rarg.store_id;
			r->is_primary = (rarg.index == shard->primary_replica_index);
			r->is_local = (r->store_id == app_context.store_id);
			r->status = health_status_from_str(rarg.status);
			r->ssd_index = -1;
			if (r->is_local)
			{
				r->ssd_index = app_context.get_ssd_index(rarg.tray_uuid);
				if(r->ssd_index == -1)
				{
					throw std::runtime_error(format_string("SSD:%s not found", rarg.tray_uuid.c_str()));
				}
				shard->duty_rep_index = j;
				if (r->is_primary)
					shard->is_primary_node = TRUE;
			}
		}
	}
	_c.cancel_all();
	return vol;
}
//void to_json(json& j, const PrepareVolumeArg& p) {
//	j = json{ { "name", p.name },{ "address", p.address },{ "age", p.age } };
//}
/**
* for prepare volume, jconductor will send a json like:
*  {
*      "op":"prepare_volume",
*	   "status": "OK",
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
*               { "index":1, "replicas":[
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
	S5LOG_DEBUG("Receive prepare volume req===========\n%.*s\n============", (int)hm->body.len, hm->body.p);
	auto j = json::parse(hm->body.p, hm->body.p + hm->body.len);
	PrepareVolumeArg arg = j.get<PrepareVolumeArg>();
	PfVolume* vol = NULL;
	try {
		vol = convert_argument_to_volume(arg);
	}
	catch(std::exception& e) {
		RestfulReply r(arg.op + "_reply", RestfulReply::INVALID_ARG, e.what());
		json jr = r;
		string jstr = jr.dump();
		const char* cstr = jstr.c_str();
		mg_send_head(nc, 500, strlen(cstr), "Content-Type: text/plain");
		mg_printf(nc, "%s", cstr);
		return;
	}

	for(auto d : app_context.disps)
	{
		d->prepare_volume(vol);
	}
	{
	pthread_mutex_lock(&app_context.lock);
	DeferCall _c([]() {pthread_mutex_unlock(&app_context.lock);});
	app_context.opened_volumes[vol->id] = vol;
	}//these code in separate code block, so lock can be released quickly
	RestfulReply r(arg.op + "_reply");
	json jr = r;
	string jstr = jr.dump();
	const char* cstr = jstr.c_str();
	mg_send_head(nc, 200, strlen(cstr), "Content-Type: text/plain");
	mg_printf(nc, "%s", cstr);
}
