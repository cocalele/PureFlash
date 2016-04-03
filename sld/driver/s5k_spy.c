#include <linux/kthread.h>
#include <linux/socket.h>
#include <net/sock.h>
#include <linux/types.h>
#include <linux/ctype.h>
#include <asm/ioctls.h>

#include <linux/list.h>

#include "s5k_log.h"
#include "s5k_spy.h"
#include "s5k_utils.h"
 
#define free(mem)	kfree((void*)(mem))
#define malloc(len) 	kzalloc(len, GFP_KERNEL)

#ifndef S5LOG_WARN
#define S5LOG_WARN printf
#endif


#ifndef S5LOG_INFO
#define S5LOG_INFO printf
#endif

#ifndef S5LOG_ERROR
#define S5LOG_ERROR printf
#endif

#define COMMENT_LEN	512
#define RECV_BUF_LEN	1024
#define SEND_BUF_LEN	2048

static int spy_port = 8888;
struct task_struct *spy_thread_id = NULL;
int spy_exit = 0;

struct variable_info
{
	const void* var_adr;
	enum variable_type va_type;
	void* arg;
	const char *comment;
};

struct spy_list
{
	char *name;	//key
	struct variable_info vinfo;//value
	//struct spy_list *next;
	struct list_head ls_vars;
};

static struct spy_list list_var =
{
	.name = "",
	//.vinfo = {0},
	//.ls_vars.prev = &list_var.ls_vars,
	//.ls_vars.next = &list_var.ls_vars,
};


void list_push_back(struct spy_list *head, const char *name, struct variable_info *pvinfo);

void spy_register_variable(const char* name, const void* var_addr, enum variable_type var_type, const char* comment)
{
	struct variable_info info =
	{
		.var_adr = var_addr,
		.va_type = var_type,
		.arg = NULL,
		.comment = comment
	};
	//int len = min(COMMENT_LEN, (int)strlen(comment));
	//safe_strcpy(info.comment, comment, len);
	list_push_back(&list_var, name, &info);
}

void spy_register_property(const char* name,
                           property_getter get_func,
                           void* obj_to_get,
                           enum variable_type prop_type,
                           const char* comment
                          )
{
	struct variable_info info =
	{
		.var_adr = (void*)get_func,
		.va_type = prop_type,
		.arg = obj_to_get,
		.comment = comment
	};

	//int len = min(COMMENT_LEN, (int)strlen(comment));
	if (prop_type != vt_prop_int64)
	{
		return;
	}

	//safe_strcpy(info.comment, comment, len);
	list_push_back(&list_var, name, &info);
}

void list_erase(struct spy_list *which)
{
	if (NULL == which) return;

	list_del(&which->ls_vars);
	free((void*)which->name);
	which->name = NULL;
	free(which->vinfo.comment);
	which->vinfo.comment = NULL;
	free((void*)which);
}

void spy_unregister(const char* name)
{
	struct spy_list* list_find(struct spy_list * head, const char *name);
	struct spy_list *p = NULL;
	if (NULL == name)return;
	//LOG_INFO("Unregistering spy : %s ", name);
	p = list_find(&list_var, name);
	if (NULL == p)return;
	list_erase(p);
}

void list_push_back(struct spy_list *head, const char *name, struct variable_info *pvinfo)
{
	//reference declare
	struct spy_list* list_find(struct spy_list * head, const char *name);
	struct spy_list *pnew = NULL;
	struct spy_list *p = NULL;
	char *comm = NULL;
	int comm_len = 0;
	int name_len = 0;

	if (NULL == head) return;
	if (NULL == pvinfo) return;
	if (NULL == name) return;
	if (strlen(name) > STAT_VAR_LEN)
	{
		S5LOG_ERROR("WARNING : spy name '%s' too long(should not more than %d)", name, STAT_VAR_LEN);
		return;
	}

	if (NULL != (p = list_find(head, name)))
	{
		S5LOG_INFO("WARNING : spy var '%s' already exists, the old will be replace by new", name);
		//replace if exists
		list_erase(p);
	}
	if (NULL == pvinfo->comment)
	{
		comm_len = 1;
	}
	else
	{
		comm_len = strlen(pvinfo->comment) + 1;
	}
	pnew = (struct spy_list*)malloc(sizeof(struct spy_list));
	name_len = strlen(name) + 1;
	pnew->name = (char*)malloc(name_len);
	safe_strcpy(pnew->name, name, name_len);
	pnew->vinfo.var_adr = pvinfo->var_adr;
	pnew->vinfo.va_type = pvinfo->va_type;
	pnew->vinfo.arg = pvinfo->arg;

	comm = malloc(comm_len);
	safe_strcpy(comm, pvinfo->comment, comm_len);
	//comm[comm_len - 1] = 0;
	pnew->vinfo.comment = comm;

	list_add_tail(&pnew->ls_vars, &head->ls_vars);
}

struct spy_list* list_find(struct spy_list *head, const char *name)
{
	struct list_head *p = NULL;
	struct spy_list *spynode = NULL;
	if (NULL == head) return NULL;
	if (NULL == name) return NULL;

