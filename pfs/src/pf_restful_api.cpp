#include "mongoose.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <pf_bgtask_manager.h>
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

void from_json(const json& j, RestfulReply& p) {
	j.at("op").get_to(p.op);
	j.at("ret_code").get_to(p.ret_code);
	if(p.ret_code != 0)
		j.at("reason").get_to(p.reason);
}

void from_json(const json& j, ReplicaArg& p) {
	j.at("index").get_to(p.index);
	j.at("store_id").get_to(p.store_id);
	j.at("tray_uuid").get_to(p.tray_uuid);
	j.at("status").get_to(p.status);
	j.at("rep_ports").get_to(p.rep_ports);

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


void from_json(const json& j, GetSnapListReply& p) {
	from_json(j, *((RestfulReply*)&p));
	j.at("snap_list").get_to(p.snap_list);
}
void to_json(json& j, GetSnapListReply& r) {
	j = json{{ "ret_code", r.ret_code },{ "reason", r.reason },{ "op", r.op }, {"snap_list", r.snap_list}};
}

void to_json(json& j, const RestfulReply& r)
{
	j = json{ { "ret_code", r.ret_code },{ "reason", r.reason },{ "op", r.op } };

}

void to_json(json& j, const BackgroudTaskReply& r)
{
	j = json{ { "ret_code", r.ret_code },{ "reason", r.reason },{ "op", r.op },
		   { "task_id", r.task_id},
		   {"status", r.status},
		   {"progress", r.progress}};
}

void from_json(const json& j, ErrorReportReply& p) {
	from_json(j, *((RestfulReply*)&p));
	j.at("action_code").get_to(p.action_code);
	j.at("meta_ver").get_to(p.meta_ver);
}

template <typename R>
void send_reply_to_client(R& r, struct mg_connection *nc) {

	json jr = r;
	string jstr = jr.dump();
	const char* cstr = jstr.c_str();
	mg_send_head(nc, 200, strlen(cstr), "Content-Type: text/plain");
	mg_printf(nc, "%s", cstr);
}

static PfVolume* convert_argument_to_volume(const PrepareVolumeArg& arg)
{
	Cleaner _c;
	PfVolume *vol = new PfVolume();
	_c.push_back([vol](){vol->dec_ref();});
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
			bool is_local = (rarg.store_id == app_context.store_id);
			PfReplica * r;
			if(is_local)
				r = new PfLocalReplica(); //will be released on ~PfShard
			else
				r = new PfSyncRemoteReplica();
			shard->replicas[j] = r;
			r->rep_index = rarg.index;
			r->id = shard->id | r->rep_index;
			r->store_id = rarg.store_id;
			r->is_primary = (rarg.index == shard->primary_replica_index);
			r->is_local = is_local;
			r->status = health_status_from_str(rarg.status);
			r->ssd_index = -1;
			if (r->is_local) {
				r->ssd_index = app_context.get_ssd_index(rarg.tray_uuid);
				if(r->ssd_index == -1)
				{
					throw std::runtime_error(format_string("SSD:%s not found", rarg.tray_uuid.c_str()));
				}
				((PfLocalReplica*)r)->disk = app_context.trays[r->ssd_index];
				shard->duty_rep_index = j;
				if (r->is_primary)
					shard->is_primary_node = TRUE;
			}
			else {
				r->ssd_index = -1;
				PfReplicator *rp = app_context.replicators[vol->id%app_context.replicators.size()];
				((PfSyncRemoteReplica*)r)->replicator = rp;

				std::vector<std::string> ips = split_string(rarg.rep_ports, ',');
				while(ips.size() < 2)
					ips.push_back("");
				rp->sync_invoke([rp, &rarg, &ips](){
					auto pos = rp->conn_pool->peers.find(rarg.store_id);
					if(pos == rp->conn_pool->peers.end() || pos->second.conn == NULL) {
						rp->conn_pool->add_peer(rarg.store_id, ips[0], ips[1]);
						rp->conn_pool->connect_peer(rarg.store_id);
					}
					return 0;
				});

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
* 					{ "index":0, "tray_uuid":"xxxxxxxx", "store_id":1, "rep_ports":"ip1,ip2"},
* 					{ "index":1, "tray_uuid":"xxxxxxxx", "store_id":2, "rep_ports":"ip1,ip2"},
* 					{ "index":2, "tray_uuid":"xxxxxxxx", "store_id":3, "rep_ports":"ip1,ip2"}
*					]
* 			 },
*               { "index":1, "replicas":[
* 					{ "index":0, "tray_uuid" :"xxxxxxxx", "store_id":1, "rep_ports":"ip1,ip2"},
* 					{ "index":1, "tray_uuid" :"xxxxxxxx", "store_id":2, "rep_ports":"ip1,ip2"},
* 					{ "index":2, "tray_uuid" :"xxxxxxxx", "store_id":3, "rep_ports":"ip1,ip2"}
*					]
* 			 }
* 			]
*   }
*/
void handle_prepare_volume(struct mg_connection *nc, struct http_message * hm)
{
	string vol_name = get_http_param_as_string(&hm->query_string, "name", NULL, true);
	S5LOG_INFO("Receive prepare volume req===========\n%.*s\n============", (int)hm->body.len, hm->body.p);
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
	DeferCall _rel([vol]{vol->dec_ref();});
	int rc = 0;
	for(auto d : app_context.disps)
	{
		rc = d->sync_invoke([d, vol]()->int {return d->prepare_volume(vol);});

	}
	if(rc == 0)
	{
		S5LOG_INFO("Succeeded prepare volume:%s", vol->name);
		AutoMutexLock _l(&app_context.lock);
		if(app_context.opened_volumes.find(vol->id) == app_context.opened_volumes.end()){
			vol->add_ref();
			app_context.opened_volumes[vol->id] = vol;
		}
	}//these code in separate code block, so lock can be released quickly
	if(rc == -EALREADY)
	{
		S5LOG_ERROR("Volume already opened:%s, this is a bug", vol->name);
	}
	RestfulReply r(arg.op + "_reply");
	send_reply_to_client(r, nc);
}
void handle_set_snap_seq(struct mg_connection *nc, struct http_message * hm) {
	int64_t vol_id = get_http_param_as_int64(&hm->query_string, "volume_id", 0, true);
	int snap_seq = (int)get_http_param_as_int64(&hm->query_string, "snap_seq", 0, true);

	for(auto d : app_context.disps)
	{
		d->sync_invoke([d, vol_id, snap_seq]()->int {d->set_snap_seq(vol_id, snap_seq); return 0;});
	}
	RestfulReply r("set_snap_seq_reply");
	send_reply_to_client(r, nc);
}

