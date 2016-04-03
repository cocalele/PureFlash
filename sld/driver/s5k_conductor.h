#ifndef __S5K_CONDUCTOR_H__
#define __S5K_CONDUCTOR_H__

#include "s5k_basetype.h"
#include "s5k_imagectx.h"

#define MAX_NIC_IP_BLACKLIST_LEN 16
#define IPV4_ADDR_LEN 16
#define MAX_NAME_LEN 96	///< max length of name used in s5 modules.
typedef char name_buf_t[MAX_NAME_LEN];
typedef struct s5_cltreq_volume_open_param
{
	name_buf_t		volume_name;
	name_buf_t		snap_name;
	name_buf_t		tenant_name;
	int				volume_ctx_id;
	int				nic_ip_blacklist_len;
	ipv4addr_buf_t	nic_ip_blacklist[MAX_NIC_IP_BLACKLIST_LEN];
} s5_cltreq_volume_open_param_t;

/**
 * Parent info of volume in S5.
 *
 * Only for internal use, and is different from s5bd_parent_info_t, which will be released out.
 */
typedef struct s5d_parent_info
{
	uint64_t volume_id;		///< id of volume
	uint64_t snap_seq;		///< sequence num of snapshot
	uint64_t overlap;		///< overlap size between parent and child volume
} s5d_parent_info_t;

/**
 * Metadata info in S5.
 */
typedef struct s5bd_metadata
{
	uint64_t volume_id;
	uint64_t volume_size;		///< volume size in bytes
	uint64_t features;
	int32_t snap_seq;			///< snapContext-last seq.
	s5d_parent_info_t parent;	///< parent info
} s5bd_metadata_t;

/**
 * Reply data definition for volume open request
 */
typedef struct s5_cltreq_volume_open_reply
{
	int32_t				nic_port;
	ipv4addr_buf_t	nic_ip;
	int32_t				volume_ctx_id;
	s5bd_metadata_t	meta_data;
} s5_cltreq_volume_open_reply_t;

/**
 * Parameter definition for volume close request *
 */
typedef struct s5_cltreq_volume_close_param
{
	int			volume_ctx_id;
    char tenant_name[MAX_NAME_LEN];
}s5_cltreq_volume_close_param_t;

/**
 * meta data reply
 *
 * It is used to pass all reply from s5conductor to S5 clinet.
 */
typedef struct s5_clt_reply
{
	int32_t sub_type;			///< client reply type
	int32_t result;				///< result status of request process, '0' for success, otherwise for error code.
	int32_t num;				///< for 'list' request, like tenant list, 'num' denotes objects(tenant) number.

	union
	{
        /*
		s5_tenant_t tenants[0];
		s5_volume_info_t volumes[0];
		s5_client_link_t cltlinks[0];
		s5_store_info_t s5stores[0];						///< result of s5store list request
		s5_store_detailed_info_t s5store_detailed_info[0];	///< result of s5store stat request
		s5_conductor_info_t conductors[0];
        */
		s5_cltreq_volume_open_reply_t open_rpl_data[0];
        /*
		s5_fan_info_t fans[0];						///< result of fan list request.
		s5_tray_module_info_t trays[0];						///< result of tray list request.
		s5_rge_module_info_t rges[0];						///< result of rge list request.
		s5_bcc_module_info_t bccs[0];						///< result of bcc list request.
		s5_power_info_t powers[0];							///< result of power list request.
		s5_host_port_info_t host_ports[0];					///< result of host ports list request.
		s5_realtime_statistic_info_t statistic_info[0];		///< result of realtime iops, bw, latency request.
		int32_t s5_fan_speed;								///< result of the fan speed.
		uint64_t occupied_capacity;							///< result of occupied capacity of tenant or volume.
		s5_info_t	sys_info[0];
		uint64_t overlap;
		int snap_protected;
		char error_info[0];
        */
	} reply_info;			///< if request processed successfully, it will store reply data; otherwise, error info will be stored in 'error_info'.
} s5_clt_reply_t;
//} __attribute__((packed)) s5_clt_reply_t;

int cdkt_register_volume(struct s5_imagectx *ictx);
int cdkt_unregister_volume(struct s5_imagectx *ictx);
#endif //__S5K_CONDUCTOR_H__
