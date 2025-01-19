#define PF_EC_INDEX_PAGE_SIZE (64<<10)
#define PF_OFF2PTE_INDEX(x) ((x)>>16) //i.e. x/64K
class PfLutPage {
	void* addr; //address of page
	int lru_count;
};

class PfLutPageManager{
public:
	PfLutPage* get_page();
};
enum PteState {
	PAGE_UNPRESENT = 0;
	PAGE_PRESENT = 1;
	PAGE_LOADING = 2;
};
class PfLutPte{
public:
	PteState state;
	PfLutPage* page;
	PfEcClientVolume* owner;
	//int64_t offset;
	PfClientIocb* waiting_list;
	//PfServerIocb* forward_lut_waiting_list; //all client IO waiting on this swap_io, valid for swap IO
	//PfServerIocb* reverse_lut_waiting_list; //all client IO waiting on this swap_io, valid for swap IO
	int64_t forward_lut_loop_offset; //offset used in for loop, in set reverse lut value, valid for  client io
	int64_t reverse_lut_loop_offset; //offset used in for loop, in set reverse lut value, valid for  client io

	int64_t offset();
};
enum RetCode {
	Ok = 0;
	Yield = 1;
	//number  <0 means posix errno
};
enum LutUpdateState{
	Done = 1;
	 
};

////IO processing state, IO is processed in asynchronous mode,
enum EcIoState
{
	APPENDING_AOF = 0;//appending data to aof
	FILLING_WAL = 1;
	UPDATING_FWD_LUT = 2; //updating forward lut table
	UPDATING_RVS_LUT = 3; //updating reverse lut table
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
	PfReplicatedVolume* meta_volume;
};

class PfEcRedoLog
{

};
