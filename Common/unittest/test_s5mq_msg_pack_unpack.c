/*
 * =====================================================================================
 *
 *       Filename:  pack_unpack_s5message.c
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  2015年09月24日 19时21分06秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  FanXiaoGuang (), solar_ambitious@126.com
 *   Organization:
 *
 * =====================================================================================
 */
#include <czmq.h>
#include "s5mq.h"
#include "s5mq_common.h"

#define FXG_DEBUG       1


typedef struct __test_data_t
{
    char name[32];
}test_data_t;
#if FXG_DEBUG
int pack_s5msg_to_zmsg(const s5_message_t *s5msg, zmsg_t *zmsg);
int unpack_zmsg_to_s5msg(zmsg_t *zmsg, s5_message_t *s5msg);
int test_pack_unpack_s5msg(void);

int pack_worker_to_zmsg(const worker_self_t *worker, zmsg_t *zmsg);
int unpack_zmsg_to_worker(zmsg_t *zmsg, worker_self_t *worker);
int test_worker_msg(void);

int pack_cndct_to_zmsg(const cndct_self_t *cndct, zmsg_t *zmsg);
int unpack_zmsg_to_cndct(zmsg_t *zmsg, cndct_self_t *cndct);
int test_cndct_msg(void);

int pack_mqmsg_head(mq_head_t *msghead, zmsg_t *zmsg);
int unpack_mqmsg_head(zmsg_t *zmsg, mq_head_t *msghead);

int test_mqmsg();
void destroy_s5msg(s5_message_t *s5msg);
#if FXG_DEBUG
int main(void)
{
    printf("ULONG_MAX:%lu.\n",ULONG_MAX);
    printf("ULLONG_MAX:%llu.\n",ULLONG_MAX);
    test_pack_unpack_s5msg();
    test_worker_msg();
    test_cndct_msg();
    test_mqmsg();
    return 0;
}
#endif
int test_pack_unpack_s5msg(void)
{
    zmsg_t *zmsg = NULL;
    s5_message_t s5msg;

    s5_message_head_t s5head = {.transaction_id = 1234, .data_len = sizeof(test_data_t)};
    s5_message_tail_t s5tail = {.flag = 1111, .crc = 222};

    test_data_t data;
    memset(&data, 0, sizeof(test_data_t));
    strcpy(data.name, "fanxiaoguang");

    printf("\n---------------test_pack_unpack_s5msg-------------------------------\n");

    s5msg.head = s5head;
    s5msg.data = &data;
    s5msg.tail = s5tail;
    printf("BEFORE:\t[head]transaction_id:%d,data_len:%d. [data]name:%s, [tail] flag:%d, crc:%d.\n",
                s5msg.head.transaction_id,s5msg.head.data_len,
                data.name,
                s5tail.flag, s5tail.crc);

    zmsg = zmsg_new();
    pack_s5msg_to_zmsg(&s5msg, zmsg);
    //zmsg_print(zmsg);

    s5_message_t recvmsg;
    zmsg_t *new_zmsg = zmsg_dup(zmsg);
    unpack_zmsg_to_s5msg(new_zmsg,&recvmsg);
    printf("AFTER:\t[head]transaction_id:%d,data_len:%d. [data]name:%s, [tail] flag:%d, crc:%d.\n",
                recvmsg.head.transaction_id,
                recvmsg.head.data_len,
                data.name,
                recvmsg.tail.flag, recvmsg.tail.crc);


    free(recvmsg.data);

    zmsg_destroy(&zmsg);
    zmsg_destroy(&new_zmsg);
    return 0;
}

#endif


int pack_s5msg_to_zmsg(const s5_message_t *s5msg, zmsg_t *zmsg)
{
    zmsg_addmem(zmsg, &s5msg->head, sizeof(s5_message_head_t));
    zmsg_addmem(zmsg, &s5msg->tail, sizeof(s5_message_tail_t));
    if (s5msg->head.data_len > 0)
    {
        zmsg_addmem(zmsg, s5msg->data, (size_t)s5msg->head.data_len);
    }

    return 0;
}

