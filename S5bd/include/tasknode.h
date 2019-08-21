/**
 * Copyright (C), 2014-2015.
 * @file
 * This file declares the s5 uint node, and s5 block node.
 */

#ifndef __TASKNODE_H__
#define __TASKNODE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "s5aiocompletion.h"
#include "s5imagectx.h"
#include "s5message.h"
#include "bitarray.h"
#include "s5_meta.h"

#ifndef SLOT_SIZE
/**
 * brief Macro defines the slot number
 */
#define SLOT_SIZE (S5_OBJ_LEN/LBA_LENGTH)
#endif

enum NODEFLAG
{
	//unitnode
	NODE_AIO_WRITE,
	NODE_AIO_READ,

	//blocknode
	NODE_IDLE,
	NODE_BLOCKED,
	NODE_UNBLOCKED,
};

typedef struct s5_unitnode
{
	int32  task_id;			  // Used for message header transaction id
	uint32  flag;
	uint32  nlba;
	uint32  len;				  // len = nlba * LBA_LENGTH
	uint64  ofs;
	struct s5_aiocompletion *comp;
	struct s5_volume_ctx *ictx;
	union
	{
		char *readbuf;
		const char *writedata;
	};

	s5_message_t *msg[MAX_REPLICA_NUM];
	struct s5_unitnode *next;
	struct timeval task_start;
} s5_unitnode_t;

void s5_unitnode_reset(struct s5_unitnode *unode);

typedef struct s5_unitnode_queue
{
	struct s5_unitnode *head;
	struct s5_unitnode *tail;
	int length;
} s5_unitnode_queue_t;

void s5_unitnode_queue_init(struct s5_unitnode_queue *queue);

void s5_unitnode_queue_release(struct s5_unitnode_queue *queue);

struct s5_unitnode* s5_unitnode_queue_head(struct s5_unitnode_queue *queue);

struct s5_unitnode* s5_unitnode_queue_tail(struct s5_unitnode_queue *queue);

int s5_unitnode_queue_length(struct s5_unitnode_queue  *queue);

BOOL s5_unitnode_queue_empty(struct s5_unitnode_queue *queue);

void s5_unitnode_queue_enqueue(struct s5_unitnode_queue *queue, struct s5_unitnode *unode);

struct s5_unitnode* s5_unitnode_queue_dequeue(struct s5_unitnode_queue *queue);


typedef struct s5_blocknode
{
	int32 flag;
	uint32 running_num;
	struct s5_unitnode_queue readyqueue;
	bitarray barr;
} s5_blocknode_t;

void s5_blocknode_init(struct s5_blocknode* bnode);

void s5_blocknode_release(struct s5_blocknode* bnode);

#ifdef __cplusplus
}
#endif

#endif //__TASKNODE_H__

