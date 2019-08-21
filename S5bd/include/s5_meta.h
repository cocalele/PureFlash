/**
 * Copyright NetBRIC(C), 2014-2015.
* @file
* All meta-data data structures should be defined here.
*
 * This file includes all metadata data structures which are shared between s5bd and s5conductor.
 * 
*/
#ifndef __S5_META__
#define __S5_META__



#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdlib.h>

#define MAX_NAME_LEN 96

#define IPV4_ADDR_LEN 16				///< buffer length for addr of ipv4
#define MAX_S5STORE_NAME_LEN 59			///< Max length for s5store name.
#define MAX_SN_LEN 128					///< Max serial number length.
#define MAX_MODEL_LEN 128				///< Max model length.

#define PORT_BASE   49162               ///<the value of port base.


/** S5 conductor type property. */
typedef enum s5_conductor_role
{
	S5CDT_SLAVE = 0,
	S5CDT_MASTER = 1,
	S5CDT_NULL_ROLE
} s5_conductor_role_t;

typedef enum s5_hardware_status
{
        STATE_OK = 0,
        STATE_NA,
        STATE_ERROR,
		STATE_WARNING,
		STATE_INITIALIZING, //initializing 
        STATE_INVALID,
        STATE_IO_ERROR,
        STATE_MAX
} s5_hardware_status_t;

/** S5 status property for a component, such as S5store, conductor. */
typedef enum s5_component_status
{
	S5_COMPONENT_OK = STATE_OK,
	S5_COMPONENT_ERROR = STATE_ERROR,
	S5_COMPONENT_WARNING = STATE_WARNING,
} s5_component_status_t;

/** s5store info */
typedef struct s5_store_info
{
    char s5store_name[MAX_S5STORE_NAME_LEN];    ///< S5store name.
    char sn[MAX_SN_LEN];                        ///< Serial number.
    char model[MAX_MODEL_LEN];                  ///< The model of S5store.
	int rge_cnt;		                        ///< Rge count number
	int nic_cnt;                                ///< Nic count number
    int tray_cnt;                               ///< Tray count number.
	int fan_cnt;                                ///< Fan count number.
    int bcc_cnt;                                ///< Bcc count number.
    int power_cnt;                              ///< Power count number.
    s5_component_status_t status;               ///< The status of S5store.
    char master_daemon[IPV4_ADDR_LEN];          ///< The daemon 0 ip.
    char slave_daemon[IPV4_ADDR_LEN];           ///< The daemon 1 ip.
} s5_store_info_t;

/** s5store list */
typedef struct s5_store_list 
{
	s5_store_info_t* s5stores;		///< s5store information
	int num;					///< s5store number
} s5_store_list_t;


/** conductor info */
typedef struct s5_conductor_info
{
	s5_conductor_role_t    role;
    s5_component_status_t  status;
    char ip[IPV4_ADDR_LEN];
} s5_conductor_info_t;


#define MAX_CONDUCTOR_CNT 2
#define MAX_ERROR_INFO_LENGTH 4096		///< buffer length for error info
#define S5_PHY_BLOCK_SIZE (4 << 20)		///< size of physical block in S5
#define NIC_CNT_PER_RGE 4				///< nic count of RGE board
#define MAX_FILE_PATH_LEN 512		    ///< max file path length
#define MAX_VERIFY_KEY_LEN 512		    ///< max password or other key string length
#define MAX_NIC_IP_BLACKLIST_LEN 16		///< max len of nic ip blacklist

#define MAX_NIC_CNT_PER_STORE 16
#define MAX_RGE_CNT_PER_STORE 4
#define MAX_FAN_CNT_PER_STORE 10
#define MAX_POWER_CNT_PER_STORE 3
#define MAX_TRAY_CNT_PER_STORE 30
#define MAX_BCC_CNT_PER_STORE 2
#define MAX_SET_CNT_PER_STORE 60
#define MASK_ADDR_LEN 16
#define MAC_ADDR_LEN 18 
#define S5_HW_MAX_NAME  32

#define MAX_REPLICA_NUM 3 

typedef char name_buf_t[MAX_NAME_LEN];
typedef char verify_key_buf_t[MAX_VERIFY_KEY_LEN];
typedef char sn_buf_t[MAX_SN_LEN];
typedef char model_buf_t[MAX_MODEL_LEN];
typedef char ipv4addr_buf_t[IPV4_ADDR_LEN];


/** Volume access property in S5. */
typedef enum s5_volume_access_property
{
    S5_XX_XX = 0,                       ///< All read-write permissions are forbidden
    S5_RX_XX = 8,                       ///< Only owner read permission is allowed
    S5_RW_XX = 12,                      ///< Only owner read-write permission is allowed
    S5_RX_RX = 10,                      ///< All read permissions are allowed and all write permissions are forbidden 
    S5_RW_RW = 15,                      ///< All read-write permissions are allowed 
} s5_volume_access_property_t;