	list_for_each(p, &head->ls_vars)
	{
		spynode = list_entry(p, struct spy_list, ls_vars);
		if (spynode->name && strcmp(spynode->name, name) == 0)
		{
			return spynode;
		}
	}
	return NULL;
}

void list_clear(struct spy_list *head)
{
	struct list_head *p = NULL;
	struct list_head *save = NULL;
	struct spy_list *spynode = NULL;
	if (NULL == head) return;

	list_for_each_safe(p, save, &head->ls_vars)
	{
		spynode = list_entry(p, struct spy_list, ls_vars);
		list_erase(spynode);
	}
}

void safe_strcat(char *dst, int free_len, const char* src)
{
	char *p = NULL;
	int len = 0;
	if (!dst || free_len <= 0 || !src)
	{
		return;
	}
	len = min(free_len - 1, (int)strlen(src));
	p = dst + strlen(dst);
	strncpy(p, src, len);
	p[len] = 0;
	//LOG_INFO("strcat safe : %s", dst);
}

static void do_read(/*const*/ char *cmdline, char oss[])
{
	const char *p = NULL;
	char vname[STAT_VAR_LEN + 1] = {0};
	int len = 0;
	char value[STAT_VAR_LEN + 20] = {0};
	struct spy_list *it = NULL;
	if (NULL == cmdline) return;
	if (NULL == oss) return;
	//p = strtok(cmdline, " ");
	//while(NULL != p)
	while(NULL != (p = strsep(&cmdline, " ")))
	{
		len = strlen(p);
		if (len == 0)
		{
			continue;
		}
		else if (len > STAT_VAR_LEN)
		{
			safe_strcat(oss, SEND_BUF_LEN - strlen(oss), "TooLong");
			safe_strcat(oss, SEND_BUF_LEN - strlen(oss), " ");
			continue;
		}

		safe_strcpy(vname, p, sizeof(vname));
		it = list_find(&list_var, vname);
		if(it == NULL)
		{
			safe_strcat(oss, SEND_BUF_LEN - strlen(oss), "NotRegister");
			safe_strcat(oss, SEND_BUF_LEN - strlen(oss), " ");
			continue;
		}
		else
		{
			value[0] = 0;
			switch(it->vinfo.va_type)
			{
			case vt_int32:
				sprintf(value, "%d", *((int32_t*)it->vinfo.var_adr));
				break;
			case vt_int64:
				sprintf(value, "%lld", *((int64_t*)it->vinfo.var_adr));
				break;
			case vt_cstr:
				//snprintf(value, "%s\n", (const char*)it->vinfo.var_adr);
				value[0] = 0;
				safe_strcat(oss, SEND_BUF_LEN - strlen(oss), (const char*)it->vinfo.var_adr);
				break;
			case vt_prop_int64:
				sprintf(value, "%lld", ((property_getter)it->vinfo.var_adr)(it->vinfo.arg));
				break;
			case vt_uint32:
				sprintf(value, "%u", *((unsigned int*)it->vinfo.var_adr));
				break;
			case vt_float:
				switch (FLOAT_PRECISION)
				{
				case 10000:
					sprintf(value, "%ld.%-4ld", POINT_LEFT(it->vinfo.var_adr), POINT_RIGHT(it->vinfo.var_adr));
					break;
				case 1000:
					sprintf(value, "%ld.%-3ld", POINT_LEFT(it->vinfo.var_adr), POINT_RIGHT(it->vinfo.var_adr));
					break;
				case 100:
					sprintf(value, "%ld.%-2ld", POINT_LEFT(it->vinfo.var_adr), POINT_RIGHT(it->vinfo.var_adr));
					break;
				case 10:
					sprintf(value, "%ld.%-1ld", POINT_LEFT(it->vinfo.var_adr), POINT_RIGHT(it->vinfo.var_adr));
					break;
				default:
					sprintf(value, "%lf", *((double*)it->vinfo.var_adr));
					break;
				}
				break;
			default:
				safe_strcpy(vname, "ErrorType", sizeof(vname));
				break;
			}
			safe_strcat(oss, SEND_BUF_LEN - strlen(oss), value);
			safe_strcat(oss, SEND_BUF_LEN - strlen(oss), " ");
		}
	}
}

void reply_exit(struct socket *sock, struct msghdr *msg)
{
	struct kvec vec;
	int len = 0;
	char reply[] = "exited";
	if (!sock || !msg)
	{
		return;
	}
	vec.iov_base = (void *)reply;
	vec.iov_len = strlen(reply);
	len = kernel_sendmsg(sock, msg, &vec, 1, vec.iov_len);
	if (len <= 0)
	{
		S5LOG_ERROR("reply exit failed : %d ", len);
	}
}

