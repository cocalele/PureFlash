#ifndef pf_thread_h__
#define pf_thread_h__

#include <sys/eventfd.h>
#include <unistd.h>
#include "pf_utils.h"
#include "spdk/nvme.h"
#include "spdk/util.h"
#include "spdk/env.h"
#include "spdk/stdinc.h"
#include "spdk/version.h"
#include "spdk/assert.h"
#include "spdk/init.h"
#include "spdk/log.h"
#include "spdk/thread.h"
#include "spdk/trace.h"
#include "spdk/string.h"
#include "spdk/scheduler.h"
#include "spdk/rpc.h"
#include "spdk/util.h"
#include "spdk/string.h"
#include "rte_mempool.h"

/*
 * try spdk thread furture
*/

struct pf_thread_context {
    bool spdk_engine;
    void *(*f)(void *);
    void *arg;
    pthread_attr_t *a;
    pthread_t *t;
    bool affinitize;
    uint32_t core;
    int ret;
};


void pf_thread_create(struct pf_thread_context *tc);

void pf_thread_init(struct pf_thread_context *tc, void *(*f)(void *), void *arg, bool spdk_engine);

void pf_thread_ptreahd_att(struct pf_thread_context *tc, pthread_t *tid, pthread_attr_t *a);

void pf_thread_set_aff(struct pf_thread_context *tc, uint32_t core);

#endif