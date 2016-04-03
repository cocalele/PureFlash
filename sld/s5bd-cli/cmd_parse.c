#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <ctype.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stddef.h>
#include "bdd_message.h"

#define FILE_LOCK_NAME "/var/tmp/s5bdlk"

#define		MAX_DEVICE_NUM		64
#define		MAX_DEV_LEN		16
#define		MAX_IP_LENGTH		16
#define		MAX_SUFFIX		65536
#define		PATH_MAX		4096
#define		SEC_LEN			64

#define		g_conductor_ip_key	"front_ip"
#define		g_conductor_section	"conductor."
#define		g_conductor_front_port	"front_port"
#define		g_conductor_spy_port	"monitor_port"

#define		_device_name		"device_name"
#define		_devicde_id		"device_id"
#define		_daemon_ip		"front_ip"
#define		_volume_name		"volume_name"
#define		_s5_io_depth		"s5_io_depth"
#define		_rge_io_depth		"rge_io_depth"
#define		_rge_io_max_lbas	"rge_io_max_lbas"
#define		S5BD			"s5bd"
#define		DEVICENAME		"s5bd"
#define		DEF_S5CONF		"/etc/s5/s5.conf"
#define		RGE_PORT_BASE	0xC00A

#define		safe_strcpy(dest,src,dest_size)		\
do{							\
	strncpy(dest,src,dest_size);			\
	if(dest_size>0)					\
		dest[dest_size-1]='\0';			\
}while(0)

#define	ALIGN_UP(x)	((((unsigned int)x)+4096-1)&~(4096-1))
#define	ALIGN_DOWN(x)	((((unsigned int)x))&~(4096-1))

extern void *conf_open(const char *conf_name);
extern const char *conf_get(void *conf, const char *section, const char *key);
extern int conf_get_int(void *conf, const char *section, const char *key, int *value);
extern int conf_close(void *conf);

enum RETVALUE
{
	ERRNO_POOLNOEXISTS  = -2,
	ERRNO_OK = 0,
	ERRNO_INVALARGS,
	ERRNO_TOOMANYDEVICE,
	ERRNO_NAMETOOLONG,
	ERRNO_NOMEM,
	ERRNO_DEVICEEXISTS,
	ERRNO_DEVICENOEXISTS,
	ERRNO_CONNECT_NIC_FAILED,
	ERRNO_CONNECT_STATUS_OK
};

struct device_id
{
	int id;
	long volume_id;
	char volume_name[MAX_NAME_LEN];
	char tenant_name[MAX_NAME_LEN];
};

struct add_device_args
{
	char daemon_ip[MAX_IP_LENGTH];
	int daemon_port;
	int snap_seq;
	struct device_id dev_id;
	char conf_file[PATH_MAX];
	char s5bd_path[PATH_MAX];
	int s5_io_depth;
	int rge_io_depth;
	int rge_io_max_lbas;
	int volume_ctx_id;
	int dev_name_suffix;
	int nic_ip_blacklist_len;
	unsigned int toe_ip;
	unsigned int toe_port;
	unsigned long volume_id;
	unsigned long volume_size;
	char snap_name[MAX_NAME_LEN];
	dev_name_t dev_name;
	char nic_ip_blacklist[MAX_NIC_IP_BLACKLIST_LEN][IPV4_ADDR_LEN];
	struct s5k_conductor_entry conductor_list[MAX_CONDUCTOR_CNT];
	enum RETVALUE val;
};

struct del_device_args
{
	char daemon_ip[MAX_IP_LENGTH];
	char snap_name[MAX_NAME_LEN];
	int snap_seq;
	int daemon_port;
	int volume_ctx_id;
	unsigned int toe_ip;
	unsigned int toe_port;
	unsigned long volume_id;
	unsigned long volume_size;
	struct device_id dev_id;
	dev_name_t dev_name;
	struct s5k_conductor_entry conductor_list[MAX_CONDUCTOR_CNT];
	enum RETVALUE val;
};

struct list_device_args
{
	struct device_id dev_id[MAX_DEVICE_NUM];
	int dev_name_suffix[MAX_DEVICE_NUM];
	enum RETVALUE val;
};

struct list_device_info
{
	struct device_info *dev_info;
	struct list_device_info *next, *prev;
};