/** 
 *S5 volume info. 
 */
typedef struct s5_volume_info
{
    char volume_name[MAX_NAME_LEN];         ///< volume name
    char tenant_name[MAX_NAME_LEN];         ///< tenant name
    char quotaset_name[MAX_NAME_LEN];       ///< quotaset name
    int64_t iops;                                       ///< iops quota of volume
    int64_t cbs;                                        ///< cbs quota of volume
    int64_t bw;                                         ///< bw quota of volume
    int64_t size;                                       ///< size quota of volume 
    int64_t flag;                                       ///< volume additional features, e.g. encryption scheme, compress mode and something alike
    int32_t replica_count;                              ///< volume replicas count
    int32_t tray_id[MAX_REPLICA_NUM];                   ///< the tray id array where the volume should be created
    char s5store_name[MAX_REPLICA_NUM][MAX_NAME_LEN];   ///< the s5store array where the volume should be created
    s5_volume_access_property_t access;     ///< volume access property
} s5_volume_info_t;

/** volume list */
typedef struct s5_volume_list
{
    s5_volume_info_t* volumes;              ///< volumes in list
    int num;                                 ///< volumes number
} s5_volume_list_t;

/** 
 * tenant info in S5.
 */
typedef struct s5_tenant
{
    int64_t volume;                           ///< volume quota owned by tenant
    int64_t iops;                             ///< iops quota of tenant
    int64_t bw;                               ///< bandwidth quota of tenant
    char name[MAX_NAME_LEN];            ///< tenant name
    char pass_wd[MAX_VERIFY_KEY_LEN];   ///< password of tenant
    /** 
     * Authority of tenant.
     *
     * Authority of tenant describes access permission level of tenant, and
     * 0 indicates normal user, 1 indicates administrator, -1 indicates invalid tenant.
     */
    int auth;
} s5_tenant_t;

/** tenant list */
typedef struct
{
    s5_tenant_t* tenants;       ///< tenants buffer
	int num;                    ///< tenants count
}s5_tenant_list_t;

/** 
 * fan info.
 */
typedef struct s5_fan_info
{
	char fan_name[MAX_NAME_LEN];		///< fan name
	int  fan_status;					///< fan status: OK, Not plug-in = 0, Device error = 1, Invalid device = 2, or IO Error = 3
	int32_t  speed;						///< fan speed
	int32_t  rate;						///< fan speed rate: percentage * 100
} s5_fan_info_t;

/** fan list */
typedef struct s5_fan_list
{
	s5_fan_info_t*  fans;		///< fan info list  	
	int  num;							///< fan info count
} s5_fan_list_t;

/** 
 * tray info in s5store.^M
 */
typedef struct s5_tray_module_info
{
	char tray_name[MAX_NAME_LEN];		///< tray name
	int  tray_status;					///< tray status：OK, Not plug-in = 0, Device error = 1, Invalid device = 2, or IO Error = 3			
	char tray_model[MAX_MODEL_LEN];		///< tray model 
	int  tray_bit;						///< tray bit version
	uint32_t  tray_firmware;			///< tray upper computer version
	double tray_temperature;			///< tray temperature, unit: ℃ 
	int64_t  tray_raw_capacity;		    ///< tray raw capacity, unit: MB
	int64_t  tray_usable_capacity;		///< tray usable capacity, unit: MB

	char set0_name[MAX_NAME_LEN];		///< set0 name
	int  set0_status;					///< set0 status：OK, Not plug-in = 0, Device error = 1, Invalid device = 2, or IO Error = 3	
    char set0_model[MAX_MODEL_LEN];		///< set0 hardware model
	int  set0_bit;						///< set0 bit version 
	double set0_temperature;		    ///< set0 temperature

	char set1_name[MAX_NAME_LEN];		///< set1 name
	int  set1_status;					///< set1 status: OK, Not plug-in = 0, Device error = 1, Invalid device = 2, or IO Error = 3	
	char set1_model[MAX_MODEL_LEN];		///< set1 hardware version
	int  set1_bit;						///< set1 bit version
	double set1_temperature;			///< set1 temperature
} s5_tray_module_info_t;
/** tray list */
typedef struct s5_tray_module_list
{
	s5_tray_module_info_t* tray_modules;///< tray info list 	
	int num;							///< the number of tray info
} s5_tray_module_list_t;


/** 
 * rge info in s5store.
 */
typedef struct s5_rge_module_info
{
	char rge_name[MAX_NAME_LEN];		///< rge name
	int  rge_status;					///< rge status: OK, Not plug-in = 0, Device error = 1, Invalid device = 2, or IO Error = 3	
	char rge_model[MAX_MODEL_LEN];		///< rge hardware version
	int  rge_bit;						///< rge bit version
	double rge_temperature;				///< rge temperature, unit: ℃
} s5_rge_module_info_t;