void handle_set_meta_ver(struct mg_connection *nc, struct http_message * hm) {
	int64_t vol_id = get_http_param_as_int64(&hm->query_string, "volume_id", 0, true);
	int meta_ver = (int)get_http_param_as_int64(&hm->query_string, "meta_ver", 0, true);

	for(auto d : app_context.disps)
	{
		d->sync_invoke([d, vol_id, meta_ver]()->int {d->set_meta_ver(vol_id, meta_ver); return 0;});
	}
	RestfulReply r("set_meta_ver_reply");
	send_reply_to_client(r, nc);
}

void handle_delete_snapshot(struct mg_connection *nc, struct http_message * hm) {
	int64_t rep_id = get_http_param_as_int64(&hm->query_string, "shard_id", 0, true);
	int snap_seq = (int)get_http_param_as_int64(&hm->query_string, "snap_seq", 0, true);
	int prev_seq = (int)get_http_param_as_int64(&hm->query_string, "prev_snap_seq", 0, true);
	int next_seq = (int)get_http_param_as_int64(&hm->query_string, "next_snap_seq", 0, true);
	string ssd_uuid = get_http_param_as_string(&hm->query_string, "ssd_uuid", 0, true);
	int ssd_idx = app_context.get_ssd_index(ssd_uuid);
	RestfulReply r("delete_snapshot_reply");
	if(ssd_idx < 0) {
		r.ret_code = -ENOENT;
		r.reason = format_string("ssd:%s not found", ssd_uuid.c_str());
	} else {
		PfFlashStore *disk = app_context.trays[ssd_idx];
		disk->event_queue.sync_invoke([disk, rep_id,snap_seq, prev_seq, next_seq]()->int{
			disk->delete_snapshot(int64_to_shard_id(SHARD_ID(rep_id)),snap_seq, prev_seq, next_seq);
			return 0;
		});
	}

	send_reply_to_client(r, nc);
}

