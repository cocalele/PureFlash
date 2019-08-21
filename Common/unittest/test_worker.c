/*
 * =====================================================================================
 *
 *       Filename:  worker.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  2015年09月28日 14时54分14秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  FanXiaoGuang (), solar_ambitious@126.com
 *   Organization:
 *
 * =====================================================================================
 */
#include "s5mq.h"
#include "s5log.h"

#define TEST_TIME   (10 * 60  * 60 * 3)             // 3 hours
#define TEST_ASEND  0
#define TEST_CONF_FILE      "./mq_worker.conf" 

int g_id = 0;

typedef struct __test_data_t
{
    char name[32];
}test_data_t;

int g_recv_flag = 0;
void my_callback(s5_message_t *outmsg, void *private, const char *sender);

S5LOG_INIT("test_worker");
int main(int argc,  char *argv[])
{
    int i = 0; 
    int rc = 0;
    int test_type = 0;
    notify_to_mqcluster_t notify_info;

    test_type = atoi(argv[1]);

    srand( (unsigned)time( NULL ) );


    g_id = 1234;
    rc = mq_clnt_ctx_init(TEST_CONF_FILE, ID_TYPE_WORKER, "fbs_worker_01", my_callback, NULL);
    if (0 != rc)
    {
        printf("Failed to do mq_clnt_ctx_init.\n");
        return -1;
    }
    printf("after init timeout :%lld.\n", time(NULL));
    notify_info.subtype = NOTIFYMQ_TYPE_SET_WORKER_STATUS;
    notify_info.notify_param.worker_status = WORKER_ST_FREE;
    mq_set_notify_info(notify_info);
    printf("after init timeout :%lld.\n", time(NULL));

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
                break;
            }
        default:
            {
                printf("Invaid parameters.\n");
                break;
            }
    }

    mq_clnt_ctx_destroy();
}

int test_msg_send()
{
    int i = 0;
    int rc = 0;
    s5_message_t s5msg;

    s5_message_head_t s5head = {.transaction_id = g_id, .data_len = sizeof(test_data_t)};
    s5_message_tail_t s5tail = {.flag = 1111, .crc = 222};

    test_data_t data;
    memset(&data, 0, sizeof(test_data_t));
    strcpy(data.name, "fanxiaoguang");

    memcpy(&s5msg.head,  &s5head,  sizeof(s5_message_head_t));
    memcpy(&s5msg.tail,  &s5tail,  sizeof(s5_message_tail_t));
    s5msg.data = malloc((size_t)(s5head.data_len));
    memcpy(s5msg.data,  &data,  (size_t)s5head.data_len);


    for (i = 0; i < TEST_TIME; i++)
    {
        usleep(100000);
        if(g_recv_flag == 0)
            continue;
        s5msg.head.transaction_id = g_id;
        printf("g_id:%d, s5msg.head.transaction_id:%d.\n", g_id, s5msg.head.transaction_id);
        rc = mq_clnt_msg_send(&s5msg, "fbs_conductor_01", NULL, 0);
        if (0 != rc)
        {
            printf("Failed to do mq_clnt_msg_send.\n");
            return -1;
        }
        g_recv_flag = 0;
    }
    free(s5msg.data);
    return 0;
}
int test_msg_asend()
{
    int i = 0;
    int rc = 0;
    s5_message_t s5msg;

    s5_message_head_t s5head = {.transaction_id = g_id, .data_len = sizeof(test_data_t)};
    s5_message_tail_t s5tail = {.flag = 1111, .crc = 222};

    test_data_t data;
    memset(&data, 0, sizeof(test_data_t));
    strcpy(data.name, "fanxiaoguang");

    memcpy(&s5msg.head,  &s5head,  sizeof(s5_message_head_t));
    memcpy(&s5msg.tail,  &s5tail,  sizeof(s5_message_tail_t));
    s5msg.data = malloc((size_t)(s5head.data_len));
    memcpy(s5msg.data,  &data,  (size_t)s5head.data_len);


    for (i = 0; i < TEST_TIME; i++)
    {
        usleep(100000);
        if(g_recv_flag == 0)
            continue;
        s5msg.head.transaction_id = g_id;
        printf("g_id:%d, s5msg.head.transaction_id:%d.\n", g_id, s5msg.head.transaction_id);
        rc = mq_clnt_msg_asend(&s5msg, "fbs_conductor_01");
        if (0 != rc)
        {
            printf("Failed to do mq_clnt_msg_send.\n");
            return -1;
        }
        g_recv_flag = 0;
    }
    return 0;
}

void my_callback(s5_message_t *outmsg, void *private, const char *sender)
{
    int sleep_time = 0;

    if(MSG_TYPE_MQCLUSTER_CHANGE == outmsg->head.msg_type)
    {
        printf("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");
        printf("--------------------worker callback--------------\n");
        printf("outmsg transaction_id :%d.\n", outmsg->head.transaction_id);
        printf("outmsg:%d.\n", outmsg->head.data_len);
        printf("outmsg:id %s.\n", ((mqcluster_change_t *)outmsg->data)->id);
        printf("outmsg:subtype %d.\n", ((mqcluster_change_t *)outmsg->data)->subtype);
        if (MQCHANGE_TYPE_RECONNECT_FAIL == ((mqcluster_change_t *)outmsg->data)->subtype)
        {
            S5LOG_ERROR("Failed to do reconnect worker will exit.");
        }
        free(outmsg->data);
        free(outmsg);
        return;

    }

    sleep_time = rand() % 10 + 1;                                                                                                                                       
    printf( "worker sleep time %d\n", sleep_time);
    sleep(sleep_time);

    printf("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");
    printf("--------------------worker callback--------------\n");
    printf("outmsg transaction_id :%d.\n", outmsg->head.transaction_id);
    printf("outmsg:data_len:%d.\n", outmsg->head.data_len);
    if (outmsg->head.data_len > 0)
    {
        printf("outmsg:%s.\n", ((test_data_t *)outmsg->data)->name);
    }
    printf("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");
    g_id = outmsg->head.transaction_id;
    g_recv_flag = 1;
    if (outmsg->head.data_len > 0)
    {
        free(outmsg->data);
    }
    free(outmsg);
    return;
}