/** rge list */
typedef struct s5_rge_module_list
{
	s5_rge_module_info_t* rge_modules;	///< rge info list	
	int num;							///< the number of rge info
} s5_rge_module_list_t;

/** 
 * bcc info.
 */
typedef struct s5_bcc_module_info			
{
	char bcc_name[MAX_NAME_LEN];		///< bcc name
	int  bcc_status;					///< bcc status：OK, Not plug-in = 0, Device error = 1, Invalid device = 2, or IO Error = 3	
	char bcc_model[MAX_MODEL_LEN ];		///< bcc hardware version
	double bcc_temperature;				///< bcc temperature, unit: ℃
} s5_bcc_module_info_t;

/** bcc list */
typedef struct s5_bcc_module_list
{
	s5_bcc_module_info_t* bcc_modules;	///< bcc info list 
	int num;							///< bcc info count
} s5_bcc_module_list_t;

/** 
 * power info in s5store.
 */
typedef struct s5_power_info
{
	char power_name[MAX_NAME_LEN];		///< the power name
	int  power_status;					///< power status：OK, Not plug-in = 0, Device error = 1, Invalid device = 2, or IO Error = 3	
	int	 power_input_ok;			    ///< whether the input is normal or not
	int	 power_output_ok;				///< whether the output is normal or not 
	int	 power_fan_ok;					///< whether the fan status is normal or not 
	double power_temperature1;			///< power temperature1, unit: ℃
	double power_temperature2;			///< power temperature2, unit: ℃
	double power_temperature3;			///< power temperature3, unit: ℃
	double power_output_current;		///< power output current,unit：A
	double power_input_current;			///< power input current, unit：A
	double power_output_voltage;		///< power output voltage,unit：V
	double power_input_voltage;			///< power input voltage,unit：V
	double power;						///< power, unit：W
} s5_power_info_t;

/** power list */
typedef struct s5_power_list
{
	s5_power_info_t* powers;			///< power info list	
	int num;							///< power info count
} s5_power_list_t;

/** 
 * host port info in s5store.
 */
typedef struct s5_host_port_info
{
    char name[MAX_NAME_LEN];            ///< host port name
	char ip[IPV4_ADDR_LEN];  			///< ip address
	char mac[MAC_ADDR_LEN];				///< mac address
	char mask[MASK_ADDR_LEN];			///< mask address	
	int  linked_status;					///< status：OK, Not plug-in = 0, Device error = 1, Invalid device = 2, or IO Error = 3
} s5_host_port_info_t;

/** host port list */
typedef struct s5_host_port_list
{
	s5_host_port_info_t* host_ports;	///< host port info list
	int num;							///< host port info count
} s5_host_port_list_t;

/**
 * s5store basic info.
 */
typedef struct s5_hw_basic_info
{
    char device_name[S5_HW_MAX_NAME];   ///< the device name   
    char type[S5_HW_MAX_NAME];          ///< the device type
    char producer[S5_HW_MAX_NAME];      ///< the producer of device
    char produce_date[S5_HW_MAX_NAME];  ///< production date
}s5_hw_basic_info_t;

/**
 * s5store running status info.
 */
typedef struct s5_hw_status_info
{
    char start_time[S5_HW_MAX_NAME];    ///< start-up time
    char up_time[S5_HW_MAX_NAME];       ///< running time
}s5_hw_status_info_t;

 /** 
  * the real time iops, bw and latency.
  */
typedef struct s5_realtime_statistic_info
{
	uint64_t bw;	   					///< the realtime bw. Unit: B/s
	uint64_t iops;         				///< the realtime iops
	uint64_t latency;					///< the realtime latency. Unit: us
} s5_realtime_statistic_info_t;

/** 
 * the fan speed 
 */
typedef struct s5_set_fan_speed_info
{
    uint32_t speed;        				///< the fan speed
} s5_set_fan_speed_info_t;

typedef struct s5_store_detailed_info
{
	s5_store_info_t general_info;
    s5_hw_basic_info_t  hwbasicinfo;
    s5_hw_status_info_t hwstatusinfo;
	s5_rge_module_info_t rges[MAX_RGE_CNT_PER_STORE];
	s5_host_port_info_t nics[MAX_NIC_CNT_PER_STORE];
	s5_tray_module_info_t trays[MAX_TRAY_CNT_PER_STORE];
	s5_fan_info_t fans[MAX_FAN_CNT_PER_STORE];
    s5_bcc_module_info_t bccs[MAX_BCC_CNT_PER_STORE];
    s5_power_info_t powers[MAX_POWER_CNT_PER_STORE];
	int64_t capacity_total;
	int64_t capacity_available;
}s5_store_detailed_info_t;


/** quotaset information in S5. */
typedef struct s5_quotaset
{
    char name[MAX_NAME_LEN];            ///< quotaset name
    char tenant_name[MAX_NAME_LEN];     ///< owner(tenant) name of quotaset
    int64_t iops;							///< iops quota of quotaset
    int64_t bw;							///< bandwidth quota of quotaset
} s5_quotaset_t;

