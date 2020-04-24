#include "pf_list.h"
#include "pf_log.h"
#include <errno.h>
#include <stdio.h>

#define		ApiSuccess	0
#define		ApiFailed	(-EINVAL)


/**
 *  add entry
 */
static void __inline __list_add_(ppf_dlist_entry_t new_entry,
			    ppf_dlist_entry_t prev,
			    ppf_dlist_entry_t next)
{
	next->prev = new_entry;
	new_entry->next = next;
	new_entry->prev = prev;
	prev->next = new_entry;
}


/**
 *  del entry
 */
static void __inline __list_del_(ppf_dlist_entry_t entry)
{
    if (entry->next == entry)
    {
        entry->head->list = NULL;
    }
    else
    {
        if (entry->head->list == entry)
        {   //del the first entry.
            entry->head->list = entry->next;
        }
        entry->next->prev = entry->prev;
        entry->prev->next = entry->next;
    }
    entry->next = (ppf_dlist_entry_t)LIST_POISON1;
    entry->prev = (ppf_dlist_entry_t)LIST_POISON2;
    
    entry->head->count--;
    entry->head = NULL;
}

/**
 *  add entry from the list-head.
 */
static void __inline __list_push_(ppf_dlist_entry_t new_entry, ppf_dlist_head_t head)
{
    if (head->list == NULL)
    {	//empty
        new_entry->next = new_entry;
		new_entry->prev = new_entry;
    }
    else
    {
		__list_add_(new_entry, head->list->prev, head->list);
    }
    head->count++;
    head->list = new_entry;
    new_entry->head = head;
}

/**
 *  add entry from list-tail.
 */
static void __inline __list_push_tail_(ppf_dlist_entry_t new_entry, ppf_dlist_head_t head)
{
    if (head->list == NULL)
    {
        new_entry->next = new_entry;
		new_entry->prev = new_entry;
        head->list = new_entry;
    }
    else
    {
        __list_add_(new_entry, head->list->prev, head->list);
    }
    head->count++;
    new_entry->head = head;
}


/**
 *  init list-head.
 */
int
s5list_init_head(ppf_dlist_head_t head)
{
    if (head)
    {
        head->count = 0;
        head->list  = NULL;

        pthread_mutex_init(&head->lock, NULL);
        pthread_cond_init(&head->cond, NULL);
		return ApiSuccess;
    }
	return ApiFailed;
}


/**
 *  
 */
int
s5list_push(ppf_dlist_entry_t new_entry, ppf_dlist_head_t head)
{
    
	// verify the param.
	if (new_entry==NULL || head==NULL)
	{
		return ApiFailed;
	}
	// the list had been in special list.
	if (new_entry->head)
	{
		return ApiFailed;
	}

    s5list_lock(head);
    __list_push_(new_entry, head);
    s5list_unlock(head);

	return ApiSuccess;
}

/**
 */
int 
s5list_pushtail(ppf_dlist_entry_t new_entry, ppf_dlist_head_t head)
{
    
	if (new_entry==NULL || head==NULL)
	{
		return ApiFailed;
	}
	if (new_entry->head)
	{
		return ApiFailed;
	}

    s5list_lock(head);
    __list_push_tail_(new_entry, head);
    s5list_unlock(head);

	return ApiSuccess;
}

int 
s5list_push_ulc(ppf_dlist_entry_t new_entry, ppf_dlist_head_t head)
{
	if (new_entry==NULL || head==NULL)
	{
		return ApiFailed;
	}
	if (new_entry->head)
	{
		return ApiFailed;
	}

    __list_push_(new_entry, head);

	return ApiSuccess;
}

int 
s5list_pushtail_ulc(ppf_dlist_entry_t new_entry, ppf_dlist_head_t head)
{
	if (new_entry==NULL || head==NULL)
	{
		return ApiFailed;
	}
	if (new_entry->head)
	{
		return ApiFailed;
	}

    __list_push_tail_(new_entry, head);

	return ApiSuccess;
}


int
s5list_insert_before( ppf_dlist_entry_t entry
					, ppf_dlist_entry_t new_entry
					, ppf_dlist_head_t head)
{
	

	if (head==NULL || entry==NULL || new_entry==NULL)
	{
		return ApiFailed;
	}
	if (entry->head != head)
	{
		return ApiFailed;
	}

	s5list_lock(head);
	
	__list_add_(new_entry, entry->prev, entry);
	head->count++;
    new_entry->head = head;

    s5list_unlock(head);

	return ApiSuccess;
}

int
s5list_insert_after( ppf_dlist_entry_t entry
					, ppf_dlist_entry_t new_entry
					, ppf_dlist_head_t head)
{
	

	if (head==NULL || entry==NULL || new_entry==NULL)
	{
		return ApiFailed;
	}
	if (entry->head != head)
	{
		return ApiFailed;
	}

	s5list_lock(head);

	__list_add_(new_entry, entry, entry->next);
	head->count++;
    new_entry->head = head;

    s5list_unlock(head);

	return ApiSuccess;
}

