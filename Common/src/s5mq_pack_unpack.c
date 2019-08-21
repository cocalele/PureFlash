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
#include "s5mq.h"
#include "s5mq_common.h"

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
    zmsg_addstrf(zmsg, "%lu", worker->lasttime); /* push worker lasttime */
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

    str = zmsg_popstr(zmsg);
    assert(str);
    worker->status = (s5_worker_status_t)atoi(str);
    free(str);

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
    rc = zmsg_addstrf(zmsg, "%lu", cndct->lasttime); /* push cndct lasttime */
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


    str = zmsg_popstr(zmsg);
    assert(str);
    cndct->status = (s5_cndct_status_t)atoi(str);
    free(str);

    str = zmsg_popstr(zmsg);
    assert(str);
    cndct->role = (s5_conductor_role_t)atoi(str);
    free(str);

    str = zmsg_popstr(zmsg);
    assert(str);
    cndct->lasttime = strtoull(str, NULL, 10);
    free(str);
    return 0;
}

int pack_mqmsg_head(mq_head_t *msghead, zmsg_t *zmsg)
{
    zmsg_addstr(zmsg, msghead->msg_type);             /*1 msg type  */
    zmsg_addstrf(zmsg, "%lu", msghead->msgid);       /*2 msg id */
    zmsg_addstr(zmsg, msghead->sender);               /*3 msg sender */
    zmsg_addstr(zmsg, msghead->recver);               /*4 msg recver  */
    zmsg_addstrf(zmsg, "%d", msghead->is_asend);             /*4 msg recver  */
    zmsg_addstrf(zmsg, "%lu", msghead->timestamp);   /*5 msg timestamp */

    return OK;
}

int unpack_mqmsg_head(zmsg_t *zmsg, mq_head_t *msghead)
{
    char *str = NULL;

    str = zmsg_popstr(zmsg); /* 2 msg id */
    assert(str);
    msghead->msgid = strtoul(str, NULL, 10);
    free(str);

    str = zmsg_popstr(zmsg); /* 3 msg sender */
    assert(str);
    snprintf(msghead->sender, MQ_NAME_LEN, "%s", str);
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