/** quotasets list */
typedef struct s5_quotaset_list
{
    s5_quotaset_t* quotasets;       ///< quotasets buffer
    int num;                        ///< quotasets count
}s5_quotaset_list_t;

/** Identity information of s5 client link. */
typedef struct s5_cltlink_id
{
	char				clt_ip[IPV4_ADDR_LEN];		///< ip address of s5 client
	int					clt_port;					///< port of s5 client
	char				nic_ip[IPV4_ADDR_LEN];		///< ip address of target nic
	int					nic_port;					///< port of target nic
}s5_cltlink_id_t;

/** S5 client link information. */
typedef struct s5_client_link
{
	s5_cltlink_id_t		id;								///< identity info of s5 client link
	char				tenant_name[MAX_NAME_LEN];		///< tenant name
	char				quotaset_name[MAX_NAME_LEN];	///< quotaset name
	char				volume_name[MAX_NAME_LEN];		///< volume name
	char				snap_name[MAX_NAME_LEN];		///< snap name
	int					cid_1;							///< the first level Committed Access Rate(CAR) identity(ID) 
	uint64_t				rate_1;							///< iops quota of first level CAR
	int					cid_2;							///< the second level CAR ID
	uint64_t				rate_2;							///< iops quota of the second level CAR
	int					cid_3;							///< quota level CAR ID
	uint64_t				rate_3;							///< iops quota of the second level CAR
}s5_client_link_t;

/** s5 client link list. */
typedef struct s5_client_link_list
{
	s5_client_link_t*	clt_links;		///< client link buffer
	int					num;			///< client link count
}s5_client_link_list_t;

/**
 * Tenant context in S5.
 *
 * Only for internal use, there are tenant name and some other info like authority within it.
 */
typedef struct s5_executor_ctx
{
	char user_name[MAX_NAME_LEN];		///< tenant name
	/** password of tenant */
	char pass_wd[MAX_VERIFY_KEY_LEN];
	/**	
	 * role of admin: '1' stands for administrator, and '0' stands for tenant, 
	 */
	int role;
} s5_executor_ctx_t;

/**
 * Parent info of volume in S5.
 *
 * Only for internal use, it is different from s5bd_parent_info_t which will be released out.
 */
typedef struct s5d_parent_info
{
	uint64_t volume_id;		///< volume ID
	uint64_t snap_seq;		///< sequence number of snapshot
	uint64_t overlap;		    ///< overlap size between parent and child volume
} s5d_parent_info_t;

/**
 * Metadata information in S5.
 */
typedef struct s5bd_metadata
{
	uint64_t volume_id;
	uint64_t replica_id[MAX_REPLICA_NUM];
	uint64_t volume_size;		    ///< volume size in bytes
	uint64_t features;
	int32_t snap_seq;			    ///< snapContext-last sequence.
	int32_t replica_count;
	s5d_parent_info_t parent;	///< parent info
} s5bd_metadata_t;

/**
 * meta request type.
 */
