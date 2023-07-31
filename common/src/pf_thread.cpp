#include "pf_thread.h"


int spdk_thread_run(void *_arg)
{
    struct pf_thread_context *arg = (struct pf_thread_context *)_arg;
    arg->f(arg->arg);
}

void* p_thread_run(void *_arg)
{
    struct pf_thread_context *arg = (struct pf_thread_context *)_arg;
    int rc;

    arg->ret =  pthread_create(arg->t, arg->a, arg->f, arg->arg);
}


void pf_thread_create(struct pf_thread_context *tc)
{
    if (tc->spdk_engine) {
        if (tc->affinitize) {
           tc->ret = spdk_env_thread_launch_pinned(tc->core, spdk_thread_run, tc);
        }else{
            spdk_call_unaffinitized(p_thread_run, tc);
        }
    }else{
        p_thread_run(tc);
    }
}

void pf_thread_init(struct pf_thread_context *tc, void *(*f)(void *), void *arg, bool spdk_engine)
{
    tc->f = f;
    tc->arg = arg;
    tc->ret = 0;
    tc->spdk_engine = spdk_engine;
}

void pf_thread_ptreahd_att(struct pf_thread_context *tc, pthread_t *tid, pthread_attr_t *a)
{
    tc->t = tid;
    tc->a = a;
}

void pf_thread_set_aff(struct pf_thread_context *tc, uint32_t core)
{
    tc->affinitize = true;
    tc->core = core;
}