struct option long_options[]=
{
	{"tenant_name", 1, NULL, 't'},
	{"volume_name", 1, NULL, 'v'},
	{"conf_file", 1, NULL, 'c'},
	{"help", 1, NULL, 'h'},
	{"dev_name", 1, NULL, 'n'},
	{"all", 1, NULL, 'a'},
	{"toe_ip", 1, NULL, 'p'},
	{"toe_port", 1, NULL, 'o'},
	{"volume_id", 1, NULL, 'i'},
	{"volume_size", 1, NULL, 's'},
	{"io-depth", 1, NULL, 'd'},
	{NULL, 0, NULL, 0}
};


void help_map()
{
	printf("For map: two modes:1. conductor mode; 2. debug mode.\n");
	printf("Select one mode from configure file or input manual\n");

	printf("\nConductor mode:\n");
	printf("map -t <tenant_name> -v <volume_name> [-n <dev_name>] [-c <conf_file>]\n");
	printf("<tenant_name> : tenant name\n");
	printf("<volume_name> : volume name\n");
	printf("<dev_name> : device name\n");
	printf("<conf_file> : S5 system configuration file, default is %s\n", DEF_S5CONF);

	printf("\nDebug mode:\n");
	printf("map --toe_ip <toe_ip> --toe_port <toe_port> "
			"--volume_id <volume_id> --volume_size <volume_size(M)> [--dev_name <dev_name>]\n");
	printf("<toe_ip> : toe ip address\n");
	printf("<toe_port> : toe port\n");
	printf("<volume_id> : volume id\n");
	printf("<volume_size> : volume size\n");
	printf("<dev_name> : device name\n");
}

void help_unmap()
{
	printf("For unmap\n");
	printf("unmap -t <tenant_name> -v <volume_name> -c <conf_file> or \n");
	printf("unmap -n <dev_name>\n");
	printf("<tenant_name> : tenant name\n");
	printf("<volume_name> : volume name\n");
	printf("<dev_name> : device name\n");
}

void help_show_operations()
{
	printf("use 'map' to map device.\n");
	printf("use 'unmap' to unmap device.\n");
	printf("use 'list' to show list of added devices.\n");
	printf("use '-h' to get help.\n");
}

const char *get_curr_proc()
{
	static char buf[PATH_MAX+1] = {0};
	char *begin;
	buf[0] = 0;
	if(readlink("/proc/self/exe", buf, PATH_MAX) <= 0)
		return "";
	begin = strrchr(buf, '/');
	if(begin == NULL)
		return "";
	return begin + 1;
}

void help_list()
{
	printf("For list\n");
	printf("%s list\n", get_curr_proc());
}

void print_reply_status(int status)
{
	switch(status)
	{
	case BDD_MSG_STATUS_OK:
		printf("Status : OK\n");
		break;
	case BDD_MSG_STATUS_CONNECT_BDD_FAILED:
		printf("Status : connect bdd failed\n");
		break;
	case BDD_MSG_STATUS_BDD_NOMEM:
		printf("Status : bdd no mem\n");
		break;
	case BDD_MSG_STATUS_CONNECT_CONDUCTOR_FAILED:
		printf("Status: connect conductor failed\n");
		break;
	case BDD_MSG_STATUS_CONDUCTOR_NO_AVAILABLE_NIC_FAILED:
		printf("Status : conductor message status failed\n");
		break;
	case BDD_MSG_STATUS_CONDUCTOR_PARSE_MSG_FAILED:
		printf("Status : conductor parse message failed\n");
		break;
	case BDD_MSG_STATUS_DEVICE_EXISTS:
		printf("Status : device exist\n");
		break;
	case BDD_MSG_STATUS_DEVICE_NON_EXISTS:
		printf("Status : device no exist\n");
		break;
	case BDD_MSG_STATUS_OPEN_MANAGER_DEVICE_FAILED:
		printf("Status : open manager device failed\n");
		break;
	case BDD_MSG_STATUS_IOCTL_FAILED:
		printf("Status : ioctl failed\n");
		break;
	case BDD_MSG_STATUS_CONNECT_RGE_FAILED:
		printf("Status : connect rge failed\n");
		break;
	default:
		printf("Error Number : %d.\n", status);
		break;
	}
}

int check_conf_file(const char *conf)
{
	if(-1 == access(conf, F_OK))
	{
		printf("conf file %s no find.\n", conf);
		return -1;
	}
	return 0;
}

