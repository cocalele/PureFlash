#include "pf_aof.h"

#define AOF_MAGIC 0x466F4150 //PAoF
#define AOF_VER 0x00010000
static ssize_t sync_io(PfVolume* v, const void* buf, size_t count, off_t offset, int is_write);


class GeneralReply
{
public:
	int ret_code;
	std::string reason;
};
void from_json(const json& j, GeneralReply& reply)
{
	j.at("ret_code").get_to(reply.ret_code);
	if (reply.ret_code != 0)
		j.at("reason").get_to(reply.reason);
}

PfAof::PfAof(ssize_t append_buf_size)
{
	head_buf = aligned_alloc(4096, 4096);
	if (append_buf == NULL)
		throw std::runtime_error(format_string("Failed alloc head buf"));
	memset(head_buf, 0, 4096);
	append_buf = aligned_alloc(4096, append_buf_size);
	if(append_buf == NULL)
		throw std::runtime_error(format_string("Failed alloc append buf"));

	this->append_buf_size = append_buf_size;
	this->append_tail = 0;
}
PfAof::~PfAof()
{
	free(append_buf);
}
int pf_create_aof(const char* volume_name, int rep_cnt, const char* cfg_filename, int lib_ver)
{
	int rc = 0;
	PfAofHead* head = aligned_alloc(4096, 4096);
	if (head == NULL) {
		S5LOG_ERROR("Failed alloc head buf");
		return -ENOMEM;
	}
	DeferCall _head_r([head]() { free(head); });
	memset(head, 0, 4096);

	conf_file_t cfg = conf_open(cfg_filename);
	if (cfg == NULL)
	{
		S5LOG_ERROR("Failed open config file:%s", cfg_file.c_str());
		return -errno;
	}
	DeferCall _cfg_r([cfg]() { conf_close(cfg); });

	char* esc_vol_name = curl_easy_escape(NULL, volume_name.c_str(), 0);
	if (!esc_vol_name)
	{
		S5LOG_ERROR("Curl easy escape failed.");
		return -ENOMEM;
	}
	DeferCall _1([esc_vol_name]() { curl_free(esc_vol_name); });
	
	std::string query = format_string("op=create_aof&volume_name=%s&size=%ld&rep_cnt=%d",
		op, esc_vol_name, 128<<30, rep_cnt);
	GeneralReply r;
	rc = query_conductor(cfg, query, r);
	if (rc != 0)
		return rc;
	if (r.ret_code != 0)
		return r.ret_code;
	struct PfClientVolume* vol = pf_open_volume(volume_name, cfg_filename, NULL, lib_ver);
	if(vol == NULL){
		S5LOG_ERROR("Failed to open volume:%s" volume_name);
		return -EIO;
	}
	DeferCall _v_r([vol]() {
		vol->close();
		delete vol; });
	head->magic = AOF_MAGIC;
	head->version = AOF_VER;
	rc = sync_io(vol, head, 4096, 0, 1);
	if (rc <= 0) {
		S5LOG_ERROR("Failed write aof head, rc:%d", -errno);
		return rc;
	}
	return 0;
}

/**
 * @param flags: O_CREAT to create file on not existing
 */
