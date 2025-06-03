#ifndef pf_ec_client_volume_h__
#define pf_ec_client_volume_h__
#include "pf_client_priv.h"
class PfClientAppCtx;
class pfqueue;
class PfEcRedolog;
class PfAof;
class PfReplicatedVolume;
class PfEcVolumeIndex;
struct PfRoutine;
class PfEcSectionInfoTable;

#define PF_SECTION_INDEX(x) (uint8_t)(((uint64_t)(x))>>56)
#define PF_EC_REDOLOG_SIZE (1LL<30) 
class PfEcClientVolume : public PfClientVolume
{



public:
	struct HeadPage {
		uint64_t section_tbl_position_first;
		uint64_t section_tbl_position_second;

		uint64_t redolog_position_first;
		uint64_t redolog_position_second;
		uint64_t fwd_lut_offset;
		uint64_t rvs_lut_offset;
		uint64_t redolog_size;
		/**update after save metadata**/
		int64_t  redolog_phase;
		//uint8_t  current_metadata;
		//uint8_t  current_redolog; //0: first redolog and first section table, 1: second
		/***/
		char create_time[32];
	} head;
	void* head_buf; //aligned buffer,used to flush head
	int do_open(bool reopen = false, bool is_aof = false) override;
	int io_submit(void* buf, size_t length, off_t offset,
		ulp_io_handler callback, void* cbk_arg, int is_write) override;
	int iov_submit(const struct iovec* iov, const unsigned int iov_cnt, size_t length, off_t offset,
		ulp_io_handler callback, void* cbk_arg, int is_write) override;

	int process_event(int event_type, int arg_i, void* arg_p) override;
	void close() override;
	//functions for EC volume

	PfAof* data_volume;
	PfReplicatedVolume* meta_volume;
	PfEcVolumeIndex* ec_index;
	PfEcRedolog* ec_redolog;
	PfEcSectionInfoTable * section_tbl;

	void discard_redolog();
	void co_flush();//entry of coroutine
	void co_load(); //entry of coroutine
	int co_save_meta_data();

	PfRoutine* flush_routine; //not use FlushCb
	volatile bool meta_in_flushing = false;
	volatile int zone_to_flush; //meta zone to flush
	virtual ~PfEcClientVolume();
private:
	sem_t load_done;
	int64_t co_determine_max_phase();
};
#endif // pf_ec_client_volume_h__