int check_string_param(const char *pname, const char *pvalue, int max_len)
{
	if(!pvalue || strlen(pvalue)==0)
	{
		printf("%s name is empty.\n", pname);
		return -1;
	}
	if(strlen(pvalue) > (size_t)max_len)
	{
		printf("parameter %s too long, max len is %d.\n", pname, max_len);
		return -1;
	}
	if(isdigit(pvalue[0]))
	{
		printf("%s with value '%s', must not begin with digital.\n", pname, pvalue);
		return -1;
	}

	return 0;
}

static int verify_devname(const char *dev_name)
{
	char s5bd_path[PATH_MAX];
	snprintf(s5bd_path, PATH_MAX, "/dev/%s", dev_name);
	if (access(s5bd_path, F_OK) == 0)
	{
		return -1;
	}
	return 0;
}

static int get_devname_suffix()
{
    int i = 0;
    static char dev_name[MAX_DEV_LEN];
    char s5bd_path[PATH_MAX];
    for (i=0; i<MAX_SUFFIX; ++i)
    {
        snprintf(dev_name, MAX_DEV_LEN , "s5bd%d", i);
        snprintf(s5bd_path, PATH_MAX, "/dev/%s", dev_name);
        if (access(s5bd_path, F_OK) != 0)
        {
            return i;
        }
    }
    return -1;
}

int fill_add_args_by_conf(const char *conf, struct add_device_args *add_args)
{
	int val = 0;
	int nret = 0;
	void *cf = NULL;
	if(!conf || !add_args)
		return -1;
	cf = conf_open(conf);
	if(cf==NULL)
		return -1;
	nret = conf_get_int(cf, S5BD, _s5_io_depth, &val);
	if(nret != 0)
		goto failed;
	add_args->s5_io_depth = val;
	nret = conf_get_int(cf, S5BD, _rge_io_depth, &val);
	if(nret != 0)
		goto failed;
	add_args->rge_io_depth = val;
	nret = conf_get_int(cf, S5BD, _rge_io_max_lbas, &val);
	if(nret != 0)
		goto failed;
	add_args->rge_io_max_lbas = val;
	conf_close(cf);
	return 0;

failed:
	conf_close(cf);
	return -1;
}

int get_s5bd_path(char *path, size_t len)
{
	ssize_t i;
	ssize_t cnt = readlink("/proc/self/exe", path, len);
	if(cnt<0 || cnt>=(ssize_t)len)
		return -EINVAL;
	for(i=cnt; i>=0; --i)
	{
		if(path[i] == '/')
		{
			path[i+1] = '\0';
			break;
		}
	}
	return 0;
}

int parse_s5config_for_conductor_info(const char *s5conf, struct add_device_args *add_args)
{
	void *s5_conf_obj=NULL;
	int rc = 0;
	int conductor_port = -1;
	int conductor_spy_port = -1;
	int buf_offset = 0;
	const int sec_len = SEC_LEN;
	char conductor_section[SEC_LEN] = {0};
	const char *conductor_ip = NULL;
	unsigned int index = 0;
	unsigned int conductor_cnt = 0;
	struct s5k_conductor_entry *conductor_entry = NULL;

	if(!s5conf || !add_args)
		return -1;
	s5_conf_obj = conf_open(s5conf);
	if(s5_conf_obj == NULL)
		return -1;

	while(1)
	{
		if(index >= MAX_CONDUCTOR_CNT)
			break;
		buf_offset = snprintf(conductor_section, sec_len, "%s%d", (char *)g_conductor_section, index);
		conductor_section[buf_offset] = 0;
		conductor_ip = conf_get(s5_conf_obj, conductor_section, g_conductor_ip_key);
		if(!conductor_ip)
		{
			index++;
			continue;
		}
		if(strlen(conductor_ip) >= IPV4_ADDR_LEN)
		{
			rc = -517;
			return rc;
		}
		rc = conf_get_int(s5_conf_obj, conductor_section, (char *)g_conductor_front_port, &conductor_port);
		if(rc)
		{
			rc = -517;
			return rc;
		}
		rc = conf_get_int(s5_conf_obj, conductor_section, (char *)g_conductor_spy_port, &conductor_spy_port);
		if(rc)
		{
			rc = -517;
			return rc;
		}
		conductor_entry = &add_args->conductor_list[index];
		conductor_entry->front_ip = inet_addr(conductor_ip);
		conductor_entry->front_port = (unsigned int)conductor_port;
		conductor_entry->spy_port = (unsigned int)conductor_spy_port;
		conductor_entry->index = index++;
		conductor_cnt++;
	}

	if(conductor_cnt <= 0)
	{
		rc = -517;
		return rc;
	}
	return rc;
}