void recv_and_send(struct socket *sock)
{
	char *recv_buf = malloc(RECV_BUF_LEN + 1);
	char *send_buf = malloc(SEND_BUF_LEN + 1);
	int len = 0;
	struct msghdr msg;
	struct kvec vec;
	struct sockaddr_in remote_addr;
	char *space = NULL;
	char *cmd = NULL;
	struct list_head *pos = NULL;
	struct spy_list *it = NULL;

	memset(&msg, 0, sizeof(msg));
	msg.msg_name = &remote_addr;
	msg.msg_namelen = sizeof(remote_addr);
	//non-block
	msg.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;

	if (NULL == sock)
	{
		spy_exit = 1;
		S5LOG_ERROR("argument : sock is null ");
		free(recv_buf);
		free(send_buf);
		return;
	}

	while (!kthread_should_stop())
	{
		recv_buf[0] = 0;
		send_buf[0] = 0;

		vec.iov_base = (void *)recv_buf;
		vec.iov_len = RECV_BUF_LEN;

		len = kernel_recvmsg(sock, &msg, &vec, 1, RECV_BUF_LEN, msg.msg_flags);
		if(-EAGAIN == len || -EINTR == len || 0 == len)
		{
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(HZ/4);
			continue;
		}
		else if (len < 0)
		{
			spy_exit = 1;
			break;
		}
		recv_buf[len] = 0;

		space = strchr(recv_buf, ' ');
		if (NULL == space)continue;
		*space = '\0';
		cmd = space + 1;
		if(strcmp(recv_buf, "_exit") == 0)
		{
			spy_exit = 1;
			reply_exit(sock, &msg);
			S5LOG_INFO("spy cmd : _exit");
			break;
		}
		if(strcmp(recv_buf, "read") == 0)
		{
			int end_pos = 0;
			do_read(cmd, send_buf);
			end_pos = strlen(send_buf) - 1;
			if (end_pos > 0 && isspace(send_buf[end_pos]))
			{
				send_buf[end_pos] = 0;
			}
		}
		else if(strcmp(recv_buf, "list") == 0)
		{
			list_for_each(pos, &list_var.ls_vars)
			{
				it = list_entry(pos, struct spy_list, ls_vars);
				safe_strcat(send_buf, SEND_BUF_LEN - strlen(send_buf), it->name);
				safe_strcat(send_buf, SEND_BUF_LEN - strlen(send_buf), "\t:\t");
				safe_strcat(send_buf, SEND_BUF_LEN - strlen(send_buf), it->vinfo.comment);
				safe_strcat(send_buf, SEND_BUF_LEN - strlen(send_buf), "\n");
			}
		}
		else
		{
			const char* help = "Invalid command. Commands are: read <var_name> [var_name] ...\n";
			safe_strcpy(send_buf, help, SEND_BUF_LEN + 1);
		}

		vec.iov_base = (void *)send_buf;
		vec.iov_len = strlen(send_buf);
		len = kernel_sendmsg(sock, &msg, &vec, 1, vec.iov_len);
		if(-EAGAIN == len || -EINTR == len || 0 == len)
		{
			S5LOG_ERROR("sendmsg return len=%d", len);
			schedule_timeout(100);
			continue;
		}
		else if (len < 0)
		{
			S5LOG_ERROR("Failed send reply errno: %d", len);
			spy_exit = 1;
			break;
		}
	}

	S5LOG_INFO("spy thread exit");
	free(recv_buf);
	free(send_buf);
}

static int spy_thread_proc(void* arg)
{
	//struct sigaction sa;
	struct sockaddr_in addr;
	int rc = -1;
	struct socket *sock = NULL;
	//int on = 1;

	//sa.sa_handler=SIG_IGN;
	//sigaction(SIGUSR1, &sa, NULL);

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(spy_port);
	rc = sock_create_kern(PF_INET, SOCK_DGRAM, IPPROTO_UDP, &sock);
	if (rc < 0)
	{
		spy_exit = 1;
		S5LOG_ERROR("NBMonitor failed create socket. errno: %d", rc);
		return -1;
	}

	while( (rc = kernel_bind(sock, (struct sockaddr*)&addr, sizeof(addr))) != 0 && spy_port < 65535)
	{
		if(rc == -EADDRINUSE)
		{
			spy_port ++;
			addr.sin_port = htons(spy_port);
			continue;
		}
		S5LOG_ERROR("NBMonitor failed bind to port:%d. errno: %d", spy_port, rc);
		sock_release(sock);
		spy_exit = 1;
		return -1;
	}
	S5LOG_INFO("NBMonitor bind to port:%d.\n", spy_port);
	recv_and_send(sock);
	sock_release(sock);
	return -1;
}

void spy_start(int port)
{
	//LIST_HEAD_INIT(list_var.ls_vars);
	INIT_LIST_HEAD(&list_var.ls_vars);
	spy_port = port;
	spy_thread_id = kthread_run(spy_thread_proc, NULL, "spy_thread");
	if(NULL == spy_thread_id)
	{
		S5LOG_ERROR("Failed call spy thread.");
	}
}

void spy_end(void)
{
	if (!spy_exit && NULL != spy_thread_id)
	{
		kthread_stop(spy_thread_id);
	}
	list_clear(&list_var);
}

int spy_get_port(void)
{
	return spy_port;
}

int* get_spy_port_ptr(void)
{
	return &spy_port;
}

