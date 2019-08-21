/*
 * =====================================================================================
 *
 *       Filename:  my_s5list.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  2015年10月09日 10时39分43秒
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
#include <string.h>
#include <assert.h>

#include "s5list.h"

#define NAME_LEN    1024


typedef struct __my_self_t
{
    s5_dlist_entry_t list;
    char  id[NAME_LEN];
    int   status;
}my_self_t;

s5_dlist_head_t mylist;
s5_dlist_head_t cndctlist;

int main(void)
{

    /* init list head */
    s5list_init_head(&mylist);
    s5list_init_head(&cndctlist);

    my_self_t *tmp1 = malloc(sizeof(my_self_t));
    assert(tmp1);
    memset(tmp1,  0,  sizeof(my_self_t));
    my_self_t *tmp2 = malloc(sizeof(my_self_t));
    assert(tmp2);
    memset(tmp2,  0,  sizeof(my_self_t));
    my_self_t *tmp3 = malloc(sizeof(my_self_t));
    assert(tmp3);
    memset(tmp3,  0,  sizeof(my_self_t));

    strncpy(tmp1->id, "cnductor01",  NAME_LEN - 1);
    tmp1->status = 0;
    strncpy(tmp2->id, "cnductor02",  NAME_LEN - 1);
    tmp2->status = 1;
    strncpy(tmp3->id, "cnductor03",  NAME_LEN - 1);
    tmp3->status = 2;

    /*  for each list */
    s5_dlist_entry_t *pos;
    s5_dlist_entry_t *n;
    int count = 0;
    printf("Before add :\n");
    S5LIST_FOR_EACH(pos,  count, &mylist)
    {
        my_self_t *tmp = S5LIST_ENTRY(pos,  my_self_t,  list);
        printf("before mylist: tmp:%d,  id:%s,  status:%d.\n", count,  tmp->id,  tmp->status);
    }
    printf("--------------------------------------------------------------\n");

    /* add to list */
    s5list_pushtail_ulc(&tmp1->list,  &mylist);
    s5list_pushtail_ulc(&tmp2->list,  &mylist);
    s5list_pushtail_ulc(&tmp3->list,  &mylist);
    printf("After add:\n");
    S5LIST_FOR_EACH(pos,  count, &mylist)
    {
        my_self_t *tmp = S5LIST_ENTRY(pos,  my_self_t,  list);
        if(NULL == tmp)
            break;
        printf("mylist: tmp:%d,  id:%s,  status:%d.\n", count,  tmp->id,  tmp->status);
    }
    printf("--------------------------------------------------------------\n");

    /* move to other list */
    S5LIST_FOR_EACH_SAFE(pos, n, count, &mylist)
    {
        my_self_t *tmp = S5LIST_ENTRY(pos,  my_self_t,  list);
        if(NULL == tmp)
            break;
        printf("do delete my_list: tmp:addr:%p,  pos addr:%p", tmp,  pos);
        printf("do_delete my_list tmp:%d,  id:%s,  status:%d.\n", count,  tmp->id,  tmp->status);
        s5list_del(&tmp->list);
        free(tmp);
        //s5list_pushtail(&tmp->list,  &cndctlist);
    }
    printf("--------------------------------------------------------------\n");
    printf("After delete,.\n");
    S5LIST_FOR_EACH(pos,  count, &mylist)
    {
        my_self_t *tmp = S5LIST_ENTRY(pos,  my_self_t,  list);
        if(NULL == tmp)
            break;
        printf("mylist: tmp:%d,  id:%s,  status:%d.\n", count,  tmp->id,  tmp->status);
    }
    printf("--------------------------------------------------------------\n");

#if 0
    S5LIST_FOR_EACH(pos,  count, &mylist)
    {
        my_self_t *tmp = S5LIST_ENTRY(pos,  my_self_t,  list);
        if(NULL == tmp)
            break;
        printf("mylist: tmp:%d,  id:%s,  status:%d.\n", count,  tmp->id,  tmp->status);
    }

    S5LIST_FOR_EACH(pos,  count, &cndctlist)
    {
        my_self_t *tmp = S5LIST_ENTRY(pos,  my_self_t,  list);
        if(NULL == tmp)
            break;
        printf("cndctlist: tmp:%d,  id:%s,  status:%d.\n", count,  tmp->id,  tmp->status);
    }
#endif

    return 0;
}
