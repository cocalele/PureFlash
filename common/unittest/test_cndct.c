/*
 * =====================================================================================
 *
 *       Filename:  cndct.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  2015年09月28日 16时24分12秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  FanXiaoGuang (), solar_ambitious@126.com
 *   Organization:
 *
 * =====================================================================================
 */
#include <signal.h>
#include "s5mq.h"

#define TEST_TIME               2160
#define TEST_GET_WORKER_LIST    1
#define TEST_NO_S5DATA          0

#define CONF_FILE       "./mq_cndct.conf"
#define NAME_LEN        1024

typedef struct __test_data_t
{
    char name[NAME_LEN];
}test_data_t;

typedef struct __test_private_t
{
    char name[NAME_LEN];
}test_private_t;

s5_dlist_head_t workerlist;

void my_callback(s5_message_t *outmsg, void *private, const char *sender);
void destroy_workerlist(s5_dlist_head_t *workerlist);
int get_one_worker(char *worker,  s5_dlist_head_t workerlist);
void print_worker_list(s5_dlist_head_t workerlist);
void dump(int signo);

int test_msg_send();
int test_msg_asend();
int test_msg_send_nodata();

S5LOG_INIT("test_cndct");
int main(int argc,  char *argv[])
{
    int i = 0;
    int rc = 0;
    int test_type = 0;
    notify_to_mqcluster_t notify_info;

    if(argc != 2)
    {
        printf("Invalid parameters.\n");
        return -1;
    }
    test_type = atoi(argv[1]);
    signal(SIGSEGV, &dump);

    s5list_init_head(&workerlist);
    rc = mq_clnt_ctx_init(CONF_FILE, ID_TYPE_CNDCT, "fbs_conductor_01", my_callback, "helloworld");
    if (0 != rc)
    {
        printf("Failed to do mq_clnt_ctx_init.\n");
        return -1;
    }
    printf("after init timeout :%lld.\n", time(NULL));
    notify_info.subtype = NOTIFYMQ_TYPE_SET_CONDUCTOR_ROLE;
    notify_info.notify_param.cndct_role = S5CDT_MASTER;
    mq_set_notify_info(notify_info);
    printf("after init timeout :%lld.\n", time(NULL));

    /**
     * wait 5 second for worker start 
     */
    for ( i = 0; i < 10; i++)
    {
        rc = mq_clnt_get_worker_list(&workerlist);
        if (0 == rc)
        {
            S5LOG_INFO("There is some worker online.\n");
            if (i > 5)
            {
                break;
            }
        }
        else
        {
            S5LOG_INFO("INFO: There is no worker in workerlist.\n");
        }
        sleep(1);
    }
    print_worker_list(workerlist);


    switch (test_type)
    {
        case 0:
            {
                printf("test msg send.\n");
                sleep(1);
                test_msg_send();
                break;
            }
        case 1:
            {
                printf("test msg asend.\n");
                sleep(1);
                test_msg_asend();
                printf("After test_msg_asend.\n");
                break;
            }
        case 2:
            {
                printf("test msg send without data.\n");
                sleep(1);
                test_msg_send_nodata();
                break;
            }
        default:
            {
                printf("Invaid parameters.\n");
                break;
            }

    }
    printf("Do destroy context.\n");
    mq_clnt_ctx_destroy();
    destroy_workerlist(&workerlist);

    return 0;
}

int test_msg_send()
{
    int i = 0;
    int rc = 0;
    s5_message_t recvmsg;
    s5_message_t s5msg;

    s5_message_head_t s5head = {.transaction_id = 1234, .data_len = sizeof(test_data_t)};
    s5_message_tail_t s5tail = {.flag = 1111, .crc = 222};

    test_data_t data;
    memset(&data, 0, sizeof(test_data_t));
    strcpy(data.name, "fanboshi");

    s5msg.data = malloc((size_t)s5head.data_len);
    memcpy(&s5msg.head,  &s5head,  sizeof(s5_message_head_t));
    memcpy(&s5msg.tail,  &s5tail,  sizeof(s5_message_tail_t));
    memcpy(s5msg.data,  &data,  (size_t)s5head.data_len);


    printf("before init timeout :%lld.\n", time(NULL));

    for (i = TEST_TIME; i < TEST_TIME * 2; i++)
    {
        s5msg.head.transaction_id = i;
        char worker_id[NAME_LEN] = {0};
        strcpy(worker_id, "fbs_worker_01");
        rc = mq_clnt_msg_send(&s5msg, worker_id,  &recvmsg, 0);
        if (0 != rc)
        {
            printf("Failed to do mq_clnt_msg_send.\n");
            return -1;
        }
        printf("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.\n");
        printf("recvmsg i=%d: recvmsg.head.id:%d.\n", i, recvmsg.head.transaction_id);
        printf("recvmsg i=%d: recvmsg.data.name:%s.\n", i, ((test_data_t*)recvmsg.data)->name);
        printf("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.\n");
        free(recvmsg.data);
        usleep(1000);
    }
    S5LOG_INFO("do resource free.\n");
    free(s5msg.data);
    s5msg.data = NULL;
    return 0;
}