typedef enum meta_req_type
{
	CLT_USER_LOGIN,					///< request type for user login
	CLT_TENANT_CREATE,					///< request type for tenant creation
	CLT_TENANT_DELETE,					///< request type for tenant deletion
	CLT_TENANT_UPDATE,					///< request type for tenant update
	CLT_TENANT_LIST,					///< request type for tenant list
	CLT_TENANT_STAT,					///< request type for tenant state
	
	CLT_ADMIN_CREATE,                   ///< request type for admin creation
    CLT_ADMIN_DELETE,                   ///< request type for admin deletion
	
	CLT_VOLUME_UPDATE,					///< request type for volume update
	CLT_VOLUME_STAT,					///< request type for volume state
	CLT_VOLUME_FLATTEN,					///< request type for volume flatten
	CLT_VOLUME_CREATE,					///< request type for volume creation
	CLT_VOLUME_CLONE,					///< request type for volume clone
	CLT_VOLUME_DELETE,					///< request type for volume deletion
	CLT_VOLUME_RESIZE,					///< request type for volume resizing
	CLT_VOLUME_RENAME,					///< request type for volume renaming
	CLT_VOLUME_OPEN,
	CLT_VOLUME_CLOSE,

	CLT_LIST_VOLUMES_OF_TENANT,			///< request type for listing volumes of tenant
	CLT_LIST_VOLUMES_OF_CLUSTER,		///< request type for listing volumes of entire S5

	CLT_SNAPSHOT_CREATE,				///< request type for snapshot creation
	CLT_SNAPSHOT_DELETE,				///< request type for snapshot deletion
	CLT_SNAPSHOT_PROTECT,				///< request type for snapshot protection
	CLT_SNAPSHOT_UNPROTECT,				///< request type for snapshot unprotected
	CLT_SNAPSHOT_LIST,					///< request type for snapshot list
	CLT_SNAPSHOT_STAT,					///< request type for snapshot state

	CLT_VOLUME_SNAP_ROLLBACK,			///< request type for snapshot rollback

	CLT_PARENT_STAT,					///< request type for parent state

	CLT_S5_STAT,						///< request type for S5 system state

	CLT_OVERLAP_STAT,					///< request type for overlap state

	CLT_CHILDREN_LIST,					///< request type for children list

	CLT_CLTLINK_LIST,					///< request type for listing client link of all
	CLT_CLTLINK_LIST_OF_VOLUME,			///< request type for listing client link of volume
	CLT_CLTLINK_LIST_OF_TENANT,			///< request type for listing client link of tenant

	CLT_S5STORE_ADD,					///< request type for adding a s5store into s5center
	CLT_S5STORE_LIST,					///< request type for listing s5stores of s5center
	CLT_S5STORE_STAT,					///< request type for a s5store state
	CLT_S5STORE_DELETE,					///< request type for a s5store deletion

	CLT_CONDUCTOR_STAT,					///< request type for listing conductors of s5center
	CLT_CONDUCTOR_ROLE_SET,				///< request type for setting conductor role

	CLT_FAN_LIST_OF_S5STORE,			///< request type for listing fans of s5store
	CLT_HOST_PORT_LIST_OF_S5STORE,		///< request type for listing nics of s5store
	CLT_RGE_LIST_OF_S5STORE,			///< request type for listing rge modules of s5store
	CLT_BCC_LIST_OF_S5STORE,			///< request type for listing bccs of s5store
	CLT_POWER_LIST_OF_S5STORE,			///< request type for listing powers of s5store
	CLT_TRAY_LIST_OF_S5STORE,			///< request type for listing trays of s5store
	
	CLT_OCCUPIED_SIZE_OF_VOLUME,		///< request type for getting occupied size by volume
	CLT_OCCUPIED_SIZE_OF_TENANT,        ///< request type for getting occupied size by tenant
	
	CLT_GET_REALTIME_STATISTIC_OF_S5STORE,	///< request type for getting realtime iops, bw and latency of s5store
	CLT_GET_REALTIME_STATISTIC_OF_TENANT,   ///< request type for getting realtime iops, bw and latency of tenant
	CLT_GET_REALTIME_STATISTIC_OF_VOLUME,   ///< request type for getting realtime iops, bw and latency of volume
	
	CLT_S5_POWEROFF,					    ///< request type for poweroff one s5store

	CLT_S5_SET_FAN_SPEED,			     	///< request type for setting fan speed of one s5store

	CLT_SUB_TYPE_MAX
} meta_req_type_t;

#define ALL_REPLICA 0xffffffffffffffffLL
/** 
 * Parameter definition for volume open request. 
 */
typedef struct s5_cltreq_volume_open_param
{
	name_buf_t		volume_name;
	name_buf_t		snap_name;
	name_buf_t		tenant_name;
	int				replica_ctx_id[MAX_REPLICA_NUM];
	int				nic_ip_blacklist_len;
	ipv4addr_buf_t	nic_ip_blacklist[MAX_NIC_IP_BLACKLIST_LEN];
	uint64_t replica_id;                     // ALL_REPLICA opens all replicas, replica id opens the designated replica only. id is not index.
} s5_cltreq_volume_open_param_t;

/** 
 * Parameter definition for setting conductor role  
 */
typedef struct s5_cltreq_cdt_role_set_param
{
	s5_conductor_role_t role;	///< 0 ---- slave, 1 ---- master
}s5_cltreq_cdt_role_set_param_t;

/** 
 * Parameter definition for getting s5store hardware information list.
 */
typedef struct s5_cltreq_hardware_of_s5store_list_param
{
	char s5store_name[MAX_NAME_LEN];	
}s5_cltreq_hardware_of_s5store_list_param_t;

/** 
 * Parameter definition for getting occupied size
 */
typedef struct s5_cltreq_occupied_size_param
{
    char tenant_name[MAX_NAME_LEN];
	char volume_name[MAX_NAME_LEN];
}s5_cltreq_occupied_size_param_t;

/** 
 * Parameter definition for getting realtime iops, bw and latency.
 */
typedef struct s5_cltreq_get_realtime_statistic_param
{
	char s5store_name[MAX_NAME_LEN];
    char tenant_name[MAX_NAME_LEN];
    char volume_name[MAX_NAME_LEN];
} s5_cltreq_get_realtime_statistic_param_t;

/** 
  * Parameter definition for the s5store poweroff.
  */
typedef struct s5_cltreq_poweroff_s5store_param
{
     char s5store_name[MAX_NAME_LEN];
} s5_cltreq_poweroff_s5store_param_t;

/** 
  * Parameter definition for setting the fan speed
  */
