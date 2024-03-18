#define _ISOC11_SOURCE //for aligned_alloc
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string>
#include <exception>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <vector>
#include "pf_aof.h"
#include "pf_utils.h"
#include "pf_log.h"
#include "pf_client_priv.h"

using namespace std;
using nlohmann::json;

extern const char* default_cfg_file; //defined in pf_client_api.cpp

#define AOF_MAGIC 0x466F4150 //PAoF
#define AOF_VER 0x00010000


//up align, the min number great or equal x, and aligned on 4K. e.g. 1 will align to 4096. 8192 will not change
#define UP_ALIGN_4K(x)  (((x) + 4095) & (~4095LL))

//down align, the max number less or equal x, and aligned on 4K, e.g. 1 will aligned to 0, 8192 will not change
#define DOWN_ALIGN_4K(x)  ((x)  & (~4095LL))

static ssize_t sync_io(PfClientVolume* v, void* buf, size_t count, off_t offset, int is_write);

#ifdef _DATA_DBG
#define LOCAL_DIR "/tmp"
#endif

class LsAofReply : public GeneralReply
{
public:
	std::vector<string> files;
};
void from_json(const json& j, LsAofReply& reply)
{
	j.at("op").get_to(reply.op);
	j.at("ret_code").get_to(reply.ret_code);
	if (reply.ret_code != 0)
		j.at("reason").get_to(reply.reason);
	else
		j.at("files").get_to(reply.files);
}

PfAof::PfAof(ssize_t append_buf_size)
{
	head_buf = aligned_alloc(4096, 4096);
	if (head_buf == NULL)
		throw std::runtime_error(format_string("Failed alloc head buf"));
	memset(head_buf, 0, 4096);
	append_buf = aligned_alloc(4096, append_buf_size);
	if(append_buf == NULL)
		throw std::runtime_error(format_string("Failed alloc append buf"));
	//read_buf = aligned_alloc(4096, 8192);
	//if (read_buf == NULL)
	//	throw std::runtime_error(format_string("Failed alloc read_buf"));

	this->append_buf_size = append_buf_size;
	this->append_tail = 0;
	this->volume = NULL;


	file_len = 0;
}
static std::map<std::string, PfAof*> opened_aof;
static std::mutex aof_lock;

PfAof::~PfAof()
{
	sync();
	S5LOG_DEBUG("close aof:%s len:%ld", volume->volume_name.c_str(), this->file_length());
	free(append_buf);
	{
		std::lock_guard<std::mutex> _l(aof_lock);
		opened_aof.erase(volume->volume_name);
	}
	if (volume != NULL) {
		volume->close();
		delete volume;
	}
#ifdef _DATA_DBG
	close(localfd);
	localfd=0;
#endif
}

const char* PfAof::path()
{
	return volume->volume_name.c_str();
}

struct PfClientVolume* _pf_open_volume(const char* volume_name, const char* cfg_filename, const char* snap_name,
	int lib_ver, bool is_aof); //defined in pf_client_api

int pf_create_aof(const char* volume_name, int rep_cnt, const char* cfg_filename, int lib_ver)
{
	int rc = 0;
	PfAofHead* head = (PfAofHead*) aligned_alloc(4096, 4096);
	if (head == NULL) {
		S5LOG_ERROR("Failed alloc head buf");
		return -ENOMEM;
	}
	DeferCall _head_r([head]() { free(head); });
	memset(head, 0, 4096);

	conf_file_t cfg = conf_open(cfg_filename);
	if (cfg == NULL)
	{
		S5LOG_ERROR("Failed open config file:%s", cfg_filename);
		return -errno;
	}
	DeferCall _cfg_r([cfg]() { conf_close(cfg); });

	char* esc_vol_name = curl_easy_escape(NULL, volume_name, 0);
	if (!esc_vol_name)
	{
		S5LOG_ERROR("Curl easy escape failed.");
		return -ENOMEM;
	}
	DeferCall _1([esc_vol_name]() { curl_free(esc_vol_name); });
	std::string query = format_string("op=create_aof&volume_name=%s&size=%ld&rep_cnt=%d",
		esc_vol_name, 128LL<<30, rep_cnt);
	GeneralReply r;
	rc = query_conductor(cfg, query, r);
	if (rc != 0)
		return rc;
	if (r.ret_code != 0)
		return r.ret_code;
	struct PfClientVolume* vol = _pf_open_volume(volume_name, cfg_filename, NULL, lib_ver, true);
	if(vol == NULL){
		S5LOG_ERROR("Failed to open volume:%s", volume_name);
		return -EIO;
	}
	DeferCall _v_r([vol]() {
		vol->close();
		delete vol; });
	head->magic = AOF_MAGIC;
	head->version = AOF_VER;
	rc = (int)sync_io(vol, head, 4096, 0, 1);
	if (rc <= 0) {
		S5LOG_ERROR("Failed write aof head, rc:%d", -errno);
		return rc;
	}
#ifdef _DATA_DBG
	char tmp[256];

	sprintf(tmp, "%s%.*s", LOCAL_DIR, (int)(strrchr(volume_name, '/') - volume_name), volume_name);

	rc = mkdir(tmp, 0777);
	if(rc != 0 && errno != EEXIST){
		S5LOG_ERROR("Failed create dir:%s, rc:%d", tmp, errno);
		return -errno;
	}
	sprintf(tmp, "%s%s", LOCAL_DIR, volume_name);
	rc = creat(tmp, 0666);
	if (rc < 0) {
		S5LOG_ERROR("Failed create file:%s, rc:%d", tmp, errno);
		return rc;
	}
	close(rc);
#endif
	return 0;
}

