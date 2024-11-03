#define PF_EC_INDEXER_PAGE_SIZE (64<<10)

class PfLutPage {
	void* addr; //address of page
	int lru_count;
};

class PfLutPte{
public:
	PfServerIocb* forward_lut_waiting_list; //all client IO waiting on this swap_io, valid for swap IO
	PfServerIocb* reverse_lut_waiting_list; //all client IO waiting on this swap_io, valid for swap IO
	int64_t forward_lut_loop_offset; //offset used in for loop, in set reverse lut value, valid for  client io
	int64_t reverse_lut_loop_offset; //offset used in for loop, in set reverse lut value, valid for  client io

};
enum RetCode {
	Ok = 0;
	Yield = 1;
	//number  <0 means posix errno
};
enum LutUpdateSate{
	Done = 1;

};
class PfEcVolumeIndex
{
public:
	//set offset of lba in aof
	RetCode set_forward_lut(PfServerIocb* iocb, int64_t vol_offset, int64_t aof_offset);
	RetCode set_reverse_lut(PfServerIocb* iocb, int64_t vol_offset, int64_t aof_offset);

	int64_t get_io_lut_value(PfServerIocb* iocb, int64_t);
private:
	int disp_index;
	int64_t vol_id;
	void load_page(PfServerIocb* iocb, int64_t offset, int length, PfIndexPage* page);
	PfClientVolumeInfo *persist_volume;
};

class PfEcRedoLog
{

};