typedef struct s5_cltreq_set_fan_speed_param
{
	char s5store_name[MAX_NAME_LEN];
	uint32_t speed_rate;							///percentage * 100
} s5_cltreq_set_fan_speed_param_t;

/** 
 * Reply data definition for volume open request 
 */
typedef struct s5_cltreq_volume_open_reply
{
	int				nic_port[MAX_REPLICA_NUM];
	ipv4addr_buf_t	nic_ip[MAX_REPLICA_NUM];
	int				replica_ctx_id[MAX_REPLICA_NUM];
	s5bd_metadata_t	meta_data;
} s5_cltreq_volume_open_reply_t;

/** 
 * Parameter definition for volume close request
 */
typedef struct s5_cltreq_volume_close_param
{
	int			replica_ctx_id[MAX_REPLICA_NUM];
    char		tenant_name[MAX_NAME_LEN];
}s5_cltreq_volume_close_param_t;


/**
 * meta request for listing volumes of tenant with filter.
 */
typedef struct s5_cltreq_volume_of_tenant_list_f_param
{
	char tenant_name[MAX_NAME_LEN];				///< tenant name
	uint32_t start;								///< position of volume in result list, start from 0.
	uint32_t count;								///< the selected number of volumes in result list.
	int asc;									///< if asc is 1, volumes will be ranged in ascending order, and 0 in descending order.
} s5_cltreq_volume_of_tenant_list_f_param_t;

/**
 * meta request for creating volume
 */
typedef struct s5_cltreq_tenant_create_param
{
	int64_t volume;								///< volume quota of tenant to create
	int64_t iops;									///< iops quota of tenant to create
	int64_t cbs;									///< Committed Burst Size(CBS) quota for new tenant
	int64_t bw;									///< band-width quota of tenant to create
	char name[MAX_NAME_LEN];					///< name of tenant to create
	char pass_wd[MAX_VERIFY_KEY_LEN];			///< password of tenant to create
	/**
	 * Authority of tenant to create.
	 *
	 * Authority of tenant describes access permission level of tenant, and
	 * 0 indicates normal user, 1 indicates administrator, -1 indicates invalid tenant.
	 */
	int auth;
} s5_cltreq_tenant_create_param_t;

typedef struct s5_cltreq_admin_param
{
	char admin_name[MAX_NAME_LEN];
	char pass_wd[MAX_VERIFY_KEY_LEN];
} s5_cltreq_admin_param_t;

/**
 * meta request for tenant update
 */
typedef struct s5_cltreq_tenant_update_param
{
	char src_name[MAX_NAME_LEN];				///< original name of tenant
	int64_t volume;								///< new volume quota of tenant
	int64_t iops;									///< new iops quota of tenant
	int64_t cbs;									///< new cbs of tenant
	int64_t bw;									///< new bandwidth quota of tenant
	char name[MAX_NAME_LEN];					///< new name of tenant
	char pass_wd[MAX_VERIFY_KEY_LEN];			///< new password of tenant
	int auth;									///< new permissions of tenant
} s5_cltreq_tenant_update_param_t;

/**
 * meta request for tenant deletion
 */
typedef struct s5_cltreq_tenant_delete_param
{
	char name[MAX_NAME_LEN];					///< name of tenant to delete
} s5_cltreq_tenant_delete_param_t;

/**
 * meta request for list volumes of tenant
 */
typedef struct s5_cltreq_volume_of_tenant_list_param
{
	char tenant_name[MAX_NAME_LEN];				///< tenant name
} s5_cltreq_volume_of_tenant_list_param_t;

/**
 * meta request for list children of volume
 */
typedef struct s5_cltreq_children_list_param
{
	char volume_name[MAX_NAME_LEN];				///< parent volume name
	char snap_name[MAX_NAME_LEN];				///< snapshot name of volume
} s5_cltreq_children_list_param_t;

/**
 * meta request for volume rename
 */
typedef struct s5_cltreq_volume_rename_param
{
	char volume_name[MAX_NAME_LEN];				///< original volume name
	char tenant_name[MAX_NAME_LEN];				///< tenant of  volume
	char new_name[MAX_NAME_LEN];				///< new volume name 
} s5_cltreq_volume_rename_param_t;

/**
 * meta request for snapshot list
 */
typedef struct s5_cltreq_snap_list_param
{
	char volume_name[MAX_NAME_LEN];				///< volume name 
} s5_cltreq_snap_list_param_t;

/**
 * meta request for parent state
 */
typedef struct s5_cltreq_parent_stat_param
{
	char volume_name[MAX_NAME_LEN];				///< volume name
	char snap_name[MAX_NAME_LEN];				///< snapshot name
} s5_cltreq_parent_stat_param_t;

/**
 * meta request for volume delete
 */
typedef struct s5_cltreq_volume_delete_param
{
	char volume_name[MAX_NAME_LEN];				///< volume name 
	char tenant_name[MAX_NAME_LEN];				///< tenant name
} s5_cltreq_volume_delete_param_t;