/**
 * @param flags: O_CREAT to create file on not existing
 */
PfAof* pf_open_aof(const char* volume_name, const char* snap_name, int flags, const char* cfg_filename, int lib_ver)
{
	int rc = 0;
	if (lib_ver != S5_LIB_VER) {
		S5LOG_ERROR("Caller lib version:%d mismatch lib:%d", lib_ver, S5_LIB_VER);
		return NULL;
	}
	std::lock_guard<std::mutex> _l(aof_lock);
	auto pos = opened_aof.find(volume_name);
	if (pos != opened_aof.end()) {
		S5LOG_INFO("AOF '%s' has already opened!", volume_name);
		PfAof *f = pos->second;
		f->add_ref();
		return f;
	}
	

	

	if(pf_aof_access(volume_name, cfg_filename) != 0) {
		if(flags & O_CREAT) {
			S5LOG_INFO("create aof:%s", volume_name);
			rc = pf_create_aof(volume_name, 1, cfg_filename, lib_ver);
			if(rc != 0){
				S5LOG_ERROR("Failed to create aof:%s rc:%d", volume_name, rc);
				return NULL;
			}
		}
		else {
			S5LOG_ERROR("Aof not exists:%s", volume_name);
			return NULL;
		}
	}
	S5LOG_INFO("Opening aof: %s@%s", volume_name,
		(snap_name == NULL || strlen(snap_name) == 0) ? "HEAD" : snap_name);
	try
	{
		Cleaner _clean;
		PfAof* aof = new PfAof;
		if (aof == NULL)
		{
			S5LOG_ERROR("alloca memory for volume failed!");
			return NULL;
		}
		_clean.push_back([aof]() { delete aof; });


			
		aof->volume = new PfClientVolume;
		if (aof->volume == NULL)
		{
			S5LOG_ERROR("alloca memory for volume failed!");
			return NULL;
		}

		//other calls
		aof->volume->volume_name = volume_name;
		if (cfg_filename == NULL)
			cfg_filename = default_cfg_file;
		aof->volume->cfg_file = cfg_filename;
		if (snap_name)
			aof->volume->snap_name = snap_name;

		rc = aof->volume->do_open(false, true);
		if (rc) {
			return NULL;
		}

		S5LOG_INFO("Succeeded open volume %s@%s(0x%lx), meta_ver=%d, io_depth=%d", aof->volume->volume_name.c_str(),
			aof->volume->snap_seq == -1 ? "HEAD" : aof->volume->snap_name.c_str(), aof->volume->volume_id, 
			aof->volume->meta_ver, aof->volume->io_depth);
		rc = aof->open(); //open aof
		if (rc) {
			return NULL;
		}
		
		opened_aof[volume_name] = aof;
		_clean.cancel_all();
		return aof;
	}
	catch (std::exception& e)
	{
		S5LOG_ERROR("Exception in open aof:%s", e.what());
	}
	return NULL;
}

