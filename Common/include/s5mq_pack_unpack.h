/*
 * =====================================================================================
 *
 *       Filename:  s5mq_pack_unpack.h
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  2015年09月28日 14时20分50秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  FanXiaoGuang (), solar_ambitious@126.com
 *   Organization:
 *
 * =====================================================================================
 */
#ifndef __S5MQ_PACK_UNPACk_H_H
#define __S5MQ_PACK_UNPACk_H_H

#include <czmq.h>
int pack_s5msg_to_zmsg(const s5_message_t *s5msg, zmsg_t *zmsg);
int unpack_zmsg_to_s5msg(zmsg_t *zmsg, s5_message_t *s5msg);

int pack_worker_to_zmsg(const worker_self_t *worker, zmsg_t *zmsg);
int unpack_zmsg_to_worker(zmsg_t *zmsg, worker_self_t *worker);

int pack_cndct_to_zmsg(const cndct_self_t *cndct, zmsg_t *zmsg);
int unpack_zmsg_to_cndct(zmsg_t *zmsg, cndct_self_t *cndct);

int pack_mqmsg_head(mq_head_t *msghead, zmsg_t *zmsg);
int unpack_mqmsg_head(zmsg_t *zmsg, mq_head_t *msghead);

#endif