#define	FLAG_V	(1<<0)
#define	FLAG_C	(1<<1)
#define	FLAG_T	(1<<2)
#define	FLAG_N	(1<<3)
#define	FLAG_P	(1<<4)
#define	FLAG_O	(1<<5)
#define	FLAG_I	(1<<6)
#define	FLAG_S	(1<<7)

static char msg_buf[MAX_BDD_MESSAGE_LEN];
static int create_client_socket(const char *dst_name)
{
	int retval;
	int sockfd;
	socklen_t addr_len;
	struct sockaddr_un dst_un;

	retval = 0;
	if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return -1;

	memset((void *)&dst_un, 0, sizeof(dst_un));
	dst_un.sun_family = AF_UNIX;
	sprintf(dst_un.sun_path, dst_name);

	addr_len = offsetof(struct sockaddr_un, sun_path) + strlen(dst_name);
	int ret = connect(sockfd, (struct sockaddr*)&dst_un, addr_len);
	if (ret < 0)
	{
		printf("connect failed ret(%d)", ret);
		retval = -2;
		goto err;
	}
	return sockfd;

err:
	close(sockfd);
	return retval;
}


int map_dev(int argc, char *argv[])
{
	unsigned char flag = 0, input_flag = 0;
//	char *print_name[3], *print_title_name[3];
	int c = 0;
	int nret = 0;
	int sockfd;
	struct add_device_args add_args;

	struct bdd_message *bdd_msg;
	struct device_info *dev_info;

	memset((void *)&add_args, 0, sizeof(struct add_device_args));
	add_args.volume_ctx_id = -1;
	add_args.nic_ip_blacklist_len = 0;
	add_args.dev_id.id = -1;
	add_args.toe_ip = -1;

	while((c = getopt_long(argc-1, argv+1, "v:c:t:n:p:o:i:s:h", long_options, NULL)) != -1)
	{
		flag = 1;
		switch(c)
		{
		case 'v':
			if(check_string_param("v", optarg, sizeof(add_args.dev_id.volume_name)-1))
				return -1;
			input_flag |= FLAG_V;
			safe_strcpy(add_args.dev_id.volume_name, optarg, sizeof(add_args.dev_id.volume_name));
			break;
		case 'c':
			if(check_conf_file(optarg) || check_string_param("c", optarg, sizeof(add_args.conf_file)-1))
				return -1;
			input_flag |= FLAG_C;
			safe_strcpy(add_args.conf_file, optarg, sizeof(add_args.conf_file));
			break;
		case 't':
			if(check_string_param("t", optarg, sizeof(add_args.dev_id.tenant_name)-1))
				return -1;
			input_flag |= FLAG_T;
			safe_strcpy(add_args.dev_id.tenant_name, optarg, sizeof(add_args.dev_id.tenant_name));
			break;
		case 'n':
			if(check_string_param("n", optarg, sizeof(add_args.dev_name)-1))
				return -1;
			input_flag |= FLAG_N;
			safe_strcpy(add_args.dev_name, optarg, sizeof(add_args.dev_name));
			break;
		case 'p':
			input_flag |= FLAG_P;
			add_args.toe_ip = inet_addr(optarg);
			break;
		case 'o':
			input_flag |= FLAG_O;
			sscanf(optarg, "%d", &add_args.toe_port);
			add_args.toe_port += RGE_PORT_BASE;
			break;
		case 'i':
			input_flag |= FLAG_I;
			sscanf(optarg, "%lu", &add_args.volume_id);
			break;
		case 's':
			input_flag |= FLAG_S;
			sscanf(optarg, "%lu", &add_args.volume_size);
			add_args.volume_size *= 1048576;//M
			break;
		case 'h':
			help_map();
			return 0;
		default:
			help_map();
			return -1;
		}
	}
	if(flag==0)
	{
		help_map();
		return -1;
	}
	if(((input_flag&0xf0)==0) && ((input_flag&FLAG_V)==0 || (input_flag&FLAG_T)==0))
	{
		help_map();
		return -1;
	}
	if((input_flag&FLAG_C)&&(input_flag&0xf0))
	{
		add_args.toe_ip = -1;
		help_map();
		return -1;
	}
	if(((input_flag&FLAG_C)==0) && ((input_flag&0xf0)==0))
	{
		if(check_conf_file(DEF_S5CONF) == 0)
			safe_strcpy(add_args.conf_file, DEF_S5CONF, sizeof(add_args.conf_file));
		else
			return -1;
	}

#if 0
	if((input_flag&FLAG_C)==0 && (input_flag&0xf0) != 0xf0)
	{
		help_map();
		return -1;
	}
#endif

	if((input_flag&FLAG_N)==0)
	{
		snprintf(add_args.dev_name, MAX_DEVICE_NAME_LEN, "s5bd%d", get_devname_suffix());
	}
	else
	{
		if(verify_devname(add_args.dev_name) == -1)
		{
			printf("Device /dev/%s exists, please change a device name.\n", add_args.dev_name);
			return -1;
		}
	}

	if((input_flag&0xf0) == 0)
	{
		if((nret = fill_add_args_by_conf(add_args.conf_file, &add_args)) != 0)
			goto laybel_err0;
		if((nret = get_s5bd_path(add_args.s5bd_path, PATH_MAX-1)) != 0)
			goto laybel_err0;
		if((nret = parse_s5config_for_conductor_info(add_args.conf_file, &add_args)) != 0)
			goto laybel_err0;
	}

//	args_print(&add_args);

	sockfd = create_client_socket(BDDAEMON);
	if(sockfd < 0)
	{
		nret = -1;
		printf("Failed to create client socket\n");
		goto laybel_err0;
	}

	memset(msg_buf, 0, MAX_BDD_MESSAGE_LEN);
	bdd_msg = (struct bdd_message*)msg_buf;
	dev_info = (struct device_info *)(msg_buf + sizeof(struct bdd_message));

	safe_strcpy(dev_info->mode_conductor.volume_name, add_args.dev_id.volume_name, sizeof(add_args.dev_id.volume_name));
	safe_strcpy(dev_info->mode_conductor.tenant_name, add_args.dev_id.tenant_name, sizeof(add_args.dev_id.tenant_name));
	safe_strcpy(dev_info->dev_name, add_args.dev_name, sizeof(add_args.dev_name));
//	dev_info->map_param.toe_ip = add_args.toe_ip;
	if(add_args.toe_ip != -1)
	{
		dev_info->toe_use_mode = TOE_MODE_DEBUG;
		dev_info->mode_debug.toe_ip = add_args.toe_ip;
		dev_info->mode_debug.toe_port = add_args.toe_port;
		dev_info->mode_debug.volume_id = add_args.volume_id;
		dev_info->mode_debug.volume_size = add_args.volume_size;
	}
	else
	{
		dev_info->toe_use_mode = TOE_MODE_CONDUCTOR;
		memcpy(dev_info->mode_conductor.conductor_list, add_args.conductor_list,
				MAX_CONDUCTOR_CNT * sizeof(struct s5k_conductor_entry));
	}
	printf("It takes time to finish, depending on network status.\n");

	bdd_msg->msg_type = BDD_MSG_MAP;
	bdd_msg->version = VERSION_NUM;
	bdd_msg->valid_msg_len = sizeof(struct device_info);
	bdd_msg->device_info_num = 1;

	ssize_t count = send(sockfd, msg_buf, MAX_BDD_MESSAGE_LEN, MSG_WAITALL);

	if(count != MAX_BDD_MESSAGE_LEN)
	{
		nret = -3;
		printf("Failed to send.\n");
		goto laybel_err1;
	}

	memset(msg_buf, 0, MAX_BDD_MESSAGE_LEN);
	count = recv(sockfd, msg_buf, MAX_BDD_MESSAGE_LEN, MSG_WAITALL);
	if(count != MAX_BDD_MESSAGE_LEN)
	{
		nret = -4;
		printf("Failed to read socket\n");
		goto laybel_err1;
	}

	if(bdd_msg->version != VERSION_NUM)
	{
		printf("s5bd version(%d.%d.%d) does not match bdd version(%d.%d.%d)",
				VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH,
				bdd_msg->version>>24, bdd_msg->version>>16 & 0xff,
				bdd_msg->version & 0xffff);
		nret = bdd_msg->status;
		goto laybel_err1;
	}

#if 0
	print_title_name[0] = "tenant_name";
	print_title_name[1] = "volume_name";
	print_title_name[2] = "device_name";
	print_name[0] = &(dev_info->tenant_name[0]);
	print_name[1] = &(dev_info->volume_name[0]);
	print_name[2] = &(dev_info->dev_name[0]);
	print_title(3, print_title_name);
	print_content(3, print_name);
#endif

	print_reply_status(bdd_msg->status);
	if(bdd_msg->status == 0)
	{
		switch(dev_info->toe_use_mode)
		{
			case TOE_MODE_DEBUG:
				printf("debug mode.\n");
				printf("device_name:%s\n", dev_info->dev_name);
				break;
			case TOE_MODE_CONDUCTOR:
				printf("conductor mode.\n");
				printf("tenant_name:%s\n", dev_info->mode_conductor.tenant_name);
				printf("volume_name:%s\n", dev_info->mode_conductor.volume_name);
				printf("device_name:%s\n", dev_info->dev_name);
				break;
			default:
				printf("error mode: %d.\n", dev_info->toe_use_mode);
				break;
		}
	}

	close(sockfd);
	return bdd_msg->status;

laybel_err1:
	close(sockfd);
laybel_err0:
	return nret;
}

