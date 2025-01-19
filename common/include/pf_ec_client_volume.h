class PfEcClientVolume : public PfClientVolume
{
	PfClientAppCtx* runtime_ctx;
	pfqueue* event_queue;
	PfEcWal* ec_redolog;
public:
	int do_open(bool reopen = false, bool is_aof = false) override;
	int pf_io_submit(struct PfClientVolume* volume, void* buf, size_t length, off_t offset,
		ulp_io_handler callback, void* cbk_arg, int is_write) override;
	int pf_iov_submit(struct PfClientVolume* volume, const struct iovec* iov, const unsigned int iov_cnt, size_t length, off_t offset,
		ulp_io_handler callback, void* cbk_arg, int is_write) override;

protected:
	int process_event(int event_type, int arg_i, void* arg_p) override;

	//functions for EC volume
	int io_write(PfServerIocb* iocb, PfVolume* vol);
	int update_lut(PfServerIocb* iocb, struct PfEcRedologEntry* wal, int64_t off);
	int on_page_load_complete(PfServerIocb* swap_io);


	PfAof* data_volume;
	PfReplicatedVolume* meta_volume;
	PfEcVolumeIndex* ec_index;
}