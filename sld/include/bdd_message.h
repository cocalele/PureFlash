/* bdd_message.h - xxxx */

/*
 * Copyright (c) 2015 NetBric Systems, Inc.
 *
 * The right to copy, distribute, modify or otherwise make use
 * of this software may be licensed only pursuant to the terms
 * of an applicable NetBric license agreement.
 */


#ifndef __BDD_MESSAGE_H__
#define __BDD_MESSAGE_H__

#ifndef _S5BD_KERNEL_
#include <stdint.h>
#endif

#ifndef MAX_NAME_LEN
#define MAX_NAME_LEN 96
#endif

#ifndef MAX_DEVICE_NAME_LEN
#define MAX_DEVICE_NAME_LEN 16
#endif

#ifndef MAX_BDD_MESSAGE_LEN
#define MAX_BDD_MESSAGE_LEN (64*1024)
#endif

#ifndef MAX_NIC_IP_BLACKLIST_LEN
#define MAX_NIC_IP_BLACKLIST_LEN 16
#endif

#ifndef MAX_CONDUCTOR_CNT
#define MAX_CONDUCTOR_CNT 2
#endif

#ifndef IPV4_ADDR_LEN
#define IPV4_ADDR_LEN 16				///< buffer length for addr of ipv4
#endif

#define VERSION_MAJOR 1
#define VERSION_MINOR 2
#define VERSION_PATCH 610
#define VERSION_NUM ((VERSION_MAJOR << 24) | (VERSION_MINOR << 16) | VERSION_PATCH)

#define BDDAEMON "/var/tmp/bddaemon"


typedef char dev_name_t[MAX_DEVICE_NAME_LEN];

typedef struct s5k_conductor_entry
{
	uint32_t index;
	uint32_t front_ip;
	uint32_t front_port;
	uint32_t spy_port;
} __attribute__((packed)) s5k_conductor_entry_t;

enum BDD_MSG_TYPE
{
    BDD_MSG_MAP = 0,
    BDD_MSG_MAP_REPLY,
    BDD_MSG_UNMAP,
    BDD_MSG_UNMAP_REPLY,
    BDD_MSG_LIST,
    BDD_MSG_LIST_REPLY
};

enum BDD_MSG_STATUS
{
    BDD_MSG_STATUS_OK = 0,
    BDD_MSG_STATUS_CONNECT_BDD_FAILED,
    BDD_MSG_STATUS_BDD_NOMEM,
    BDD_MSG_STATUS_CONNECT_CONDUCTOR_FAILED,
    BDD_MSG_STATUS_CONDUCTOR_NO_AVAILABLE_NIC_FAILED,
    BDD_MSG_STATUS_CONDUCTOR_PARSE_MSG_FAILED,
    BDD_MSG_STATUS_TOO_MANY_DEVICES,
    BDD_MSG_STATUS_DEVICE_EXISTS,
    BDD_MSG_STATUS_DEVICE_NON_EXISTS,
    BDD_MSG_STATUS_OPEN_MANAGER_DEVICE_FAILED,
    BDD_MSG_STATUS_IOCTL_FAILED,
    BDD_MSG_STATUS_CONNECT_RGE_FAILED,
    BDD_MSG_STATUS_VERSION_NOT_MATCH,
    BDD_MSG_STATUS_CONDUCTOR_CONNECTION_LOST,
    BDD_MSG_STATUS_GET_TOE_INFO_FAILED,
    BDD_MSG_STATUS_PUT_TOE_INFO_FAILED,
};

#ifdef _S5BD_KERNEL_
#else
typedef struct {
	unsigned int counter;
} atomic_t;
#endif

typedef struct device_statistics
{
	atomic_t bio_read_accepted;
	atomic_t bio_read_finished_ok;
	atomic_t bio_read_finished_error;

	atomic_t bio_write_accepted;
	atomic_t bio_write_finished_ok;
	atomic_t bio_write_finished_error;

	atomic_t sent_to_rge;
	atomic_t recv_from_rge;
	atomic_t send_list_len;
	atomic_t retry_fifo_len;
	atomic_t tid_len;
	atomic_t finish_id_len;
	atomic_t recv_timeout_conflict;
} device_statistics_t;

enum TOE_MODE
{
	TOE_MODE_DEBUG = 0,
	TOE_MODE_CONDUCTOR,
};

typedef struct debug_map_mode
{
    unsigned int toe_ip;
    unsigned int toe_port;
    unsigned long volume_size;
    unsigned long volume_id;
} __attribute__((packed)) debug_map_mode_t;

typedef struct conductor_map_mode
{
    char volume_name[MAX_NAME_LEN];
    char tenant_name[MAX_NAME_LEN];
    char snap_name[MAX_NAME_LEN];
	s5k_conductor_entry_t conductor_list[MAX_CONDUCTOR_CNT];
} __attribute__((packed)) conductor_map_mode_t;

typedef struct device_info
{
    dev_name_t dev_name;

	int toe_use_mode;
	union
	{
		struct debug_map_mode mode_debug;
		struct conductor_map_mode mode_conductor;
	};
	struct device_statistics dstat;
} __attribute__((packed)) device_info_t;

typedef struct bdd_message
{
	int status;
	int version;
	int msg_type;
	int valid_msg_len;
    int device_info_num;
	struct device_info dinfo[0];
} __attribute__((packed)) bdd_message_t;

#endif /* __BDD_MESSAGE_H__ */
