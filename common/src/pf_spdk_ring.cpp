#include <sys/eventfd.h>
#include <unistd.h>

#include "pf_utils.h"
#include "pf_event_queue.h"
#include "pf_lock.h"
#include "spdk/nvme.h"
#include "spdk/util.h"
#include "spdk/env.h"
#include "spdk/stdinc.h"
#include "spdk/env.h"
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

static __thread PfSpdkQueue *tls_queue = NULL;


#define MEMPOOL_CACHE_SIZE 1024

void * pf_spdk_dma_zmalloc(size_t size, size_t align, uint64_t *phys_addr)
{
    return spdk_dma_zmalloc(size, align, phys_addr);
}

void pf_spdk_free(void *buf)
{
    spdk_dma_free(buf);
}


// mode is SPDK_RING_TYPE_MP_SC for dispather and SPDK_RING_TYPE_SP_SC for ssd
int PfSpdkQueue::init(const char* name, int size, enum spdk_ring_type mode)
{
    char mempool_name[32];
    struct pf_spdk_msg *msgs[MEMPOOL_CACHE_SIZE*2];
    int i;

    pthread_spin_init(&lock, 0);
    safe_strcpy(this->name, name, sizeof(this->name));

    messages = spdk_ring_create(mode, size, SPDK_ENV_SOCKET_ID_ANY);
    if (!messages) {
        S5LOG_ERROR("Failed create spdk ring for:%s", name);
        return -1;
    }
    snprintf(mempool_name, sizeof(mempool_name), "msgpool_%s", name);
    msg_mempool = spdk_mempool_create(mempool_name, MEMPOOL_CACHE_SIZE * 2, sizeof(pf_spdk_msg), SPDK_MEMPOOL_DEFAULT_CACHE_SIZE, SPDK_ENV_SOCKET_ID_ANY);
    if (!msg_mempool) {
        S5LOG_ERROR("Failed create spdk mempool for:%s", mempool_name);
        int err = errno;
        switch (err) {
            case ENOMEM:
                S5LOG_ERROR("Unable to create mempool: Not enough memory");
                break;
            case EINVAL:
                S5LOG_ERROR("Unable to create mempool: Invalid argument");
                break;
            default:
                S5LOG_ERROR("Unable to create mempool: Unknown error");
                break;
        }
        return -1;
    }

    int rc = spdk_mempool_get_bulk(msg_mempool, (void **)msgs, MEMPOOL_CACHE_SIZE * 2);
    if (rc == 0) {
        for(i = 0; i < MEMPOOL_CACHE_SIZE; i++) {
            SLIST_INSERT_HEAD(&this->msg_cache, msgs[i], link);
        }
        for (i = MEMPOOL_CACHE_SIZE; i < MEMPOOL_CACHE_SIZE * 2; i++) {
             SLIST_INSERT_HEAD(&this->msg_cache_locked, msgs[i], link);
        }
    }

    return 0;
}


void PfSpdkQueue::destroy()
{
    spdk_mempool_free(msg_mempool);
    spdk_ring_free(messages);
}

int PfSpdkQueue::post_event(int type, int arg_i, void* arg_p, void*)
{
    struct pf_spdk_msg *msg;
    size_t rc;
    if (tls_queue) {
        msg = SLIST_FIRST(&tls_queue->msg_cache);
        if (unlikely(msg == NULL)) {
            S5LOG_ERROR("failed to alloc msg from tls_queue's msg_cache");
            return NULL;
        }
        SLIST_REMOVE_HEAD(&tls_queue->msg_cache, link);
        msg->lock_cache_msg = false;
    } else {
        pthread_spin_lock(&lock);
        msg = SLIST_FIRST(&msg_cache_locked);
        if (unlikely(msg == NULL)) {
            S5LOG_ERROR("failed to alloc msg from msg_cache_locked");
            return NULL;
        }
        SLIST_REMOVE_HEAD(&msg_cache_locked, link);
        pthread_spin_unlock(&lock);
        msg->lock_cache_msg = true;
    }
    msg->start_time = spdk_get_ticks();
    msg->event.type = type;
    msg->event.arg_i = arg_i;
    msg->event.arg_p = arg_p;

    rc = spdk_ring_enqueue(messages, (void **)&msg, 1, NULL);
    if (rc != 1) {
        S5LOG_ERROR("failed to enqueue event");
        return -1;
    }

    return 0;
}

int PfSpdkQueue::post_event_locked(int type, int arg_i, void* arg_p)
{
    struct pf_spdk_msg *msg;
    size_t rc;

    pthread_spin_lock(&lock);
    msg = SLIST_FIRST(&msg_cache_locked);
    SLIST_REMOVE_HEAD(&msg_cache_locked, link);
    pthread_spin_unlock(&lock);
    msg->start_time = spdk_get_ticks();
    msg->event.type = type;
    msg->event.arg_i = arg_i;
    msg->event.arg_p = arg_p;
    msg->lock_cache_msg = true;
    rc = spdk_ring_enqueue(messages, (void **)&msg, 1, NULL);
    if (rc != 1){
        S5LOG_ERROR("failed to enqueue event");
        return -1;
    }

    return 0;
}

#define MSG_BATCH_SIZE 8
int PfSpdkQueue::get_events(int max_events, void **msgs)
{
    size_t count;
    //uint64_t notify = 1;

    if (max_events > 0) {
        max_events = spdk_min(max_events, MSG_BATCH_SIZE);
    }else{
        max_events = MSG_BATCH_SIZE;
    }

    #if 0
	if( unlikely(read(event_fd, &notify, sizeof(notify)) != sizeof(notify)))
	{
		S5LOG_ERROR("Failed read event fd, rc:%d", -errno);
		return -errno;
	}
    #endif

    count = spdk_ring_dequeue(messages, msgs, max_events);
    #if 0
    //busy polling mode
    if (spdk_ring_count(messages) != 0) {
        rc = write(event_fd, &notify, sizeof(notify));
    }
    #endif
    //S5LOG_ERROR("dequeu messages=====%d", count);

    return count;
}

void PfSpdkQueue::put_event(void *msg)
{
    struct pf_spdk_msg *_msg = (struct pf_spdk_msg *)msg;
    _msg->event.type = 0;
    _msg->event.arg_i = 0;
    _msg->event.arg_p = NULL;
    if (_msg->lock_cache_msg) {
        pthread_spin_lock(&lock);
        SLIST_INSERT_HEAD(&msg_cache_locked, (struct pf_spdk_msg *)msg, link);
        pthread_spin_unlock(&lock);
    } else {
        SLIST_INSERT_HEAD(&msg_cache, (struct pf_spdk_msg *)msg, link);
    }
}

void PfSpdkQueue::set_thread_queue()
{
    tls_queue = this;
}

PfSpdkQueue::PfSpdkQueue()
{
    event_fd = 0;
}

PfSpdkQueue::~PfSpdkQueue()
{
    return ;
}

int PfSpdkQueue::sync_invoke(std::function<int()> f)
{
	SyncInvokeArg  arg;
	arg.func = f;
	sem_init(&arg.sem, 0, 0);
	int rc = post_event(EVT_SYNC_INVOKE, 0, &arg, NULL);
	if (rc)
	{
		sem_destroy(&arg.sem);
		S5LOG_ERROR("Failed post EVT_SYNC_INVOKE event");
		return rc;
	}
	sem_wait(&arg.sem);
	sem_destroy(&arg.sem);
	return arg.rc;
}