void handle_begin_recovery(struct mg_connection *nc, struct http_message * hm) {
	int64_t rep_id = get_http_param_as_int64(&hm->query_string, "replica_id", 0, true);

	RestfulReply r("begin_recovery_reply");
	for(auto d : app_context.disps)
	{
		int rc = d->sync_invoke([d, rep_id]()->int {
			replica_id_t  rid = int64_to_replica_id(rep_id);
			auto pos = d->opened_volumes.find(rid.to_volume_id().vol_id);
			if(pos == d->opened_volumes.end()) {
				return RestfulReply::INVALID_STATE;
			}
			PfVolume* v = pos->second;
			v->shards[rid.shard_index()]->replicas[rid.shard_index()]->status = HS_RECOVERYING;
			return 0;
		});
		if(rc == RestfulReply::INVALID_STATE) {
			r.ret_code = RestfulReply::INVALID_STATE;
			r.reason = "Volume not opened";
		}
	}
	send_reply_to_client(r, nc);
}

void handle_end_recovery(struct mg_connection *nc, struct http_message * hm) {
	int64_t rep_id = get_http_param_as_int64(&hm->query_string, "replica_id", 0, true);
	int64_t ok = get_http_param_as_int64(&hm->query_string, "ok", 0, true);
	RestfulReply r("end_recovery_reply");
	for(auto d : app_context.disps)
	{
		int rc = d->sync_invoke([d, rep_id, ok]()->int {
			replica_id_t  rid = int64_to_replica_id(rep_id);
			auto pos = d->opened_volumes.find(rid.to_volume_id().vol_id);
			if(pos == d->opened_volumes.end()) {
				return RestfulReply::INVALID_STATE;
			}
			PfVolume* v = pos->second;

			v->shards[rid.shard_index()]->replicas[rid.shard_index()]->status = (ok ? HS_OK : HS_ERROR);
			return 0;
		});
		if(rc == RestfulReply::INVALID_STATE) {
			r.ret_code = RestfulReply::INVALID_STATE;
			r.reason = "Volume not opened";
		}
	}
	send_reply_to_client(r, nc);
}

void handle_recovery_replica(struct mg_connection *nc, struct http_message * hm) {
	uint64_t rep_id = (uint64_t)get_http_param_as_int64(&hm->query_string, "replica_id", 0, true);
	int from = (int)get_http_param_as_int64(&hm->query_string, "from_store_id", 0, true);
	string from_ip = get_http_param_as_string(&hm->query_string, "from_store_mngt_ip", "", true);
	string ssd_uuid = get_http_param_as_string(&hm->query_string, "ssd_uuid", "", true);
	int64_t obj_size = get_http_param_as_int64(&hm->query_string, "object_size", 0, true);
	string from_ssd_uuid = get_http_param_as_string(&hm->query_string, "from_ssd_uuid", "", true);
	BackgroudTaskReply r;
	r.op="recovery_replica_reply";
	int i = app_context.get_ssd_index(ssd_uuid);
	PfFlashStore* disk = app_context.trays[i];
	BackgroundTask* t = app_context.bg_task_mgr.initiate_task(TaskType::RECOVERY,
								   format_string("recovery 0x%llx", rep_id),
								   [disk, rep_id, from_ip=std::move(from_ip), from, from_ssd_uuid=std::move(from_ssd_uuid), obj_size](void*)->RestfulReply*{
		int rc = disk->recovery_replica(rep_id, from_ip, from , from_ssd_uuid, obj_size);
		RestfulReply *r = new RestfulReply();
		if(rc != 0){
			r->ret_code = rc;
			r->reason = "Failed reocvery";
		}
		return r;
	}, NULL);
	r.task_id = t->id;
	r.status = TaskStatusToStr(t->status);
	send_reply_to_client(r, nc);
}
void handle_get_obj_count(struct mg_connection *nc, struct http_message * hm) {
	int cnt = 0;
	for(auto disk : app_context.trays) {
		cnt += disk->event_queue.sync_invoke([disk]()->int{
			return disk->free_obj_queue.space();
		});
	}
	mg_send_head(nc, 200, 16, "Content-Type: text/plain");
	mg_printf(nc, "%-16d", cnt);
}