/**
 * meta request for snapshot create and delete
 */
typedef struct s5_cltreq_snapshot_param
{
	char snap_name[MAX_NAME_LEN];		        ///< snapshot name 
	char volume_name[MAX_NAME_LEN];             ///< volume name 
} s5_cltreq_snapshot_param_t;

/**
 * meta request for volume clone
 */
typedef struct s5_cltreq_volume_clone_param
{
	char pvolume_name[MAX_NAME_LEN];			///< parent volume name 
	s5_volume_info_t cvolume_meta_info;			///< detailed info of child volume, with volume name, qos info, etc.
	char snap_name[MAX_NAME_LEN];				///< snapshot name
} s5_cltreq_volume_clone_param_t;

/**
 * meta request for volume resize
 */
typedef struct s5_cltreq_volume_resize_param
{
	char volume_name[MAX_NAME_LEN];			///< volume name
	char tenant_name[MAX_NAME_LEN];				///< volume tenant
	uint64_t resize;								///< new volume quota of volume
} s5_cltreq_volume_resize_param_t;

/**
 * meta request for volume state
 */
typedef struct s5_cltreq_volume_info_param
{
	char tenant_name[MAX_NAME_LEN];				///< tenant name
	char  volume_name[MAX_NAME_LEN];			///< volume name
	char  snap_name[MAX_NAME_LEN];				///< snapshot name
} s5_cltreq_volume_info_param_t;

/**
 * meta request for volume update
 */
typedef struct s5_cltreq_volume_update_param
{
	char src_name[MAX_NAME_LEN];				///< original volume name
	s5_volume_info_t volume_info;				///< detailed new volume information, with all information to update in it.
} s5_cltreq_volume_update_param_t;

/**
 * meta request for a s5store state
 */
typedef struct s5_cltreq_s5store_stat_param
{
    char s5store_name[MAX_S5STORE_NAME_LEN];    ///< The name of the s5store to state
} s5_cltreq_s5store_stat_param_t;

/**
 * meta request for volume overlap state
 */
typedef struct s5_cltreq_overlap_stat_param
{
	char volume_name[MAX_NAME_LEN];				///< volume name
	int64_t snap_seq;							    ///< snap sequence
} s5_cltreq_overlap_stat_param_t;

/**
 * meta request for volume flatten
 */
typedef struct s5_cltreq_volume_flatten_param
{
	char  volume_name[MAX_NAME_LEN];			///< name of volume to flatten
} s5_cltreq_volume_flatten_param_t;

/**
 * meta request for volume rollback
 */
typedef struct s5_cltreq_volume_rollback_param
{
	char  volume_name[MAX_NAME_LEN];			///< name of volume to rollback
	char  snap_name[MAX_NAME_LEN];				///< snap name of volume to rollback
} s5_cltreq_volume_rollback_param_t;

/**
 * meta request for tenant information state
 */
typedef struct s5_cltreq_tenant_stat_param
{
	char name[MAX_NAME_LEN];					///< name of tenant to state
} s5_cltreq_tenant_stat_param_t;

/**
 * meta request for tenant list
 */
typedef struct s5_cltreq_tenant_list_param
{
	uint32_t start;								///< position of tenants in result list, start from 0.
	uint32_t count;								///< the selected number of tenants in result list.
	int asc;									///< if asc is 1, tenants will be ranged in ascending order, and 0 in descending order.
} s5_cltreq_tenant_list_param_t;

/**
 * meta request for client link list
 */
typedef struct s5_cltreq_cltlink_list_param
{
	char tenant_name[MAX_NAME_LEN];				///< name of tenant, and if assigned, of which client links will be listed
	char volume_name[MAX_NAME_LEN];				///< name of volume, and if assigned, of which client links will be listed
	char snap_name[MAX_NAME_LEN];				///< snap name of volume
	char clt_ip[IPV4_ADDR_LEN];					///< ipv4 address of s5 client
	int	 clt_port;								///< link port of s5 client
} s5_cltreq_cltlink_list_param_t;

/**
 * meta request for list all volumes in S5 with filter
 */
typedef struct s5_cltreq_volume_list_f_param
{
	uint32_t start;								///< position of volume in result list, start from 0.
	uint32_t count;								///< expected number of volume in result list
	int asc;									///< if asc is 1, volume will be ranged in ascending order, and 0 in descending order.
} s5_cltreq_volume_list_f_param_t;

/**
 * meta request for adding s5store node
 */
typedef struct s5_cltreq_s5store_add_param
{
	char s5store_new_name[MAX_S5STORE_NAME_LEN]; ///< S5store new name
	char daemon_0_ip[IPV4_ADDR_LEN];		  ///< daemon 0 ip
	char daemon_1_ip[IPV4_ADDR_LEN];		  ///< daemon 1 ip
} s5_cltreq_s5store_add_param_t;

/**
 * meta request for s5store deletion
 */