void my_callback(s5_message_t *outmsg, void *private, const char *sender)
{
    if(NULL != private)
    {
        printf("private name:%s.\n.", ((test_private_t *)private)->name);
    }
    if(MSG_TYPE_MQCLUSTER_CHANGE == outmsg->head.msg_type)
    {
        printf("xxxxxxxxxxxxxMSG_TYPE_MQCLUSTER_CHANGExxxxxxxxx\n");
        printf("--------------------cndct callback--------------\n");
        printf("outmsg transaction_id :%d.\n",  outmsg->head.transaction_id);
        printf("outmsg:%d.\n",  outmsg->head.data_len);
        printf("outmsg:id %s.\n",  ((mqcluster_change_t *)outmsg->data)->id);
        printf("outmsg:subtype %d.\n",  ((mqcluster_change_t *)outmsg->data)->subtype);                                                                                                       
        s5msg_release_all(&outmsg);
        return;

    }

    if(outmsg->head.data_len > 0)
    {
        printf("--------      my_callback       -----------------.\n");
        printf("outmsg->head.transaction_id:%d.\n", outmsg->head.transaction_id);
        printf("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.\n");
    }
    s5msg_release_all(&outmsg);
    return;
}

void destroy_workerlist(s5_dlist_head_t *workerlist)
{
    s5_dlist_entry_t    *pos        = NULL;
    s5_dlist_entry_t    *n        = NULL;
    int                 count       = 0;
    worker_self_t       *tmp_worker  = NULL;
    S5LIST_FOR_EACH_SAFE(pos, n, count,  workerlist) 
    {
        tmp_worker = S5LIST_ENTRY(pos,  worker_self_t,  list);     
        if (NULL == tmp_worker)
        {
            printf("There is no worker in workerlist.\n");
            break;
        }
        printf("get_worker_list:worker:[%s],  status[%d]\n",  tmp_worker->worker_id,  tmp_worker->status);
        s5list_del(&(tmp_worker->list));
        free(tmp_worker);
        tmp_worker = NULL;
    }

    return;
}


void print_worker_list(s5_dlist_head_t workerlist)
{
    s5_dlist_entry_t    *pos        = NULL;
    int                 count       = 0;
    worker_self_t       *tmp_worker  = NULL;
    printf("----------------------------------------\n");
    S5LIST_FOR_EACH(pos, count,  &workerlist) 
    {
        tmp_worker = S5LIST_ENTRY(pos,  worker_self_t,  list);     
        if (NULL == tmp_worker)
            return;
        printf("worker in workerlist id is:%s.\n",tmp_worker->worker_id);
        return;
    }
    printf("----------------------------------------\n");

}
int test_msg_asend()
{
    int i = 0;
    int rc = 0;
    s5_message_t s5msg;
    s5_message_head_t s5head = {.transaction_id = 1234, .data_len = sizeof(test_data_t)};
    s5_message_tail_t s5tail = {.flag = 1111, .crc = 222};

    test_data_t data;
    memset(&data, 0, sizeof(test_data_t));
    strcpy(data.name, "fanboshi-asend");

    memcpy(&s5msg.head,  &s5head,  sizeof(s5_message_head_t));
    memcpy(&s5msg.tail,  &s5tail,  sizeof(s5_message_tail_t));
    memcpy(&s5msg.data,  &data,  sizeof(test_data_t));

    s5msg.data = malloc((size_t)s5head.data_len);

    for (i = 0; i < TEST_TIME; i++)
    {
        s5msg.head.transaction_id = i;
        rc = mq_clnt_msg_asend(&s5msg, "fbs_worker_01");
        if (0 != rc)
        {
            printf("Failed to do mq_clnt_msg_send.\n");
            return -1;
        }
    }
    sleep(20);
    free(s5msg.data);
    s5msg.data = NULL;

    mq_clnt_ctx_destroy();
    destroy_workerlist(&workerlist);
    exit(0);

    return 0;
}

int test_msg_send_nodata()
{
    int i = 0;
    int rc = 0;
    s5_message_t recvmsg;
    s5_message_t s5msg_nodata;
    s5_message_head_t s5head = {.transaction_id = 1234, .data_len = sizeof(test_data_t)};
    s5_message_tail_t s5tail = {.flag = 1111, .crc = 222};

    memcpy(&s5msg_nodata.head,  &s5head,  sizeof(s5_message_head_t));
    memcpy(&s5msg_nodata.tail,  &s5tail,  sizeof(s5_message_tail_t));
    s5msg_nodata.data = NULL;
    s5msg_nodata.head.data_len = 0;

    for (i = TEST_TIME*3; i < TEST_TIME * 4; i++)
    {
        char worker_id[NAME_LEN] = {0};
        s5msg_nodata.head.transaction_id = i;
        memset(worker_id, 0,  (size_t)MQ_NAME_LEN);
        strcpy(worker_id, "fbs_worker_01");
        printf("sendmsg:id: s5msg.head.transaction_id:%d",  s5msg_nodata.head.transaction_id);

        rc = mq_clnt_msg_send(&s5msg_nodata, worker_id,  &recvmsg, 0);
        if (0 != rc)
        {
            printf("Failed to do mq_clnt_msg_send.\n");
            return -1;
        }
        printf("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.\n");
        printf("recvmsg: recvmsg.head.id:%d.\n", recvmsg.head.transaction_id);
        printf("recvmsg: recvmsg.data.name:%s.\n", ((test_data_t*)recvmsg.data)->name);
        printf("i:%d, .\n", i);
        printf("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.\n");
        printf("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.\n");
        if(recvmsg.head.data_len >0)
        {
            free(recvmsg.data);
        }

    }

    return 0;
}
void dump(int signo)
{
    char buf[1024];
    char cmd[1024];
    FILE *fh;

    snprintf(buf, sizeof(buf), "/proc/%d/cmdline", getpid());
    if(!(fh = fopen(buf, "r")))
        exit(0);
    if(!fgets(buf, sizeof(buf), fh))
        exit(0);
    fclose(fh);
    if(buf[strlen(buf) - 1] == '\n')
        buf[strlen(buf) - 1] = '\0';
    snprintf(cmd, sizeof(cmd), "gdb %s %d", buf, getpid());
    system(cmd);
    
    exit(0);
}