int PfAof::open()
{
	int rc = 0;
	rc = (int)sync_io(this->volume, head_buf, 4096, 0, 0);
	if (rc <=0 || head->magic != AOF_MAGIC) {
		S5LOG_ERROR("Aof magic error, not a AoF file. rc:%d volume:%s", rc, volume->volume_name);
		return -EINVAL;
	}
	if (head->version != AOF_VER) {
		S5LOG_ERROR("Aof version error, not supported ver:0x%x. volume:%s", head->version, volume->volume_name);
		return -EINVAL;
	}
	file_len = head->length;
	if(file_len % 4096){
		rc = (int)sync_io(volume, append_buf, 4096, (file_len&(~4095LL)) + 4096 /*4096 is head length*/, 0);
		if(rc<0){
			S5LOG_ERROR("Failed to read aof:%s", volume->volume_name);
			return -EIO;
		}
		append_tail = file_len % 4096;
	}
	S5LOG_DEBUG("opened aof:%s len:%ld", volume->volume_name.c_str(), this->file_length());
#ifdef _DATA_DBG
	char tmp[256];

	sprintf(tmp, "%s%s", LOCAL_DIR,  volume->volume_name.c_str());

	localfd = ::open(tmp, O_RDWR|O_APPEND );
	if (localfd < 0) {
		S5LOG_ERROR("Failed open file:%s, rc:%d", tmp, errno);
		return rc;
	}

#endif
	return 0;
}
struct io_waiter
{
	sem_t sem;
	sem_t *throttle;
	int rc;
};

static void io_cbk(void* cbk_arg, int complete_status)
{
	struct io_waiter* w = (struct io_waiter*)cbk_arg;
	if(w->rc != 0)
		w->rc = complete_status;
	sem_post(w->throttle);
	sem_post(&w->sem);
}

static ssize_t sync_io(PfClientVolume* v, void* buf, size_t count, off_t offset, int is_write)
{
	int rc = 0;
	io_waiter w;
	w.throttle = &v->runtime_ctx->io_throttle;
	w.rc = 0;
	sem_init(&w.sem, 0, 0);
	sem_wait(w.throttle);
	rc = pf_io_submit(v, buf, count, offset, io_cbk, &w, is_write);
	if (rc)
		return rc;
	sem_wait(&w.sem);
	if (w.rc != 0) {
		S5LOG_ERROR("IO has failed for sync_io, rc:%d", w.rc);
		return w.rc;
	}
	return count;
}

void PfAof::sync()
{
	int rc = 0;
/*          +------------------------
 * Volume:  |   Volume data area  ...
 *          +---------+---------------
 * File:    | 4K head |     File data
 *          +---------+---------------
 * So:    offset_in_vol = offset_in_file + 4K
 */
	const uint64_t write_seg_size = PF_MAX_IO_SIZE;
	const uint64_t seg_align_mask = ~(write_seg_size - 1);
	ssize_t cur_file_tail = file_len - append_tail;
	ssize_t cur_vol_tail = cur_file_tail + 4096;
	if (file_len > volume->volume_size) {
		S5LOG_ERROR("AOF size:%ld exceed volume size:%ld", file_len, volume->volume_size);
	}


	io_waiter arg;
	arg.rc = 0;
	arg.throttle = &volume->runtime_ctx->io_throttle;
	int iodepth = AOF_IODEPTH;
	sem_init(&arg.sem, 0, iodepth);

	ssize_t buf_off = 0;
	while (cur_file_tail < file_len) {
		off_t next_64K_end_in_vol = (cur_vol_tail + write_seg_size) & seg_align_mask;
		off_t next_seg_end_in_file = next_64K_end_in_vol - 4096;

		ssize_t io_size = min(next_seg_end_in_file, file_len) - cur_file_tail;
		ssize_t aligned_io_size = io_size;
		if( (io_size&4095) != 0){
			aligned_io_size = (io_size + 4095) & (~4095LL);//upround to 4096
			memset((char*)append_buf + append_tail, 0, aligned_io_size - io_size);
		}
		sem_wait(arg.throttle);
		sem_wait(&arg.sem);
		if(arg.rc != 0) {
			S5LOG_FATAL("IO has failed, rc:%d",  arg.rc);
			break;
		}
		//S5LOG_INFO("Write at off:0x%lx len:0x%lx", cur_vol_tail, aligned_io_size);
		rc = pf_io_submit_write(volume, (char*)append_buf+ buf_off, aligned_io_size, cur_vol_tail, io_cbk, &arg);
		if (rc != 0) {
			S5LOG_FATAL("Failed to submit io, rc:%d", rc);
			break;
		}

		cur_vol_tail += io_size;
		cur_file_tail += io_size;
		buf_off += io_size;
	}
	for(int i=0;i<iodepth;i++){
		sem_wait(&arg.sem);	
		if (arg.rc != 0) {
			S5LOG_FATAL("IO has failed, rc:%d", arg.rc);
		}
	}

	sem_wait(arg.throttle);
	//write file head
	head->length = file_len;
	rc = pf_io_submit_write(volume, head_buf, 4096, 0, io_cbk, &arg);
	sem_wait(&arg.sem); 
	if (arg.rc != 0) {
		S5LOG_FATAL("IO has failed for aof sync, rc:%d", arg.rc);
	}

	//if there are some unaligned part in file tail, copy it to buffer head
	if(append_tail % 4096 != 0 && append_tail > 4096){
		//TODO: use of append_buf may use optimized later, to use a ping-pong buffer, avoid unnecessary copy
		memcpy(append_buf, (char*)append_buf + (append_tail & (~4095LL)), append_tail & 4095);
	}
	append_tail &= 4095;
}

