#ifndef pf_trace_defs_h__
#define pf_trace_defs_h__

/* Owner definitions */
#define OWNER_PFS_CIENT_IO	0x1

/* Object definitions */
#define OBJECT_CLIENT_IO	0x1

/* Trace group definitions */
#define TRACE_GROUP_CLIENT	0x1

/* Client io tracepoint definitions */

#define TRACE_IO_EVENT_STAT		    SPDK_TPOINT_ID(TRACE_GROUP_CLIENT, 0x0)

static inline uint64_t get_us_from_tsc(uint64_t tsc, uint64_t tsc_rate)
{
	return tsc * 1000 * 1000 / tsc_rate;
}

#endif /* SPDK_INTERNAL_TRACE_DEFS */