/*
 * Copyright (C) 2016 Liu Lele(liu_lele@126.com)
 *
 * This code is licensed under the GPL.
 */
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
#include "pf_scrub.h"
#include "pf_stat.h"
#include "pf_event_queue.h"
#include "pf_server.h"

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
	j.at("id").get_to(p.id);
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

void from_json(const json& j, DeleteVolumeArg& p) {
	j.at("op").get_to(p.op);
	j.at("volume_name").get_to(p.volume_name);
	j.at("volume_id").get_to(p.volume_id);
}

void from_json(const json& j, GetThreadStatsArg& p) {
	j.at("op").get_to(p.op);
}

void from_json(const nlohmann::json& j, GetThreadStatsReply& p) {
	from_json(j, *((RestfulReply*)&p));
	for (int i = 0; i < p.thread_stats.size(); i++) {
		j.at("name").get_to(p.thread_stats[i].name);
		j.at("tid").get_to(p.thread_stats[i].tid);
		j.at("busy").get_to(p.thread_stats[i].stats.busy_tsc);
		j.at("idle").get_to(p.thread_stats[i].stats.idle_tsc);
	}
}

void to_json(json& j, thread_stat& r) {
	j = json{{ "name", r.name },{ "tid", r.tid },{ "busy", r.stats.busy_tsc }, {"idle", r.stats.idle_tsc}};
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
void to_json(nlohmann::json& j, const CalcMd5Reply& r)
{
	j = json{{"ret_code", r.ret_code},
	         {"reason",   r.reason},
	         {"op",       r.op},
	         {"md5",      r.md5},
	};
}
void to_json(nlohmann::json& j, const PerfReply& r)
{
	j = json{ {"ret_code", r.ret_code},
			 {"reason",   r.reason},
			 {"op",       r.op},
			 {"perf_stat",      r.line},
	};
}
void to_json(nlohmann::json& j, const SnapshotMd5& m)
{
	j["snap_seq"] = m.snap_seq;
	j["md5"] = m.md5;
}
void to_json(nlohmann::json& j, const ObjectMd5Reply& r)
{
	to_json(j, (RestfulReply)r);
	j["rep_id"] = r.rep_id.val();
	j["offset_in_vol"]=r.offset_in_vol;
	j["snap_md5"] = r.snap_md5;
}

template <typename R>
void send_reply_to_client(R& r, struct mg_connection *nc) {

	json jr = r;
	string jstr = jr.dump();
	const char* cstr = jstr.c_str();
	mg_send_head(nc, r.ret_code == 0 ? 200 : 400, strlen(cstr), "Content-Type: text/plain");
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
		memset(shard->replicas, 0, sizeof(shard->replicas));
		vol->shards.push_back(shard);
		shard->id = arg.volume_id | (arg.shards[i].index << 4);
		shard->shard_index = arg.shards[i].index;
		S5ASSERT(shard->shard_index == i);
		shard->primary_replica_index = arg.shards[i].primary_rep_index;
		shard->is_primary_node = FALSE;
		shard->rep_count = vol->rep_count;
		shard->snap_seq = vol->snap_seq;
		shard->status = health_status_from_str(arg.shards[i].status);
		S5LOG_INFO("Convert to shard:%d with %d replicas", i, arg.shards[i].replicas.size());
		for (int j = 0; j < arg.shards[i].replicas.size(); j++)
		{
			if (app_context.shard_to_replicator) {
				// case1: primary shard is asigned to this store, alloc PfLocalReplica and PfSyncRemoteReplica
				// case2: primary shard is not asigned to this store but slave shard is asigned to this store, 
				// 		  only alloc PfLocalReplica
				// case3: no shard is asigned to this store, do noting
				if (app_context.store_id != arg.shards[i].replicas[shard->primary_replica_index].store_id && 
					app_context.store_id != arg.shards[i].replicas[j].store_id) {
					continue;
				}				
			}
			
			const ReplicaArg& rarg = arg.shards[i].replicas[j];
			bool is_local = (rarg.store_id == app_context.store_id);
			PfReplica * r;
			if(is_local)
				r = new PfLocalReplica(); //will be released on ~PfShard
			else
				r = new PfSyncRemoteReplica();
			shard->replicas[j] = r;
			r->rep_index = rarg.index;
			r->id = rarg.id;
			S5ASSERT(r->id != 0);
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
			} else {
				r->ssd_index = -1;
				PfReplicator *rp = NULL;
				if (app_context.shard_to_replicator) {
					rp = app_context.get_replicator();
				} else {
					rp = app_context.replicators[(vol->id>>24)%app_context.replicators.size()];
				}
				((PfSyncRemoteReplica*)r)->replicator = rp;

				std::vector<std::string> ips = split_string(rarg.rep_ports, ',');
				while(ips.size() < 2)
					ips.push_back("");
				rp->sync_invoke([rp, r, &rarg, &ips](){
					auto pos = rp->conn_pool->peers.find(rarg.store_id);
					if(pos == rp->conn_pool->peers.end() || pos->second.conn == NULL) {
						rp->conn_pool->add_peer(rarg.store_id, ips[0], ips[1]);
						if(r->status == HS_OK || r->status == HS_RECOVERYING)
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
	int rc = 0;
	for(auto d : app_context.disps)
	{
		PfVolume* vol = NULL;
		try {
			vol = convert_argument_to_volume(arg);
		}
		catch (std::exception& e) {
			RestfulReply r(arg.op + "_reply", RestfulReply::INVALID_ARG, e.what());
			json jr = r;
			string jstr = jr.dump();
			const char* cstr = jstr.c_str();
			mg_send_head(nc, 500, strlen(cstr), "Content-Type: text/plain");
			mg_printf(nc, "%s", cstr);
			return;
		}
		DeferCall _rel([vol] {vol->dec_ref(); });
		rc = d->sync_invoke([d, vol]()->int {return d->prepare_volume(vol);});
		assert(rc == 0);
	}

	{//begin a new code block, so app_context lock can release quickly
		PfVolume* vol = NULL;
		try {
			vol = convert_argument_to_volume(arg);
		}
		catch (std::exception& e) {
			RestfulReply r(arg.op + "_reply", RestfulReply::INVALID_ARG, e.what());
			json jr = r;
			string jstr = jr.dump();
			const char* cstr = jstr.c_str();
			mg_send_head(nc, 500, strlen(cstr), "Content-Type: text/plain");
			mg_printf(nc, "%s", cstr);
			return;
		}
		DeferCall _rel([vol] {vol->dec_ref(); });
		AutoMutexLock _l(&app_context.lock); //
		auto pos = app_context.opened_volumes.find(vol->id);
		if (pos == app_context.opened_volumes.end()) {
			vol->add_ref();
			app_context.opened_volumes[vol->id] = vol;
		}
		else {
			*pos->second = std::move(*vol);
		}
	}
	S5LOG_INFO("Succeeded prepare volume:%s", arg.volume_name.c_str());

	RestfulReply r(arg.op + "_reply");
	send_reply_to_client(r, nc);
}

void handle_delete_volume(struct mg_connection *nc, struct http_message * hm)
{
	S5LOG_INFO("Receive delete volume req===========\n%.*s\n============", (int)hm->body.len, hm->body.p);
	auto j = json::parse(hm->body.p, hm->body.p + hm->body.len);
	DeleteVolumeArg arg = j.get<DeleteVolumeArg>();
	int rc = 0;
	uint64_t vol_id = arg.volume_id;
	for(auto d : app_context.disps)
	{
		rc = d->sync_invoke([d, vol_id]()->int {return d->delete_volume(vol_id);});
		assert(rc == 0);
	}
	S5LOG_INFO("Succeeded delete volume:%s", arg.volume_name.c_str());

	RestfulReply r(arg.op + "_reply");
	send_reply_to_client(r, nc);
}

static void _thread_get_stats(void *arg)
{
	struct restful_get_stats_ctx *ctx = (struct restful_get_stats_ctx*)arg;
	struct PfEventThread *thread = get_current_thread();
	struct pf_thread_stats stats;

	if (0 == get_thread_stats(&stats)) {
		ctx->ctx.thread_stats.push_back( {thread->name, thread->tid, stats} );
	} else {
		sem_post(&ctx->sem);
		return;		
	}
	ctx->ctx.next_thread_id++;

	// collect done
	if (ctx->ctx.next_thread_id == ctx->ctx.num_threads) {
		for (int i = 0; i < ctx->ctx.thread_stats.size(); i++) {
			S5LOG_INFO("thread stats: name: %s, tid: %llu, busy: %llu, idle: %llu",
				ctx->ctx.thread_stats[i].name.c_str(), ctx->ctx.thread_stats[i].tid,
				ctx->ctx.thread_stats[i].stats.busy_tsc,
				ctx->ctx.thread_stats[i].stats.idle_tsc);
		}
		sem_post(&ctx->sem);
	} else {
		// continue to get next thread stat
		((PfSpdkQueue *)ctx->ctx.threads[ctx->ctx.next_thread_id])->post_event_locked(EVT_GET_STAT, 0, ctx);
	}
}

void handle_get_thread_stats(struct mg_connection *nc, struct http_message * hm)
{
	S5LOG_INFO("Receive get thread stats req===========\n%.*s\n============", (int)hm->body.len, hm->body.p);
	auto j = json::parse(hm->body.p, hm->body.p + hm->body.len);
	GetThreadStatsReply reply;
	GetThreadStatsArg arg = j.get<GetThreadStatsArg>();
	auto ctx = new restful_get_stats_ctx();
	ctx->ctx.next_thread_id = 0;
	ctx->ctx.fn = _thread_get_stats;
	ctx->nc = nc;
	sem_init(&ctx->sem, 0, 0);
	for (int i = 0; i < app_context.disps.size(); i++) {
		ctx->ctx.threads.push_back(app_context.disps[i]->event_queue);
		ctx->ctx.num_threads++;
	}

	for (int i = 0; i < app_context.replicators.size(); i++) {
		ctx->ctx.threads.push_back(app_context.replicators[i]->event_queue);
		ctx->ctx.num_threads++;
	}

	for (int i = 0; i < app_context.trays.size(); i++) {
		ctx->ctx.threads.push_back(app_context.trays[i]->event_queue);
		ctx->ctx.num_threads++;
	}

	if (ctx->ctx.num_threads > 0) {
		((PfSpdkQueue *)ctx->ctx.threads[ctx->ctx.next_thread_id])->post_event_locked(EVT_GET_STAT, 0, ctx);
	}
	sem_wait(&ctx->sem);
	sem_destroy(&ctx->sem);
	reply.thread_stats = ctx->ctx.thread_stats;
	S5LOG_INFO("Succeeded get thread stats");
	reply.op = "get_thread_stats_reply";
	reply.ret_code = 0;
	//send_reply_to_client(reply, nc);
	auto jarray = nlohmann::json::array();
	for (int i = 0; i < reply.thread_stats.size(); i++) {
		nlohmann::json j;
		to_json(j, reply.thread_stats[i]);
		jarray.emplace_back(j);
	}
	string jstr = jarray.dump();
	const char* cstr = jstr.c_str();
	mg_send_head(nc, reply.ret_code == 0 ? 200 : 400, strlen(cstr), "Content-Type: text/plain");
	mg_printf(nc, "%s", cstr);
	delete ctx;
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
	int failed = 0;

	for(auto d : app_context.disps)
	{
		int rc = d->sync_invoke([d, vol_id, meta_ver]()->int { return d->set_meta_ver(vol_id, meta_ver); });
		if(rc != 0)
			failed = rc;
	}
	RestfulReply r("set_meta_ver_reply");
	r.ret_code = failed;
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
		disk->delete_snapshot(int64_to_shard_id(SHARD_ID(rep_id)),snap_seq, prev_seq, next_seq);
	}

	send_reply_to_client(r, nc);
}

void handle_begin_recovery(struct mg_connection *nc, struct http_message * hm) {
	int64_t rep_id = get_http_param_as_int64(&hm->query_string, "replica_id", 0, true);
	int64_t rep_index = get_http_param_as_int64(&hm->query_string, "replica_index", -1, false);

	RestfulReply r("begin_recovery_reply");
	for(auto d : app_context.disps)
	{
		int rc = d->sync_invoke([d, rep_id, rep_index]()->int {
			replica_id_t  rid = int64_to_replica_id(rep_id);
			auto pos = d->opened_volumes.find(rid.to_volume_id().vol_id);
			if(pos == d->opened_volumes.end()) {
				return RestfulReply::INVALID_STATE;
			}
			PfVolume* v = pos->second;
			int act_rep_idx = rid.replica_index();
			if(rep_index >= 0){
				if(rep_index >= MAX_REP_COUNT || v->shards[rid.shard_index()]->replicas[rep_index] == NULL){
					return RestfulReply::INTERNAL_ERROR;
				}
				act_rep_idx = (int)rep_index;
			}
			
			v->shards[rid.shard_index()]->replicas[act_rep_idx]->status = HS_RECOVERYING;
			return 0;
		});
		if(rc) {
			S5LOG_ERROR("Begin recovery failed for:%d", rc);
			r.ret_code = rc;
			r.reason = format_string("Begin recovery failed for:%d", rc);
		}
	}
	send_reply_to_client(r, nc);
}

void handle_end_recovery(struct mg_connection *nc, struct http_message * hm) {
	int64_t rep_id = get_http_param_as_int64(&hm->query_string, "replica_id", 0, true);
	int64_t rep_index = get_http_param_as_int64(&hm->query_string, "replica_index", -1, false);
	int64_t ok = get_http_param_as_int64(&hm->query_string, "ok", 0, true);
	RestfulReply r("end_recovery_reply");
	for(auto d : app_context.disps)
	{
		int rc = d->sync_invoke([d, rep_id, rep_index, ok]()->int {
			replica_id_t  rid = int64_to_replica_id(rep_id);
			auto pos = d->opened_volumes.find(rid.to_volume_id().vol_id);
			if(pos == d->opened_volumes.end()) {
				return RestfulReply::INVALID_STATE;
			}
			PfVolume* v = pos->second;
			int act_rep_idx = rid.replica_index();
			if (rep_index >= 0) {
				if (rep_index >= MAX_REP_COUNT || v->shards[rid.shard_index()]->replicas[rep_index] == NULL) {
					return RestfulReply::INTERNAL_ERROR;
				}
				act_rep_idx = (int)rep_index;
			}
			v->shards[rid.shard_index()]->replicas[act_rep_idx]->status = (ok ? HS_OK : HS_ERROR);
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
	int64_t meta_ver = get_http_param_as_int64(&hm->query_string, "meta_ver", 0, true);
	BackgroudTaskReply r;
	r.op="recovery_replica_reply";
	int i = app_context.get_ssd_index(ssd_uuid);
	if(i<0){
		S5LOG_ERROR("disk %s not found for handle_get_snap_list", ssd_uuid.c_str());
		r.ret_code = -ENOENT;
		r.reason = "disk not found";
		send_reply_to_client(r, nc);
		return;
	}
	PfFlashStore* disk = app_context.trays[i];
	BackgroundTask* t = app_context.bg_task_mgr.initiate_task(TaskType::RECOVERY, format_string("recovery 0x%llx", rep_id),
			[disk, rep_id, from_ip=std::move(from_ip), from, from_ssd_uuid=std::move(from_ssd_uuid), obj_size, meta_ver](BackgroundTask* t)->RestfulReply*{
		int rc = disk->recovery_replica(replica_id_t(rep_id), from_ip, from , from_ssd_uuid, obj_size, (uint16_t)meta_ver);
		RestfulReply *r = new RestfulReply();
		if(rc != 0){
			r->ret_code = rc;
			r->reason = "Failed recovery";
		}
		return r;
	}, NULL);
	r.task_id = t->id;
	r.status = TaskStatusToStr(t->status);
	app_context.bg_task_mgr.commit_task(t); //Task never start before this line, ensure above reference to t valid
	send_reply_to_client(r, nc);
}
void handle_get_obj_count(struct mg_connection *nc, struct http_message * hm) {
	int cnt = 0;
	for(auto disk : app_context.trays) {
		cnt += disk->event_queue->sync_invoke([disk]()->int{
			return disk->free_obj_queue.space();
		});
	}
	mg_send_head(nc, 200, 16, "Content-Type: text/plain");
	mg_printf(nc, "%-16d", cnt);
}

void handle_clean_disk(struct mg_connection *nc, struct http_message * hm) {
	string ssd_uuid = get_http_param_as_string(&hm->query_string, "ssd_uuid", "", true);
	int i = app_context.get_ssd_index(ssd_uuid);
	if(i<0){
		S5LOG_ERROR("disk %s not found for handle_clean_disk", ssd_uuid.c_str());
		mg_send_head(nc, 400, 5, "Content-Type: text/plain");
		mg_printf(nc, "ERROR");
		return;
	}
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

void handle_save_md_disk(struct mg_connection *nc, struct http_message * hm) {
	string ssd_uuid = get_http_param_as_string(&hm->query_string, "ssd_uuid", "", true);
	int i = app_context.get_ssd_index(ssd_uuid);
	if(i<0){
		S5LOG_ERROR("disk %s not found for handle_clean_disk", ssd_uuid.c_str());
		mg_send_head(nc, 400, 5, "Content-Type: text/plain");
		mg_printf(nc, "ERROR");
		return;
	}
	PfFlashStore* disk = app_context.trays[i];
	S5LOG_INFO("save metadata disk:%s", disk->tray_name);
	disk->sync_invoke([disk]()->int{
		disk->meta_data_compaction_trigger(COMPACT_TODO, false);
		return 0;
	});
	mg_send_head(nc, 200, 2, "Content-Type: text/plain");
	mg_printf(nc, "OK");
}

void handle_stat_conn(struct mg_connection* nc, struct http_message* hm) {
	std::string rst = format_string("established:%d closed:%d released:%d\n", PfConnection::total_count, PfConnection::closed_count, PfConnection::released_count);
	
	char verbose[16];
	int found = mg_get_http_var(&hm->query_string, "verbose", verbose, sizeof(verbose));
	if(found){
		std::lock_guard<std::mutex> _l(app_context.conn_map_lock);
		for(auto it = app_context.client_ip_conn_map.begin(); it != app_context.client_ip_conn_map.end(); ++it) {
			std::string line = format_string("%p %s, state:%d, ref_count:%d, volume:%s\n", it->second, it->second->connection_info.c_str(), 
				it->second->state, it->second->ref_count, it->second->srv_vol ? it->second->srv_vol->name : "<NA>");
			rst += line;
		}

		for(auto rp : app_context.replicators){
			for (auto it = rp->conn_pool->ip_id_map.begin(); it != rp->conn_pool->ip_id_map.end(); ++it) {
				std::string line = format_string("%p %s, state:%d, ref_count:%d, REPLICATING\n", it->second, it->second->connection_info.c_str(),
					it->second->state, it->second->ref_count);
				rst += line;
			}

		}
	}
	mg_send_head(nc, 200, rst.size(), "Content-Type: text/plain");
	mg_send(nc, rst.c_str(), (int)rst.length());
}

void handle_stat_iocb_pool(struct mg_connection* nc, struct http_message* hm) {

	std::string summary;
	for (int i = 0; i < app_context.disps.size(); i++)
	{
		std::string rst = format_string("dispatcher %d remain %d\n", i, app_context.disps[i]->iocb_pool.remain());
		summary += rst;
	}

	mg_send_head(nc, 200, summary.size(), "Content-Type: text/plain");
	mg_send(nc, summary.c_str(), (int)summary.length());
}

void handle_get_snap_list(struct mg_connection *nc, struct http_message * hm) {
	uint64_t vol_id = (uint64_t)get_http_param_as_int64(&hm->query_string, "volume_id", 0, true);
	uint64_t offset = (uint64_t)get_http_param_as_int64(&hm->query_string, "offset", 0, true);
	string ssd_uuid = get_http_param_as_string(&hm->query_string, "ssd_uuid", "", true);
	GetSnapListReply reply;
	int i = app_context.get_ssd_index(ssd_uuid);
	if(i<0){
		S5LOG_ERROR("disk %s not found for handle_get_snap_list", ssd_uuid.c_str());
		reply.ret_code = -ENOENT;
		reply.reason = "disk not found";
		send_reply_to_client(reply, nc);
		return;
	}
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
	RestfulReply reply("delete_replica_reply");
	int i = app_context.get_ssd_index(ssd_uuid);
	if(i<0){
		S5LOG_ERROR("disk %s not found for handle_delete_replica", ssd_uuid.c_str());
		reply.ret_code = -ENOENT;
		reply.reason = "disk not found";
		send_reply_to_client(reply, nc);
		return;
	}
	PfFlashStore* disk = app_context.trays[i];
	int rc = disk->sync_invoke([disk, rep_id]()->int{
		return disk->delete_replica(replica_id_t(rep_id));
	});
	S5LOG_INFO("Delete replica 0x:%x from disk:%s, rc:%d", rep_id, disk->tray_name, rc);
	reply.ret_code = rc;
	send_reply_to_client(reply, nc);
}

void handle_query_task(struct mg_connection *nc, struct http_message * hm) {
	//S5LOG_DEBUG("call in handle_query_task");
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

void handle_cal_replica_md5(struct mg_connection *nc, struct http_message * hm) {
	uint64_t rep_id = (uint64_t)get_http_param_as_int64(&hm->query_string, "replica_id", 0, true);
	string ssd_uuid = get_http_param_as_string(&hm->query_string, "ssd_uuid", "", true);
	CalcMd5Reply reply;
	int i = app_context.get_ssd_index(ssd_uuid);
	if(i<0){
		S5LOG_ERROR("disk %s not found for cal_replica_md5", ssd_uuid.c_str());
		reply.ret_code = -ENOENT;
		reply.reason = "disk not found";
		send_reply_to_client(reply, nc);
		return;
	}

	PfFlashStore* disk = app_context.trays[i];
	Scrub scrub;
	reply.md5 = scrub.cal_replica(disk, int64_to_replica_id(rep_id));
	send_reply_to_client(reply, nc);
}

void handle_cal_object_md5(struct mg_connection* nc, struct http_message* hm) {
	uint64_t _rep_id = (uint64_t)get_http_param_as_int64(&hm->query_string, "replica_id", 0, true);
	string ssd_uuid = get_http_param_as_string(&hm->query_string, "ssd_uuid", "", true);
	int64_t obj_idx = get_http_param_as_int64(&hm->query_string, "object_index", 0, true);
	
	replica_id_t rep_id(_rep_id);
	ObjectMd5Reply reply(rep_id);
	int i = app_context.get_ssd_index(ssd_uuid);
	if (i < 0) {
		S5LOG_ERROR("disk %s not found for cal_replica_md5", ssd_uuid.c_str());
		reply.ret_code = -ENOENT;
		reply.reason = "disk not found";
		send_reply_to_client(reply, nc);
		return;
	}

	PfFlashStore* disk = app_context.trays[i];
	int64_t logic_offset = rep_id.shard_index() * SHARD_SIZE + obj_idx *disk->head.objsize;
	reply.offset_in_vol = logic_offset;

	int rc = Scrub::cal_object(disk, rep_id, obj_idx, reply.snap_md5);
	if(rc){
		if(rc == -ENOENT){
			reply.ret_code = 0;//consider as success
		} else {
			S5LOG_ERROR("Scrub::cal_object fail, rc:%d", rc);
			reply.ret_code = rc;
		}
	}
	send_reply_to_client(reply, nc);
}

void handle_prepare_shards(struct mg_connection* nc, struct http_message* hm)
{
	string vol_name = get_http_param_as_string(&hm->query_string, "name", NULL, true);
	S5LOG_INFO("Receive reprepare_shards req===========\n%.*s\n============", (int)hm->body.len, hm->body.p);
	auto j = json::parse(hm->body.p, hm->body.p + hm->body.len);
	PrepareVolumeArg arg = j.get<PrepareVolumeArg>();
	{
		PfVolume* vol = NULL;
		try {
			vol = convert_argument_to_volume(arg);
		}
		catch (std::exception& e) {
			RestfulReply r(arg.op + "_reply", RestfulReply::INVALID_ARG, e.what());
			json jr = r;
			string jstr = jr.dump();
			const char* cstr = jstr.c_str();
			mg_send_head(nc, 500, strlen(cstr), "Content-Type: text/plain");
			mg_printf(nc, "%s", cstr);
			return;
		}
		DeferCall _rel([vol] {vol->dec_ref(); });

		AutoMutexLock _l(&app_context.lock);
		auto pos = app_context.opened_volumes.find(vol->id);
		if (pos == app_context.opened_volumes.end()) {
			RestfulReply r(arg.op + "_reply", RestfulReply::INVALID_STATE, "Volume not opened yet");
			send_reply_to_client(r, nc);
			return;
		}



		PfVolume* old_v = pos->second;
		if(old_v->meta_ver != vol->meta_ver && old_v->meta_ver != vol->meta_ver-1){
			S5LOG_ERROR("prepare shards failed, old meta_ver:%d new meta_ver:%d", old_v->meta_ver, vol->meta_ver);
			RestfulReply r(arg.op + "_reply", RestfulReply::INVALID_STATE, "meta_ver invalid");
			send_reply_to_client(r, nc);
			return;

		}
		old_v->meta_ver = vol->meta_ver;
		for (int i = 0; i < vol->shards.size(); i++)
		{
			PfShard* new_shard = vol->shards[i];
			PfShard* old_shard = old_v->shards[new_shard->shard_index];
			old_v->shards[new_shard->shard_index] = new_shard;
			vol->shards[i] = NULL;
			delete old_shard;
		}
	}

	int rc = 0;
	for (auto d : app_context.disps)
	{
		PfVolume* vol = NULL;
		try {
			vol = convert_argument_to_volume(arg);
		}
		catch (std::exception& e) {
			RestfulReply r(arg.op + "_reply", RestfulReply::INVALID_ARG, e.what());
			json jr = r;
			string jstr = jr.dump();
			const char* cstr = jstr.c_str();
			mg_send_head(nc, 500, strlen(cstr), "Content-Type: text/plain");
			mg_printf(nc, "%s", cstr);
			return;
		}
		DeferCall _rel([vol] {vol->dec_ref(); });
		rc = d->sync_invoke([d, vol]()->int {return d->prepare_shards(vol); });
		assert(rc == 0);
	}
	
	S5LOG_INFO("Succeeded reprepare_shards volume:%s", arg.volume_name.c_str());
	RestfulReply r(arg.op + "_reply");
	send_reply_to_client(r, nc);

}
void handle_perf_stat(struct mg_connection* nc, struct http_message* hm)
{
	int len=0;
	char buf[512];
	PerfReply reply;
	for (auto d : app_context.disps)
	{
		len += snprintf(buf+len, sizeof(buf)-len-1, "disp_%d:%d ", 
				d->disp_index, ((PfEventQueue *)(d->event_queue))->current_queue->count());
	
	}
	reply.line = std::move(std::string(buf, len));
	send_reply_to_client(reply, nc);
}

void handle_disp_io_stat(struct mg_connection* nc, struct http_message* hm)
{
	int len = 0;
	char buf[512];
	PerfReply reply;
	//DispatchStat total_stat={0};
	for (auto d : app_context.disps)
	{
		len += snprintf(buf + len, sizeof(buf) - len - 1, "disp_%d:wr_cnt:%ld rd_cnt:%ld rep_cnt:%ld wr_bytes:%ld rd_bytes:%ld rep_bytes:%ld\n ",
			d->disp_index,
			d->stat.wr_cnt, d->stat.rd_cnt, d->stat.rep_wr_cnt,
			d->stat.wr_bytes, d->stat.rd_bytes, d->stat.rep_wr_bytes);
	}
	reply.line = std::move(std::string(buf, len));
	send_reply_to_client(reply, nc);

}
void handle_disp_io_stat_reset(struct mg_connection* nc, struct http_message* hm)
{
	PerfReply reply;
	//DispatchStat total_stat={0};
	for (auto d : app_context.disps)
	{
		d->sync_invoke([d]()->int {
			d->stat.wr_cnt= d->stat.rd_cnt= d->stat.rep_wr_cnt=d->stat.wr_bytes= d->stat.rd_bytes= d->stat.rep_wr_bytes=0;
			return 0;
		});
	}
	send_reply_to_client(reply, nc);

}