int unmap_dev(int argc, char *argv[])
{
	int sockfd;
	int c = 0;
	int nret = -1;
	unsigned char flag=0, t_ok=0, v_ok=0, n_ok = 0;
	struct del_device_args rm_args;

	struct bdd_message *bdd_msg;
	struct device_info *dev_info;

	memset((void *)&rm_args, 0, sizeof(struct del_device_args));
	while((c = getopt_long(argc-1, argv+1, "t:v:n:h", long_options, NULL)) != -1)
	{
		flag = 1;
		switch(c)
		{
		case 't':
			if(check_string_param("t", optarg, sizeof(rm_args.dev_id.tenant_name)-1))
				return -1;
			t_ok = 1;
			safe_strcpy(rm_args.dev_id.tenant_name, optarg, sizeof(rm_args.dev_id.tenant_name));
			break;
		case 'v':
			if(check_string_param("v", optarg, sizeof(rm_args.dev_id.volume_name)-1))
				return -1;
			v_ok = 1;
			safe_strcpy(rm_args.dev_id.volume_name, optarg, sizeof(rm_args.dev_id.volume_name));
			break;
		case 'n':
			if(check_string_param("n", optarg, sizeof(rm_args.dev_name)-1))
				return -1;
			n_ok = 1;
			safe_strcpy(rm_args.dev_name, optarg, sizeof(rm_args.dev_name));
			break;
		case 'h':
			help_unmap();
			break;
		default:
			help_unmap();
			return -1;
		}
	}
	if(flag==0)
	{
		help_unmap();
		return -1;
	}
	if((n_ok==0) && (v_ok==0 || t_ok==0))
	{
		help_unmap();
		return -1;
	}

	if(n_ok==1 && v_ok==1 && t_ok ==1)
	{
		help_unmap();
		return -1;
	}

	sockfd = create_client_socket(BDDAEMON);
	if(sockfd < 0)
	{
		nret = -1;
		goto laybel_err0;
	}

	memset(msg_buf, 0, MAX_BDD_MESSAGE_LEN);
	bdd_msg = (struct bdd_message*)msg_buf;
	dev_info = (struct device_info *)(msg_buf + sizeof(struct bdd_message));

	safe_strcpy(dev_info->mode_conductor.volume_name,
			rm_args.dev_id.volume_name, sizeof(rm_args.dev_id.volume_name));
	safe_strcpy(dev_info->mode_conductor.tenant_name,
			rm_args.dev_id.tenant_name, sizeof(rm_args.dev_id.tenant_name));
	safe_strcpy(dev_info->dev_name, rm_args.dev_name, sizeof(rm_args.dev_name));

	bdd_msg->msg_type = BDD_MSG_UNMAP;
	bdd_msg->version = VERSION_NUM;
	bdd_msg->valid_msg_len = sizeof(struct device_info);
	bdd_msg->device_info_num = 1;

	ssize_t count = send(sockfd, msg_buf, MAX_BDD_MESSAGE_LEN, MSG_WAITALL);

	if(count != MAX_BDD_MESSAGE_LEN)
	{
		nret = -2;
		goto laybel_err1;
	}

	memset(bdd_msg, 0, MAX_BDD_MESSAGE_LEN);
	count = recv(sockfd, msg_buf, MAX_BDD_MESSAGE_LEN, MSG_WAITALL);
	if(count != MAX_BDD_MESSAGE_LEN)
	{
		nret = -3;
		goto laybel_err1;
	}

	if(bdd_msg->version != VERSION_NUM)
	{
		printf("s5bd version(%d.%d.%d) does not match bdd version(%d.%d.%d)",
				VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH,
				bdd_msg->version>>24, bdd_msg->version>>16 & 0xff,
				bdd_msg->version & 0xffff);
		nret = bdd_msg->status;
		goto laybel_err1;
	}

	print_reply_status(bdd_msg->status);
	if(bdd_msg->status == 0)
	{
		switch(dev_info->toe_use_mode)
		{
			case TOE_MODE_DEBUG:
				printf("debug mode.\n");
				printf("device_name:%s\n", dev_info->dev_name);
				break;
			case TOE_MODE_CONDUCTOR:
				printf("conductor mode.\n");
				printf("tenant_name:%s\n", dev_info->mode_conductor.tenant_name);
				printf("volume_name:%s\n", dev_info->mode_conductor.volume_name);
				printf("device_name:%s\n", dev_info->dev_name);
				break;
			default:
				printf("error mode: %d.\n", dev_info->toe_use_mode);
				break;
		}
	}

	close(sockfd);
	return bdd_msg->status;

laybel_err1:
	close(sockfd);
laybel_err0:
	return nret;
}