void handle_clean_disk(struct mg_connection *nc, struct http_message * hm) {
	string ssd_uuid = get_http_param_as_string(&hm->query_string, "ssd_uuid", "", true);
	int i = app_context.get_ssd_index(ssd_uuid);
	PfFlashStore* disk = app_context.trays[i];
	S5LOG_WARN("Clean disk:%s", disk->tray_name);
	disk->sync_invoke([disk]()->int{
		for(auto it = disk->obj_lmt.begin();it!=disk->obj_lmt.end();++it) {
			lmt_key k= it->first;
			lmt_entry *head = it->second;
			while (head) {
				lmt_entry *p = head;
				head = head->prev_snap;
				disk->delete_obj(&k, p);
			}
			return 0;
		}
		return 0;
	});
	mg_send_head(nc, 200, 2, "Content-Type: text/plain");
	mg_printf(nc, "OK");
}


void handle_get_snap_list(struct mg_connection *nc, struct http_message * hm) {
	uint64_t vol_id = (uint64_t)get_http_param_as_int64(&hm->query_string, "volume_id", 0, true);
	uint64_t offset = (uint64_t)get_http_param_as_int64(&hm->query_string, "offset", 0, true);
	string ssd_uuid = get_http_param_as_string(&hm->query_string, "ssd_uuid", "", true);
	int i = app_context.get_ssd_index(ssd_uuid);
	GetSnapListReply reply;
	reply.op = "get_snap_list_reply";
	PfFlashStore* disk = app_context.trays[i];
	int rc = disk->sync_invoke([disk, vol_id, offset, &reply]()->int{
		return disk->get_snap_list(volume_id_t(vol_id), offset, reply.snap_list);
	});
	reply.ret_code = rc;
	send_reply_to_client(reply, nc);
}

void handle_delete_replica(struct mg_connection *nc, struct http_message * hm) {
	uint64_t rep_id = (uint64_t)get_http_param_as_int64(&hm->query_string, "replica_id", 0, true);
	string ssd_uuid = get_http_param_as_string(&hm->query_string, "ssd_uuid", "", true);
	int i = app_context.get_ssd_index(ssd_uuid);
	RestfulReply reply("delete_replica_reply");
	PfFlashStore* disk = app_context.trays[i];
	int rc = disk->sync_invoke([disk, rep_id]()->int{
		return disk->delete_replica(replica_id_t(rep_id));
	});
	S5LOG_INFO("Delete replica 0x:%x from disk:%s, rc:%d", rep_id, disk->tray_name, rc);
	reply.ret_code = rc;
	send_reply_to_client(reply, nc);
}

void handle_query_task(struct mg_connection *nc, struct http_message * hm) {
	S5LOG_DEBUG("call in handle_query_task");
	uint64_t task_id = (uint64_t)get_http_param_as_int64(&hm->query_string, "task_id", 0, true);
	BackgroundTask *t = app_context.bg_task_mgr.task_map[task_id];
	BackgroudTaskReply r;
	r.op="query_task_reply";
	if(t==NULL){
		S5LOG_ERROR("No task id:%d", task_id);
		r.task_id = 0;
		r.ret_code = -ENOENT;
		r.reason = "No such task";
	} else {
		r.task_id = t->id;
		r.status = TaskStatusToStr(t->status);
	}
	send_reply_to_client(r, nc);
}