typedef struct s5_cltreq_s5store_delete_param
{
	char s5store_name[MAX_S5STORE_NAME_LEN];   ///< name of the S5store to delete
} s5_cltreq_s5store_delete_param_t;
/*
*meta request for conductor info
*/
typedef struct s5_cltreq_s5_conductor_stat_param
{
	char ip[IPV4_ADDR_LEN];
}s5_cltreq_s5_conductor_stat_param_t;

/**
 * client request
 *
 * It is used to transmit all request from S5 client to s5conductor, except for io requests.
 */
typedef struct s5_client_req
{
	int sub_type;				///< client request type
	union
	{
		s5_cltreq_tenant_update_param_t				tenant_update_param;
		s5_cltreq_tenant_create_param_t				tenant_create_param;
		s5_cltreq_tenant_delete_param_t				tenant_delete_param;
		s5_cltreq_tenant_stat_param_t				tenant_stat_param;
		s5_cltreq_tenant_list_param_t				tenant_list_f_param;

		s5_cltreq_volume_of_tenant_list_param_t		volume_t_list_param;
		s5_cltreq_volume_of_tenant_list_f_param_t	volume_t_list_f_param;
		s5_cltreq_volume_list_f_param_t				volume_c_list_f_param;
		s5_cltreq_children_list_param_t				children_list_param;
		s5_cltreq_volume_rename_param_t				volume_rename_param;

		s5_cltreq_snap_list_param_t	snap_list_param;
		s5_cltreq_parent_stat_param_t	parent_stat_param;

		s5_volume_info_t			volume_create_param;
		s5_cltreq_volume_update_param_t	volume_update_param;
		s5_cltreq_volume_delete_param_t volume_delete_param;
		s5_cltreq_volume_clone_param_t volume_clone_param;
		s5_cltreq_volume_resize_param_t volume_resize_param;
		s5_cltreq_volume_info_param_t volume_stat_param;
		s5_cltreq_volume_flatten_param_t volume_flatten_param;
		s5_cltreq_volume_rollback_param_t volume_rollback_param;
		s5_cltreq_snapshot_param_t	snapshot_manage_param;
		s5_cltreq_overlap_stat_param_t overlap_stat_param;

		s5_cltreq_s5store_stat_param_t s5store_stat_param;

		s5_cltreq_cltlink_list_param_t cltlink_list_param;

		s5_cltreq_s5store_add_param_t s5store_add_param;
		s5_cltreq_s5store_delete_param_t s5store_delete_param;
		s5_cltreq_s5_conductor_stat_param_t s5_conductor_stat_param;

		s5_cltreq_cdt_role_set_param_t cdt_role_set_param;
		s5_cltreq_hardware_of_s5store_list_param_t hardware_t_list_param; ///< request parameter for fan, power, tray and so on.
		s5_cltreq_admin_param_t admin_param;							  ///< request parameter for admin creation or deletion.
		s5_cltreq_occupied_size_param_t occupied_size_param;			  ///< request parameter for volume raw capacity and occupied capacity.
		s5_cltreq_get_realtime_statistic_param_t statistic_info_param;	  ///< request parameter for getting realtime iops, bw and latency of s5store, volume or tenant.
		s5_cltreq_poweroff_s5store_param_t poweroff_s5store_param;        ///< request parameter for powering off the s5store.		
		s5_cltreq_set_fan_speed_param_t	s5_set_fan_speed_param;			  ///< request parameter for setting speed of s5store.
	} req_param;					    ///< request parameter.
	s5_executor_ctx_t executor_ctx;		///< context of executor who is the request issued from.
} s5_client_req_t;

/**
 * S5 info.
 *
 * S5 cluster capacity information
 */
typedef struct s5_info
{
	uint64_t kb; //total capacity, kb = kb_avail + kb_used
	uint64_t kb_avail;
	uint64_t kb_used;
} s5_info_t;

/**
 * meta data reply
 *
 * Use the function to transmit all replies from s5conductor to S5 client.
 */
typedef struct s5_clt_reply
{
	int sub_type;			///< client reply type
	int result;				///< result status of request process, '0' for success, otherwise for error code.
	int num;				///< for 'list' request, like tenant list, 'num' stands for objects(tenant) count.
	union
	{
		s5_tenant_t tenants[0];
		s5_volume_info_t volumes[0];
		s5_client_link_t cltlinks[0];
		s5_store_info_t s5stores[0];						///< result of s5store list request
		s5_store_detailed_info_t s5store_detailed_info[0];	///< result of s5store stat request
		s5_conductor_info_t conductors[0];
		s5_cltreq_volume_open_reply_t open_rpl_data[0];
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
	} reply_info;			///< if request processes successfully, it will store reply data; otherwise, error info will be stored in 'error_info'.
} s5_clt_reply_t;

#pragma pack()

#ifdef __cplusplus
}
#endif


#endif	/*__S5_META__*/