int
s5list_insert_before_ulc( ppf_dlist_entry_t entry
					, ppf_dlist_entry_t new_entry
					, ppf_dlist_head_t head)
{
	if (head==NULL || entry==NULL || new_entry==NULL)
	{
		return ApiFailed;
	}
	if (entry->head != head)
	{
		return ApiFailed;
	}

	__list_add_(new_entry, entry->prev, entry);
	head->count++;
    new_entry->head = head;

	return ApiSuccess;
}

int
s5list_insert_after_ulc( ppf_dlist_entry_t entry
					, ppf_dlist_entry_t new_entry
					, ppf_dlist_head_t head)
{
	if (head==NULL || entry==NULL || new_entry==NULL)
	{
		return ApiFailed;
	}
	if (entry->head != head)
	{
		return ApiFailed;
	}

	__list_add_(new_entry, entry, entry->next);
	head->count++;
    new_entry->head = head;

	return ApiSuccess;
}


ppf_dlist_entry_t 
s5list_next_ulc(ppf_dlist_head_t head, ppf_dlist_entry_t entry)
{
	if (head == NULL)
	{
		return NULL;
	}
    if (head->list == NULL)
    {
        return NULL;
    }
    if (entry == NULL)
    {
        return head->list;
    }
	// verify entry is belong to this list?
	if (entry->head == NULL || entry->head != head)
	{
		return NULL;
	}
//    if (entry->next == head->list)
//    {
//        return NULL;
//    }
    return entry->next;
}


ppf_dlist_entry_t 
s5list_next_tail_ulc(ppf_dlist_head_t head, ppf_dlist_entry_t entry)
{
	if (head==NULL)
    {
        return NULL;
    }
	if (head->list == NULL)
	{
		return NULL;
	}
    if (entry == NULL)
    {
        return head->list->prev;
    }

	if (entry->head == NULL || entry->head != head)
	{
		return NULL;
	}
//    if (entry == head->list)
//    {
//        return NULL;
//    }
    return entry->prev;
}


int 
s5list_del_withh(ppf_dlist_head_t head, ppf_dlist_entry_t entry)
{
    

	if (entry == NULL || head == NULL)
    {
        return ApiFailed;
    }

    s5list_lock(head);
	if (entry->head != head)
    {	
		S5LOG_INFO("Do s5list_del_withh, entry->head != head. entry->head=0x%p, head=0x%p", (void*)entry->head, (void*)head);
		s5list_unlock(head);

        return ApiFailed;
    }
    __list_del_(entry);
    s5list_unlock(head);

	return ApiSuccess;
}

int 
s5list_del_withh_ulc(ppf_dlist_head_t head, ppf_dlist_entry_t entry)
{
	if (entry==NULL || head==NULL)
    {
        return ApiFailed;
    }
	if (entry->head != head)
    {	
		S5LOG_INFO("Do s5list_del_withh_ulc, entry->head != head. entry->head=0x%p, head=0x%p", (void*)entry->head, (void*)head);
        return ApiFailed;
    }
    __list_del_(entry);

	return ApiSuccess;
}

int
s5list_del(ppf_dlist_entry_t entry)
{
	ppf_dlist_head_t head;
    

	if (entry == NULL || entry->head == NULL)
    {   
        return ApiFailed;
    }

	head = entry->head;

    s5list_lock(head);
    __list_del_(entry);
    s5list_unlock(head);

	return ApiSuccess;
}

int 
s5list_del_ulc(ppf_dlist_entry_t entry)
{
	if (entry==NULL || entry->head==NULL)
    {   
        return ApiFailed;
    }
    __list_del_(entry);

	return ApiSuccess;
}

int 
s5list_clear(ppf_dlist_head_t head)
{
    

	if (head == NULL)
    {   
        return ApiFailed;
    }

    s5list_lock(head);
    s5list_clear_ulc(head);
    s5list_unlock(head);

	return ApiSuccess;
}

int 
s5list_clear_ulc(ppf_dlist_head_t head)
{
	if (head == NULL)
    {   
        return ApiFailed;
    }

    while (s5list_pop_ulc(head))
    	;
    	
	return ApiSuccess;
}

ppf_dlist_entry_t 
s5list_pop(ppf_dlist_head_t head)
{
    ppf_dlist_entry_t entry;   
	if (head == NULL)
	{
		return NULL;
	}

    s5list_lock(head);
    entry = s5list_pop_ulc(head);
    s5list_unlock(head);

    return entry;
}

ppf_dlist_entry_t 
s5list_poptail(ppf_dlist_head_t head)
{
    ppf_dlist_entry_t entry;
	if (head == NULL)
	{
		return NULL;
	}

    s5list_lock(head);
    entry = s5list_poptail_ulc(head);
    s5list_unlock(head);

    return entry;
}


ppf_dlist_entry_t s5list_pop_ulc(ppf_dlist_head_t head)
{
    ppf_dlist_entry_t entry;
    if (head == NULL)
	{
		return NULL;
	}
    entry = head->list;
    if (entry != NULL)
    {
        __list_del_(entry);
    }

    return entry;
}

ppf_dlist_entry_t 
s5list_poptail_ulc(ppf_dlist_head_t head)
{
    ppf_dlist_entry_t entry;
    
    entry = s5list_next_tail_ulc(head, NULL);
    if (entry != NULL)
    {
        __list_del_(entry);
    }

    return entry;
}