int list_dev(int argc, char *argv[])
{
	int i, c, nret;
	int sockfd;
	struct bdd_message *bdd_msg;
	struct device_info *dev_info;
	struct device_info *dinfo;
	struct list_device_info *list_info_head, *list_info_entry, *list_info_tmp;
	while((c = getopt_long(argc-1, argv+1, "ha", long_options, NULL)) != -1)
	{
		switch(c)
		{
		case 'a':
			break;
		default:
			help_list();
			return -1;
		}
	}

	sockfd = create_client_socket(BDDAEMON);
	if(sockfd < 0)
	{
		printf("Failed to create client socket. ret(%d)\n", sockfd);
		return -2;
	}

	memset(msg_buf, 0, MAX_BDD_MESSAGE_LEN);
	bdd_msg = (struct bdd_message*)msg_buf;
	dev_info = (struct device_info *)(msg_buf + sizeof(struct bdd_message));

	bdd_msg->msg_type = BDD_MSG_LIST;
	bdd_msg->version = VERSION_NUM;

	ssize_t count = send(sockfd, msg_buf, MAX_BDD_MESSAGE_LEN, MSG_WAITALL);

	if(count != MAX_BDD_MESSAGE_LEN)
	{
		nret = -3;
		goto laybel_err1;
	}

	memset(bdd_msg, 0, MAX_BDD_MESSAGE_LEN);
	count = recv(sockfd, msg_buf, MAX_BDD_MESSAGE_LEN, MSG_WAITALL);

	if(count != MAX_BDD_MESSAGE_LEN)
	{
		nret = -4;
		goto laybel_err1;
	}

	if(bdd_msg->version != VERSION_NUM)
	{
		printf("s5bd version(%d.%d.%d) does not match bdd version(%d.%d.%d)",
				VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH,
				bdd_msg->version>>24, bdd_msg->version>>16 & 0xff,
				bdd_msg->version & 0xffff);
		nret = bdd_msg->status;
		goto laybel_err1;
	}

	printf("device info num:%d\n", bdd_msg->device_info_num);
	printf("------------------\n");
	dev_info = (struct device_info *)(msg_buf + sizeof(struct bdd_message));

	list_info_head = malloc(sizeof(struct list_device_info));
	list_info_head->prev = list_info_head->next = list_info_head;

	for(i=0; i<bdd_msg->device_info_num; i++)
	{
		list_info_tmp = malloc(sizeof(struct list_device_info));
		list_info_tmp->dev_info = &dev_info[i];
		list_info_entry = list_info_head->next;
		while((list_info_entry != list_info_head)
			&&(strcmp(list_info_tmp->dev_info->dev_name, list_info_entry->dev_info->dev_name) > 0))
			list_info_entry = list_info_entry->next;
		list_info_tmp->prev = list_info_entry->prev;
		list_info_tmp->next = list_info_entry;
		list_info_tmp->prev->next = list_info_tmp;
		list_info_tmp->next->prev = list_info_tmp;
	}

	i = 0;
	list_info_entry = list_info_head->next;
	while(list_info_entry != list_info_head)
	{
		dinfo = list_info_entry->dev_info;
		switch(dinfo->toe_use_mode)
		{
			case TOE_MODE_DEBUG:
				printf("debug mode.\n");
				printf("device_name:%s\n", dinfo->dev_name);
				break;
			case TOE_MODE_CONDUCTOR:
				printf("conductor mode.\n");
				printf("tenant_name:%s\n", dinfo->mode_conductor.tenant_name);
				printf("volume_name:%s\n", dinfo->mode_conductor.volume_name);
				printf("device_name:%s\n", dinfo->dev_name);
				break;
			default:
				printf("error mode: %d.\n", dinfo->toe_use_mode);
				break;
		}
		printf("read:accepted(%u)-finished(%u, ok:%u, err:%u).\n",
				dinfo->dstat.bio_read_accepted.counter,
				dinfo->dstat.bio_read_finished_ok.counter +
				dinfo->dstat.bio_read_finished_error.counter,
				dinfo->dstat.bio_read_finished_ok.counter,
				dinfo->dstat.bio_read_finished_error.counter);
		printf("write:accepted(%u)-finished(%u, ok:%u, err:%u).\n",
				dinfo->dstat.bio_write_accepted.counter,
				dinfo->dstat.bio_write_finished_ok.counter +
				dinfo->dstat.bio_write_finished_error.counter,
				dinfo->dstat.bio_write_finished_ok.counter,
				dinfo->dstat.bio_write_finished_error.counter);
		printf("sent_to_rge(%u)-recv_from_rge(%u)-sent_list_len(%u)-\n"
				"retry_fifo_len(%u)-tid_len(%u)-finish_id_len(%u)"
				"recv_timeout_conflict(%u).\n",
				dinfo->dstat.sent_to_rge.counter,
				dinfo->dstat.recv_from_rge.counter,
				dinfo->dstat.send_list_len.counter,
				dinfo->dstat.retry_fifo_len.counter,
				dinfo->dstat.tid_len.counter,
				dinfo->dstat.finish_id_len.counter,
				dinfo->dstat.recv_timeout_conflict.counter);
		printf("------------------\n");

		/*
		int j;
		printf("conductor_list:\n");
		for(j=0; j<MAX_CONDUCTOR_CNT; j++)
		{
			printf("index:%08d, front_ip:0x%08x, front_port:%08d, spy_port:%08d\n",
			list_info_entry->dev_info->conductor_list[j].index,
			list_info_entry->dev_info->conductor_list[j].front_ip,
			list_info_entry->dev_info->conductor_list[j].front_port,
			list_info_entry->dev_info->conductor_list[j].spy_port);
		}
		*/
		list_info_entry = list_info_entry->next;
	}

	list_info_tmp = list_info_entry = list_info_head->next;
	while(list_info_entry != list_info_head)
	{
		list_info_entry = list_info_entry->next;
		free(list_info_tmp);
		list_info_tmp = list_info_entry;
	}
	free(list_info_head);

#if 0
	for(i=0; i<bdd_msg->device_info_num; i++)
	{
		printf("bdd_msg conductor_index: %04d\n", i);
		printf("volume_name:%s\n", dev_info[i].volume_name);
		printf("tenant_name:%s\n", dev_info[i].tenant_name);
		printf("snap_name:%s\n", dev_info[i].snap_name);
		printf("dev_name:%s\n", dev_info[i].dev_name);
		printf("conductor_list:\n");
		for(j=0; j<MAX_CONDUCTOR_CNT; j++)
		{
			printf("index:%08d, front_ip:0x%08x, front_port:%08d, spy_port:%08d\n",
			dev_info[i].conductor_list[j].index,
			dev_info[i].conductor_list[j].front_ip,
			dev_info[i].conductor_list[j].front_port,
			dev_info[i].conductor_list[j].spy_port);
		}
	}
#endif
	close(sockfd);
	return 0;
laybel_err1:
	close(sockfd);
	return nret;
}

int main(int argc, char *argv[])
{
	int ret = 0;
	int fd = open(FILE_LOCK_NAME, O_WRONLY|O_CREAT);

	if(argc < 2)
	{
		help_show_operations();
		close(fd);
		return -1;
	}
	flock(fd, LOCK_EX);

	if(strcmp(argv[1], "map") == 0)
		ret = map_dev(argc, argv);
	else if(strcmp(argv[1], "unmap") == 0)
		ret = unmap_dev(argc, argv);
	else if(strcmp(argv[1], "list") == 0)
		ret = list_dev(argc, argv);
	else
		help_show_operations();

	flock(fd, LOCK_UN);
	close(fd);

	return ret;
}