ssize_t PfAof::append(const void* buf, ssize_t len)
{
	ssize_t remain = len;
	ssize_t buf_idx = 0;
	while(remain>0){
		ssize_t seg_size = min(remain, append_buf_size - append_tail);
		memcpy((char*)append_buf + append_tail, (char*)buf + buf_idx, seg_size);
		buf_idx += seg_size;
		remain -= seg_size;
		append_tail += seg_size;
		file_len += seg_size;
		if (append_tail == append_buf_size)
			sync();
	}
#ifdef _DATA_DBG
	write(localfd, buf, len);
#endif
	return len;
}

ssize_t PfAof::read(void* buf, ssize_t len, off_t offset) const
{
	int rc = 0;
/*          +------------------------
 * Volume:  |   Volume data area  ...
 *          +---------+---------------
 * File:    | 4K head |     File data
 *          +---------+---------------
 * So:    offset_in_vol = offset_in_file + 4K
 */
	if(offset >= file_len){
		S5LOG_WARN("Read offset:%ld exceed file len:%ld", offset, file_len);

		return 0;
	}
	if(offset + len > file_len){
		//S5LOG_WARN("Read exceed file end, set len from %ld to %ld", len, file_len - offset);
		len = file_len - offset;
	}
	static int _cnt;
	if((offset&4095) || (len & 4095)) {
		if(_cnt % 1000 == 0){
			S5LOG_WARN("unaligned read at off:0x%lx len:0x%lx, cnt:%d", offset, len, ++_cnt);
		}
	}
	ssize_t vol_off = offset + 4096;
	ssize_t vol_end = vol_off + len;

	if (offset + len > file_len - append_tail /*offset of append buf head in file*/) //some data is in write buffer
	{
		ssize_t in_buf_len = min(len, offset + len - (file_len - append_tail));
		off_t in_buf_off = 0;
		if(len == in_buf_len){
			in_buf_off = offset + len - (file_len - append_tail) - in_buf_len;
		}
		memcpy((char*)buf + len - in_buf_len, (const char*)append_buf + in_buf_off, in_buf_len);
		//S5LOG_INFO("Copy to user buf:%p+0x%lx, from append_buf, len:0x%lx, first QWORD:0x%lx",
		//	buf, len - in_buf_len, in_buf_len, *(long*)( (char*)buf + len - in_buf_len));
		if (len == in_buf_len)
			return len;//all in append buffer
		vol_end = vol_off + (len - in_buf_len);
		assert(vol_end % 4096 == 0);
	}
	ssize_t aligned_off = DOWN_ALIGN_4K(vol_off);
	ssize_t aligned_end = UP_ALIGN_4K(vol_end);


	const uint64_t read_seg_size = PF_MAX_IO_SIZE;
	const uint64_t seg_align_mask = ~(read_seg_size - 1);


	io_waiter arg;
	arg.rc = 0;
	arg.throttle = &volume->runtime_ctx->io_throttle;
	int iodepth = AOF_IODEPTH;
	sem_init(&arg.sem, 0, iodepth);

	bool copy_head = false;
	bool copy_tail = false;
	void* read_buf=NULL;
	ssize_t buf_off = 0;
	if (vol_off % 4096 != 0 || vol_end % 4096 != 0) {
		read_buf = aligned_alloc(4096, 8192);
	}
	//in read buf, first 4K is unaligned head, second 4K is unaligned tail
	if(vol_off%4096 != 0) {
		sem_wait(&volume->runtime_ctx->io_throttle);
		sem_wait(&arg.sem);
		//S5LOG_INFO("Read at off:0x%lx len:0x%lx for unaligned head", aligned_off, 4096);
		rc = pf_io_submit_read(volume, read_buf, 4096, aligned_off, io_cbk, &arg);
		if (rc != 0) {
			S5LOG_ERROR("Failed to submit io, rc:%d", rc);
			rc = -EIO;
			goto errout1;
		}
		aligned_off += 4096;//unaligned part is 4K
		copy_head = true;
	}
	if(vol_end % 4096 != 0 /*unaligned end*/  
		&&  ( DOWN_ALIGN_4K(vol_end) != DOWN_ALIGN_4K(vol_off) //unaligned end is in different 4K with head
			|| vol_off % 4096 == 0 /*no unaligned head*/) ){
		sem_wait(&volume->runtime_ctx->io_throttle);
		sem_wait(&arg.sem);	
		rc = pf_io_submit_read(volume, (char*) read_buf + 4096, 4096, DOWN_ALIGN_4K(vol_end), io_cbk, &arg);
		if (rc != 0) {
			S5LOG_ERROR("Failed to submit io, rc:%d", rc);
			rc = -EIO;
			goto errout1;
		}
		aligned_end -= 4096;
		copy_tail = true;
	}
	if(copy_head)
		buf_off = 4096 - (offset & 4095); //skip unaligned part in buffer
	while (aligned_off < aligned_end) {
		off_t next_64K_end_in_vol = (aligned_off + read_seg_size) & seg_align_mask;

		ssize_t aligned_io_size = min(aligned_end, next_64K_end_in_vol) - aligned_off;
		sem_wait(&volume->runtime_ctx->io_throttle);
		sem_wait(&arg.sem);	
		if (arg.rc != 0) {
			S5LOG_ERROR("IO has failed, rc:%d", arg.rc);
			rc = -EIO;
			goto errout1;
		}
		//S5LOG_INFO("Read at off:0x%lx len:0x%lx", aligned_off, aligned_io_size);
		rc = pf_io_submit_read(volume, (char*)buf + buf_off, aligned_io_size, aligned_off, io_cbk, &arg);
		if (rc != 0) {
			S5LOG_ERROR("Failed to submit io, rc:%d", rc);
			rc = -EIO;
			goto errout1;
		}

		aligned_off += aligned_io_size;
		buf_off += aligned_io_size;
	}


	for (int i = 0; i < iodepth; i++) {
		sem_wait(&arg.sem);
		if (arg.rc != 0) {
			S5LOG_ERROR("IO has failed, rc:%d", arg.rc);
			rc = -EIO;
			goto errout1;
		}
	}
	if (copy_head) {
		size_t copy_off = (vol_off & 4095LL);
		size_t copy_len = min(4096 - (vol_off & 4095LL), (long long)len);
		memcpy(buf, (char*)read_buf + copy_off, copy_len );
		//S5LOG_DEBUG("Copy unaligned head, from read_buf off:0x%lx to user buf: 0 len:0x%lx", copy_off, copy_len);
	}
	if (copy_tail) {
		memcpy((char*)buf + len - (vol_end & 4095LL), (const char*)read_buf + 4096, vol_end & 4095LL);
		//                         ^^^^^^^^^^^^^         ^^^^^^^^^^^^^^^
		//                                |                   + second 4K is unaligned data of tail
		//                                +  this is unaligned part length in tail
		//S5LOG_DEBUG("Copy unaligned tail, from read_buf off: 4096 to user buf: 0x%lx len:0x%lx", len - (vol_end & 4095LL), vol_end & 4095LL);
	}
#ifdef _DATA_DBG
	{
		void* buf2 = malloc(len);
		size_t r = pread(localfd, buf2, len, offset);
		if (r != len) {
			S5LOG_ERROR("Failed read local file, rc:%d", errno);
		}
		if (memcmp(buf, buf2, len) != 0) {
			S5LOG_ERROR("Data error ,file:%s offset:%ld, len:%ld", volume->volume_name.c_str(), offset, len);
			int fd = ::open("/tmp/errdata.dat", O_RDWR|O_TRUNC|O_CREAT, 0666);
			::write(fd, buf, len);
			::close(fd);
		}
		free(buf2);
	}
#endif
	//S5LOG_INFO("Read buf:%p first QWORD:0x%lx",buf, *(long*)buf);
	if(read_buf)
		free(read_buf);
	return len;
errout1:
	free(read_buf);
	return rc;
}
/**
 * @return 0 if volume exists; -1 other otherwise
 */
