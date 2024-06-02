#ifndef pf_trace_defs_h__
#define pf_trace_defs_h__

/* Owner definitions */
#define OWNER_PFS_SPDK_IO	0x2

/* Object definitions */
#define OBJECT_SPDK_IO	0x2

/* Trace group definitions */
#define TRACE_GROUP_SPDK	0x2

/* Owner definitions */
#define OWNER_PFS_DISP_IO	0x3

/* Object definitions */
#define OBJECT_DISP_IO	0x3

/* Trace group definitions */
#define TRACE_GROUP_DISP	0x3

/* spdk io tracepoint definitions */
#define TRACE_DISK_IO_STAT		    SPDK_TPOINT_ID(TRACE_GROUP_SPDK, 0x0)

/* disp io tracepoint definitions */
#define TRACE_DISP_IO_STAT		    SPDK_TPOINT_ID(TRACE_GROUP_DISP, 0x0)
#define TRACE_DISP_REP_IO_STAT              SPDK_TPOINT_ID(TRACE_GROUP_DISP, 0x1)
static inline uint64_t get_us_from_tsc(uint64_t tsc, uint64_t tsc_rate)
{
	return tsc * 1000 * 1000 / tsc_rate;
}

#endif /* SPDK_INTERNAL_TRACE_DEFS */