int unpack_zmsg_to_s5msg(zmsg_t *zmsg, s5_message_t *s5msg)
{
    zframe_t *frame;

    frame = zmsg_pop(zmsg);
    assert(frame);
    memcpy(&s5msg->head, (s5_message_head_t *)zframe_data(frame), sizeof(s5_message_head_t));
    zframe_destroy(&frame);

    frame = zmsg_pop(zmsg);
    assert(frame);
    memcpy(&s5msg->tail, (s5_message_tail_t *)zframe_data(frame), sizeof(s5_message_tail_t));
    zframe_destroy(&frame);

    if (s5msg->head.data_len > 0)
    {
        
        frame = zmsg_pop(zmsg);
        assert(frame);
        s5msg->data = (void *)malloc((size_t)s5msg->head.data_len);
        memcpy(s5msg->data, zframe_data(frame), (size_t)s5msg->head.data_len);
        zframe_destroy(&frame);
    }

    return 0;
}
#if FXG_DEBUG
int test_worker_msg(void)
{
    zmsg_t *zmsg = zmsg_new();
    worker_self_t worker;
    worker_self_t new_worker;

    memset(&worker, 0, sizeof(worker_self_t));
    memset(&new_worker, 0, sizeof(worker_self_t));

    printf("\n---------------test_pack_unpack_worker---------------------\n");

    strncpy(worker.worker_id, "player01", MQ_NAME_LEN - 1);
    worker.status = WORKER_ST_FREE;
    worker.lasttime = (uint64)time(NULL);

    printf("BEFORE:\tworker_id:%s, status:%d, lasttime:%llu.\n",
            worker.worker_id, worker.status, worker.lasttime);
    pack_worker_to_zmsg(&worker, zmsg);
    //zmsg_print(zmsg);
    unpack_zmsg_to_worker(zmsg, &new_worker);
    printf("AFTER:\tworker_id:%s, status:%d, lasttime:%llu.\n",
            new_worker.worker_id, new_worker.status, new_worker.lasttime);

    zmsg_destroy(&zmsg);
    return 0;
}
#endif
int pack_worker_to_zmsg(const worker_self_t *worker, zmsg_t *zmsg)
{
    int rc = OK;
    rc = zmsg_addstr(zmsg, worker->worker_id);  /* push worker_id */
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to do zmsg_addstr.");
        return ERROR;
    }
    rc = zmsg_addstrf(zmsg, "%d", worker->status); /* push worker status */
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to do zmsg_addstr.");
        return ERROR;
    }
    zmsg_addstrf(zmsg, "%llu", worker->lasttime); /* push worker lasttime */
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to do zmsg_addstrf.");
        return ERROR;
    }
    return 0;
}

int unpack_zmsg_to_worker(zmsg_t *zmsg, worker_self_t *worker)
{
    char *str = NULL;

    str = zmsg_popstr(zmsg);
    assert(str);
    strncpy(worker->worker_id, str, MQ_NAME_LEN - 1);
    free(str);
    S5LOG_DEBUG("worker_id:%s.\n", worker->worker_id);

    str = zmsg_popstr(zmsg);
    assert(str);
    worker->status = (s5_worker_status_t)atoi(str);
    free(str);
    S5LOG_DEBUG("worker status:%d.\n", worker->status);

    str = zmsg_popstr(zmsg);
    assert(str);
    worker->lasttime = strtoull(str, NULL, 10);
    free(str);
    return 0;
}

int pack_cndct_to_zmsg(const cndct_self_t *cndct, zmsg_t *zmsg)
{
    int rc = OK;
    rc = zmsg_addstr(zmsg, cndct->cndct_id);  /* push cndct_id */
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to do zmsg_addstr.");
        return ERROR;
    }
    rc = zmsg_addstrf(zmsg, "%d", cndct->status); /* push cndct status */
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to do zmsg_addstr.");
        return ERROR;
    }

    rc = zmsg_addstrf(zmsg, "%d", cndct->role); /* push cndct status */
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to do zmsg_addstr.");
        return ERROR;
    }
    rc = zmsg_addstrf(zmsg, "%llu", cndct->lasttime); /* push cndct lasttime */
    if (OK != rc)
    {
        S5LOG_ERROR("Failed to do zmsg_addstrf.");
        return ERROR;
    }
    return 0;
}

int unpack_zmsg_to_cndct(zmsg_t *zmsg, cndct_self_t *cndct)
{
    char *str = NULL;

    str = zmsg_popstr(zmsg);
    assert(str);
    strncpy(cndct->cndct_id, str, MQ_NAME_LEN - 1);
    free(str);

    S5LOG_DEBUG("cndct->cndct_id:%s.", cndct->cndct_id);

    str = zmsg_popstr(zmsg);
    assert(str);
    cndct->status = (s5_cndct_status_t)atoi(str);
    free(str);
    S5LOG_DEBUG("cndct->status:%d.", cndct->status);

    str = zmsg_popstr(zmsg);
    assert(str);
    cndct->role = (s5_conductor_role_t)atoi(str);
    free(str);
    S5LOG_DEBUG("cndct->role:%d.", cndct->role);

    str = zmsg_popstr(zmsg);
    assert(str);
    cndct->lasttime = strtoull(str, NULL, 10);
    free(str);
    S5LOG_DEBUG("cndct->lasttime:%llu.", cndct->lasttime);
    return 0;
}

