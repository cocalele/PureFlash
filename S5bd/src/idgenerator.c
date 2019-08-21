#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include "idgenerator.h"
#include "s5log.h"


typedef struct id_entry
{
	struct id_entry* next;
} id_entry_t;

typedef struct idmanager
{
	struct id_entry* id_pool;
	struct id_entry* id_head;
	struct id_entry* id_tail;
	size_t sz;
} idmanager_t;

int init_id_generator(size_t sz, idgenerator *idg)
{
	int ret = 0;
	int i = 0;
	struct idmanager *idm = (struct idmanager*)malloc(sizeof(struct idmanager));
	if(!idm)
	{
		ret = -ENOMEM;
		*idg = NULL;
		S5LOG_ERROR("Failed to malloc idmanager.");
		goto out;
	}

	idm->sz = sz;
	idm->id_pool = (struct id_entry*)malloc(sizeof(struct id_entry) * (sz + 1));
	if(!idm->id_pool)
	{
		ret = -ENOMEM;
		*idg = NULL;
		S5LOG_ERROR("Failde to malloc id_entry.");
		goto release_idm;
	}

	for(i = 0; i < sz; ++i)
	{
		idm->id_pool[i].next = &(idm->id_pool[i + 1]);
	}

	idm->id_head = &(idm->id_pool[0]);
	idm->id_tail = &(idm->id_pool[sz]);
	idm->id_tail->next = NULL;
	*idg = (idgenerator)idm;
	goto out;

release_idm:
	free(idm);

out:
	return ret;
}

int release_id_generator(idgenerator idg)
{
	struct idmanager *idm = NULL;
	if(idg == NULL)
	{
		return -EINVAL;
	}

	idm = (struct idmanager*)idg;
	if(idm->id_pool != NULL)
		free(idm->id_pool);
	free(idm);

	return 0;
}

int alloc_id(idgenerator idg)
{
	struct idmanager *idm = (struct idmanager*)idg;
	struct id_entry* p = NULL;
	struct id_entry* q = NULL;

	do
	{
		p = idm->id_head;
		q = idm->id_head->next;
		if(NULL == q)
			return INVALID_ID;
	}
	while(! __sync_bool_compare_and_swap(&(idm->id_head), p, q));

	int id = (int)(p - idm->id_pool);
	assert(0 <= id && id <= idm->sz);
	return id;
}

void free_id(idgenerator idg, int id)
{
	struct idmanager *idm = (struct idmanager*)idg;
	assert(0 <= id && id <= idm->sz);
	struct id_entry* q = &(idm->id_pool[id]);
	q->next = NULL;
	struct id_entry* p = idm->id_tail;
	struct id_entry* oldp = p;

	do
	{
		while(p->next != NULL)
			p = p->next;
	}
	while(! __sync_bool_compare_and_swap(&p->next, NULL, q));
	__sync_bool_compare_and_swap(&(idm->id_tail), oldp, q);
}
