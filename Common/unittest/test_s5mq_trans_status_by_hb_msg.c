/*
 * =====================================================================================
 *
 *       Filename:  test_s5mq_trans_status_by_hb_msg.c
 *
 *    Description:  transportation status list by heartbeat message 
 *
 *        Version:  1.0
 *        Created:  2015年11月24日 11时22分13秒
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */

#include <stdio.h>
#include <stdlib.h>

#include "s5list.h"
#include "s5mq.h"
#include "s5mq_common.h"

s5_dlist_head_t  g_workerlist;
s5_dlist_head_t  newlist;


static int set_workerlist(void);
static int print_workerlist(s5_dlist_head_t *workerlist);
int pack_workerlist_to_zmsg(s5_dlist_head_t *workerlist, zmsg_t *zmsg);
int unpack_zmsg_to_workerlist(zmsg_t *zmsg, s5_dlist_head_t *workerlist);

int main(void)
{
    zmsg_t *zmsg = NULL;
    printf("do set worker list...\n");
    set_workerlist();
    printf("after set worker print it...\n");
    print_workerlist(&g_workerlist);
    printf("pack workerlist to zmsg...\n");

    zmsg = zmsg_new();
    pack_workerlist_to_zmsg(&g_workerlist,  zmsg);
    zmsg_print(zmsg);

    s5list_init_head(&newlist);
    unpack_zmsg_to_workerlist(zmsg, &newlist);
    printf("print new list...\n");
    print_workerlist(&newlist);
    return 0;
}

int set_workerlist(void)
{
    worker_self_t *tmp_worker = NULL;
    s5list_init_head(&g_workerlist);

    tmp_worker = (worker_self_t *)malloc(sizeof(worker_self_t));
    assert(tmp_worker);
    memset(tmp_worker,  0, sizeof(worker_self_t));

    strncpy(tmp_worker->worker_id, "worker01", MQ_NAME_LEN - 1);
    tmp_worker->status = WORKER_ST_FREE;
    tmp_worker->lasttime = (uint64)time(NULL);
    s5list_pushtail(&tmp_worker->list, &g_workerlist);

    tmp_worker = (worker_self_t *)malloc(sizeof(worker_self_t));
    assert(tmp_worker);
    memset(tmp_worker,  0, sizeof(worker_self_t));
    strncpy(tmp_worker->worker_id, "worker02", MQ_NAME_LEN - 1);
    tmp_worker->status = WORKER_ST_NORMAL;
    tmp_worker->lasttime = (uint64)time(NULL);
    s5list_pushtail(&tmp_worker->list, &g_workerlist);

    tmp_worker = (worker_self_t *)malloc(sizeof(worker_self_t));
    assert(tmp_worker);
    memset(tmp_worker,  0, sizeof(worker_self_t));
    strncpy(tmp_worker->worker_id, "worker03", MQ_NAME_LEN - 1);
    tmp_worker->status = WORKER_ST_BUSY;
    tmp_worker->lasttime = (uint64)time(NULL);
    s5list_pushtail(&tmp_worker->list, &g_workerlist);


    return 0;
}

int print_workerlist(s5_dlist_head_t *workerlist)
{
    worker_self_t       *tmp_worker = NULL;
    s5_dlist_entry_t    *pos;
    s5_dlist_entry_t    *n;
    int                 count = 0;

    S5LIST_FOR_EACH_SAFE(pos, n, count, workerlist)
    {
        tmp_worker = S5LIST_ENTRY(pos,worker_self_t,list);
        printf("worker id: %s, status:%d.\n", tmp_worker->worker_id, tmp_worker->status);
    }
    return 0;
}

int pack_workerlist_to_zmsg(s5_dlist_head_t *workerlist, zmsg_t *zmsg)
{
    worker_self_t       *tmp_worker = NULL;
    s5_dlist_entry_t    *pos;
    s5_dlist_entry_t    *n;
    int                 count = 0;

    s5list_lock(workerlist);
    S5LIST_FOR_EACH_SAFE(pos, n, count, workerlist)
    {
        tmp_worker = S5LIST_ENTRY(pos,worker_self_t,list);
        S5LOG_DEBUG("packing...worker id: %s, status:%d.\n", tmp_worker->worker_id, tmp_worker->status);
        zmsg_t *submsg = zmsg_new();
        assert(submsg);
        pack_worker_to_zmsg(tmp_worker,  submsg);
        zmsg_print(submsg);
        zmsg_addmsg(zmsg, &submsg);
    }
    s5list_unlock(workerlist);
    zmsg_pushstrf(zmsg,  "%d", workerlist->count);
}

int unpack_zmsg_to_workerlist(zmsg_t *zmsg, s5_dlist_head_t *workerlist)
{
    int     number  = 0;
    char    *str    = NULL;
    int     i       = 0;        /* index */
    int     count   = 0;
    int     rc      = 0;
    worker_self_t       *tmp_worker = NULL;
    s5_dlist_entry_t    *pos;
    s5_dlist_entry_t    *n;
    int     find_flag   = FALSE;

    str = zmsg_popstr(zmsg);
    assert(str);
    number = atoi(str);
    free(str);

    for (i = 0; i < number; i++)
    {
        find_flag = FALSE;
        zmsg_t *submsg = zmsg_popmsg(zmsg);
        if (NULL == submsg)
        {
            S5LOG_ERROR("Failed to do zmsg_popmsg");
            return ERROR;
        }
        worker_self_t *new_worker = malloc(sizeof(worker_self_t));
        assert(new_worker);
        memset(new_worker, 0, sizeof(worker_self_t));
        unpack_zmsg_to_worker(submsg,  new_worker);
        S5LOG_TRACE("unpacking...new worker id: %s, status:%d.\n", new_worker->worker_id, new_worker->status);
        s5list_lock(workerlist);
        S5LIST_FOR_EACH_SAFE(pos, n, count, workerlist)
        {
            tmp_worker = S5LIST_ENTRY(pos,worker_self_t,list);
            S5LOG_TRACE("unpacking...already worker id: %s, status:%d.\n", tmp_worker->worker_id, tmp_worker->status);
            if(!strcmp(tmp_worker->worker_id,  new_worker->worker_id))
            {
                tmp_worker->status = new_worker->status;
                tmp_worker->lasttime = new_worker->lasttime;
                find_flag = TRUE;
                break;
            }
        }
        s5list_unlock(workerlist);

        if(TRUE != find_flag)
        {
           rc = s5list_pushtail(&new_worker->list, workerlist);
           printf("rc:%d", rc);
           if(OK != rc)
           {
               S5LOG_ERROR("Failed to add new worker into workerlist.\n");
           }
        }
    }

    S5LIST_FOR_EACH_SAFE(pos, n, count, workerlist)
    {
        tmp_worker = S5LIST_ENTRY(pos,worker_self_t,list);
        printf("after update new worker id: %s, status:%d.\n", tmp_worker->worker_id, tmp_worker->status);
    }
}
