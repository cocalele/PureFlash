#ifndef pf_stat_h__
#define pf_stat_h__

struct DispatchStat
{
	int64_t rd_cnt;
	int64_t wr_cnt;
	int64_t rep_wr_cnt;

	int64_t rd_bytes;
	int64_t wr_bytes;
	int64_t rep_wr_bytes;
	DispatchStat& operator+=(const DispatchStat& rhs) 
	{
		this->rd_cnt += rhs.rd_cnt;
		this->wr_cnt += rhs.wr_cnt;
		this->rep_wr_cnt += rhs.rep_wr_cnt;

		this->rd_bytes += rhs.rd_bytes;
		this->wr_bytes += rhs.wr_bytes;
		this->rep_wr_bytes += rhs.rep_wr_bytes;
		return *this; // return the result by reference
	}
};


#endif // pf_stat_h__