#if FXG_DEBUG
int test_cndct_msg(void)
{
    zmsg_t *zmsg = zmsg_new();
    cndct_self_t cndct;
    cndct_self_t new_cndct;

    memset(&cndct, 0, sizeof(cndct_self_t));
    memset(&new_cndct, 0, sizeof(cndct_self_t));

    printf("\n-----------------test_pack_unpack_condcutor-----------------------\n");

    strncpy(cndct.cndct_id, "condcutor01", MQ_NAME_LEN - 1);
    cndct.status = CNDCT_ST_FREE;
    cndct.lasttime = (uint64)time(NULL);

    printf("BEFORE:\tworker_id:%s, status:%d, lasttime:%llu.\n",
            cndct.cndct_id, cndct.status, cndct.lasttime);
    pack_cndct_to_zmsg(&cndct, zmsg);
    //zmsg_print(zmsg);
    unpack_zmsg_to_cndct(zmsg, &new_cndct);
    printf("AFTER:\tcndct_id:%s, status:%d, lasttime:%llu.\n",
            cndct.cndct_id, cndct.status, cndct.lasttime);

    zmsg_destroy(&zmsg);
    return 0;
}
#endif
int pack_mqmsg_head(mq_head_t *msghead, zmsg_t *zmsg)
{
    zmsg_addstr(zmsg, msghead->msg_type);             /*1 msg type  */
    zmsg_addstrf(zmsg, "%llu", msghead->msgid);       /*2 msg id */
    zmsg_addstr(zmsg, msghead->sender);               /*3 msg sender */
    zmsg_addstr(zmsg, msghead->recver);               /*4 msg recver  */
    zmsg_addstrf(zmsg, "%d", msghead->is_asend);             /*4 msg recver  */
    zmsg_addstrf(zmsg, "%lu", msghead->timestamp);   /*5 msg timestamp */

    return OK;
}

int unpack_mqmsg_head(zmsg_t *zmsg, mq_head_t *msghead)
{
    char *str = NULL;
#if 0
    str = zmsg_popstr(zmsg); /* 1 msg type  */
    assert(str);
    snprintf(msghead->msg_type, MQ_NAME_LEN, "%s", str);
    free(str);
#endif

    str = zmsg_popstr(zmsg); /* 2 msg id */
    assert(str);
    msghead->msgid = strtoul(str, NULL, 10);
    free(str);

    str = zmsg_popstr(zmsg); /* 3 msg sender */
    assert(str);
    snprintf(msghead->sender, MQ_NAME_LEN - 1, "%s", str);
    free(str);

    str = zmsg_popstr(zmsg); /* 4 msg recver */
    assert(str);
    snprintf(msghead->recver, MQ_NAME_LEN - 1, "%s", str);
    free(str);

    str = zmsg_popstr(zmsg); /* 5 is_asend */
    assert(str);
    msghead->is_asend = atoi(str);
    free(str);

    str = zmsg_popstr(zmsg); /* 6 msg timestamp */
    assert(str);
    msghead->timestamp = strtoul(str, NULL,  10);
    free(str);

    return OK;
}
#if FXG_DEBUG
int test_mqmsg()
{
    s5_message_head_t s5head = {.transaction_id = 1234, .data_len = sizeof(test_data_t)};
    s5_message_tail_t s5tail = {.flag = 1111, .crc = 222};

    test_data_t data;
    memset(&data, 0, sizeof(test_data_t));
    strcpy(data.name, "fanxiaoguang");

    mqmsg_t *mqmsg = malloc(sizeof(mqmsg_t));
    assert(mqmsg);
    memset(mqmsg, 0, sizeof(mqmsg_t));
    mqmsg->usrdata.s5msg = (s5_message_t *)malloc(sizeof(s5_message_t));

    memcpy(&(mqmsg->usrdata.s5msg->head), &s5head, sizeof(s5_message_head_t));
    memcpy(&(mqmsg->usrdata.s5msg->tail), &s5tail, sizeof(s5_message_tail_t));

    mqmsg->usrdata.s5msg->data = (void *)malloc((size_t)s5head.data_len);
    memcpy(mqmsg->usrdata.s5msg->data, &data, sizeof(test_data_t));


    destroy_s5msg(mqmsg->usrdata.s5msg);
    free(mqmsg);

    return OK;
}
#endif

#if FXG_DEBUG
void destroy_s5msg(s5_message_t *s5msg)
{
    if (NULL == s5msg)
        return;
    if(NULL != s5msg->data)
    {
        free(s5msg->data);
        s5msg->data = NULL;
    }
    free(s5msg);
    s5msg = NULL;
}
#endif