PfAof* pf_open_aof(const char* volume_name, int flags, const char* cfg_filename, int lib_ver)
{
	int rc = 0;
	if (lib_ver != S5_LIB_VER) {
		S5LOG_ERROR("Caller lib version:%d mismatch lib:%d", lib_ver, S5_LIB_VER);
		return NULL;
	}
	if(pf_aof_access(volume_name, cfg_filename) != 0) {
		if(flags & O_CREAT) {
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
		PfAof* volume = new PfAof;
		if (volume == NULL)
		{
			S5LOG_ERROR("alloca memory for volume failed!");
			return NULL;
		}
		_clean.push_back([volume]() { delete volume; });
		//other calls
		volume->volume_name = volume_name;
		if (cfg_filename == NULL)
			cfg_filename = default_cfg_file;
		volume->cfg_file = cfg_filename;
		if (snap_name)
			volume->snap_name = snap_name;

		rc = volume->do_open(false, true); //open volume
		if (rc) {
			return NULL;
		}
		S5LOG_INFO("Succeeded open volume %s@%s(0x%lx), meta_ver=%d, io_depth=%d", volume->volume_name.c_str(),
			volume->snap_seq == -1 ? "HEAD" : volume->snap_name.c_str(), volume->volume_id, volume->meta_ver, volume->io_depth);
		rc = volume->open(); //open aof
		if (rc) {
			return NULL;
		}
		

		_clean.cancel_all();
		return volume;
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
	sync_io(this, head_buf, 4096, 0, 0);
	if (head->magic != AOF_MAGIC) {
		S5LOG_ERROR("Aof magic error, not a AoF file. volume:%s", volume_name);
		return -EINVAL;
	}
	if (head->version != AOF_VER) {
		S5LOG_ERROR("Aof version error, not supported ver:0x%x. volume:%s", head->version, volume_name);
		return -EINVAL;
	}
	file_len = head->length;
	if(file_len % 4096){
		rc = sync_io(this, append_buf, 4096, file_len&(~4095LL), 0);
		if(rc<0){
			S5LOG_ERROR("Failed to read aof:%s", volume_name);
			return -EIO;
		}
		append_tail = file_len % 4096;
	}
	return 0;
}
struct io_waiter
{
	sem_t sem;
	int rc;
};

static void io_cbk(void* cbk_arg, int complete_status)
{
	struct io_waiter* w = (struct io_waiter*)cbk_arg;
	if(w->rc != 0)
		w->rc = complete_status;
	sem_post(&w->sem);
}
static ssize_t sync_io(PfVolume* v, const void* buf, size_t count, off_t offset, int is_write)
{
	io_waiter w;
	w.rc = 0;
	sem_init(&w.sem, 0, 0);

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
	const uint64_t write_seg_size = 64 << 10;//Pureflash allow 64KB for each write
	const uint64_t seg_align_mask = ~(write_seg_size - 1);
	ssize_t cur_file_tail = file_len - append_tail;
	ssize_t cur_vol_tail = cur_file_tail + 4096;

	io_waiter arg;
	arg.rc = 0;
	int iodepth = 32;
	sem_init(&arg.sem, 0, iodepth);//io depth 32

	ssize_t buf_off = 0;
	while (cur_file_tail < file_len) {
		off_t next_64K_end_in_vol = (cur_vol_tail + write_seg_size) & seg_align_mask;
		off_t next_seg_end_in_file = next_64K_end_in_vol - 4096;

		ssize_t io_size = min(next_seg_end_in_file, file_len) - cur_file_tail;
		int aligned_io_size = io_size;
		if( (io_size&4095) != 0){
			aligned_io_size = (io_size + 4095) & (~4095LL);//upround to 4096
			memset(append_buf + append_tail, 0, aligned_io_size - io_size);
		}
		sem_wait(&arg.sem);
		if(arg.rc != 0) {
			S5LOG_FATAL("IO has failed, rc:%d",  arg.rc);
			break;
		}
		rc = pf_io_submit_write(this, append_buf+ buf_off, aligned_io_size, cur_vol_tail, io_cbk, &arg);
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

	//write file head
	head->length = file_len;
	rc = pf_io_submit_write(this, head_buf, 4096, 0, io_cbk, &arg);
	sem_wait(&arg.sem);
	if (arg.rc != 0) {
		S5LOG_FATAL("IO has failed for aof sync, rc:%d", arg.rc);
	}

	//if there are some unaligned part in file tail, copy it to buffer head
	if(append_tail % 4096 != 0 && append_tail > 4096){
		//TODO: use of append_buf may use optimized later, to use a ping-pong buffer, avoid unnecessary copy
		memcpy(append_buf, append_buf + (append_tail & (~4095LL)), append_tail & 4095);
	}
	append_tail &= 4095;
}

ssize_t AfAof::append(const void* buf, ssize_t len)
{
	int rc = 0;
	ssize_t remain = len;
	ssize_t buf_idx = 0;
	while(remain>0){
		ssize_t seg_size = min(remain, append_buf_size - append_tail);
		memcpy(append_buf + append_tail, buf + buf_idx, seg_size);
		buf_idx += seg_size;
		remain -= seg_size;
		append_tail += seg_size;
		if (append_tail == append_buf_size)
			sync();
	}
}

/**
 * @return 0 if volume exists; -1 other otherwise
 */
static int pf_aof_access(const char* volume_name, const char* cfg_filename)
{
	int rc = 0;
	conf_file_t cfg = conf_open(cfg_filename);
	if (cfg == NULL)
	{
		S5LOG_ERROR("Failed open config file:%s", cfg_file.c_str());
		return -errno;
	}
	DeferCall _cfg_r([cfg]() { conf_close(cfg); });
	

	char* esc_vol_name = curl_easy_escape(NULL, volume_name.c_str(), 0);
	if (!esc_vol_name)
	{
		S5LOG_ERROR("Curl easy escape failed.");
		return -ENOMEM;
	}
	DeferCall _1([esc_vol_name]() { curl_free(esc_vol_name); });

	std::string query = format_string("op=check_volume_exists&volume_name=%s", op, esc_vol_name);
	GeneralReply r;
	rc = query_conductor(cfg, query, r);
	if (rc != 0)
		return rc;
	return r.ret_code;
}