int pf_aof_access(const char* volume_name, const char* cfg_filename)
{
	int rc = 0;
	conf_file_t cfg = conf_open(cfg_filename);
	if (cfg == NULL)
	{
		S5LOG_ERROR("Failed open config file:%s", cfg_filename);
		return -errno;
	}
	DeferCall _cfg_r([cfg]() { conf_close(cfg); });
	

	char* esc_vol_name = curl_easy_escape(NULL, volume_name, 0);
	if (!esc_vol_name)
	{
		S5LOG_ERROR("Curl easy escape failed.");
		return -ENOMEM;
	}
	DeferCall _1([esc_vol_name]() { curl_free(esc_vol_name); });

	std::string query = format_string("op=check_volume_exists&volume_name=%s", esc_vol_name);
	GeneralReply r;
	rc = query_conductor(cfg, query, r, true);
	if (rc != 0)
		return rc;
	return r.ret_code;
}


 int pf_ls_aof_children(const char* tenant_name, const char* cfg_filename, std::vector<std::string>* result)
{
	int rc = 0;
	conf_file_t cfg = conf_open(cfg_filename);
	if (cfg == NULL)
	{
		S5LOG_ERROR("Failed open config file:%s", cfg_filename);
		return -errno;
	}
	DeferCall _cfg_r([cfg]() { conf_close(cfg); });


	char* esc_t_name = curl_easy_escape(NULL, tenant_name, 0);
	if (!esc_t_name)
	{
		S5LOG_ERROR("Curl easy escape failed.");
		return -ENOMEM;
	}
	DeferCall _1([esc_t_name]() { curl_free(esc_t_name); });
	std::string query = format_string("op=ls_children&tenant_name=%s", esc_t_name);

	LsAofReply reply;
	rc = query_conductor(cfg, query, reply, true);
	if (rc != 0)
	{
		S5LOG_ERROR("Failed query conductor, rc:%d", rc);
		return rc;
	}
	*result = std::move(reply.files);
	return reply.ret_code == 0 ? 0 : -ENOENT;
}

 int pf_rename_aof(const char* volume_name, const char* new_name, const char* pf_cfg_file)
 {
	 std::lock_guard<std::mutex> _l(aof_lock);
	 auto pos = opened_aof.find(volume_name);
	 if (pos != opened_aof.end()) {
		 S5LOG_INFO("AOF '%s' has already opened!", volume_name);
		 PfAof* f = pos->second;
		 f->volume->volume_name=new_name;
		 opened_aof.erase(pos);
		 if(opened_aof.find(new_name) != opened_aof.end()){
			S5LOG_ERROR("target name '%s' still opened", new_name);
		 }
		 opened_aof[f->volume->volume_name] = f;
	 }
	 int rc = pf_rename_volume(volume_name, new_name, pf_cfg_file);
#ifdef _DATA_DBG
	 char tmp1[256], tmp2[256];

	 sprintf(tmp1, "%s%s", LOCAL_DIR, volume_name);
	 sprintf(tmp2, "%s%s", LOCAL_DIR, new_name);
	 if(::access(tmp1,F_OK) == 0){
		 ::rename(tmp1, tmp2);
		 S5LOG_DEBUG("rename tmp file %s to %s, rc:%d", tmp1, tmp2, errno);
	 } else {
		S5LOG_ERROR("old file: %s not accessable, rc:%d", errno);
	 }
#endif
	return rc;
 }

 int pf_delete_aof(const char* volume_name, const char* pf_cfg_file)
 {
	 std::lock_guard<std::mutex> _l(aof_lock);
	 auto pos = opened_aof.find(volume_name);
	 if (pos != opened_aof.end()) {
		 S5LOG_INFO("AOF '%s' has already opened !", volume_name);
		 opened_aof.erase(pos);
	 }
	 int rc = pf_delete_volume(volume_name, pf_cfg_file);
#ifdef _DATA_DBG
	 char tmp1[256];

	 sprintf(tmp1, "%s%s", LOCAL_DIR, volume_name);
	 if (::access(tmp1, F_OK) == 0) {
		 ::unlink(tmp1);
		 S5LOG_DEBUG("delete tmp file %s rc:%d", tmp1, errno);
	 }
	 else {
		 S5LOG_ERROR("old file: %s not accessable, rc:%d", errno);
	 }
#endif
	 return rc;
 }

