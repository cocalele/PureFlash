/*
 * =====================================================================================
 *
 *       Filename:  broker.h
 *
 *    Description:
 *
 *        Version:  1.0
 *        Created:  2015年10月08日 15时14分14秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  FanXiaoGuang (), solar_ambitious@126.com
 *   Organization:
 *
 * =====================================================================================
 */
#ifndef __MQ_BROKER_H_H
#define __MQ_BROKER_H_H
void *thread_broker(void *args);
void destroy_workerlist_ulc(pf_dlist_head_t *worker_list);

#endif
