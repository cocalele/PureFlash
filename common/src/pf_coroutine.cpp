#include <stdio.h>
#include <errno.h>
#include "pf_coroutine.h"
#include "pf_utils.h"
#include "pf_log.h"
#include "pf_event_thread.h"
void co_yield();
static void _co_wrap(void* p)
{
	PfRoutine* co = (PfRoutine*)p;
	co->co_fn();
	fprintf(stderr, "coroutine exited\n");
	co->state = PfRoutine::State::EXIT;
	co_yield();
	fprintf(stderr, "ERROR: call into exited coroutine\n");
}

struct PfRoutine* co_create(PfEventThread* exe_thread, std::function<void(void)>  f)
{
	Cleaner _c;
	PfRoutine* co = new PfRoutine;
	_c.push_back([co](){delete co;});
	co->co_fn = f;
	co->exe_thread = exe_thread;
	ucontext_t* ctx = &co->ctx;
	int stack_sz = 64 << 10;
	void* stack1 = aligned_alloc(4096, stack_sz);
	if (stack1 == NULL) {
		S5LOG_ERROR("Failed to alloc coroutine stack");
		return NULL;
	}
	_c.push_back([stack1]() {free(stack1); });
	int rc = exe_thread->sync_invoke([ctx, stack1, f, stack_sz, co]()->int{
		if (getcontext(ctx) == -1){
			S5LOG_ERROR("Failed getcontext, rc:%d", errno);
			return -errno;
		}
		ctx->uc_stack.ss_sp = stack1;
		ctx->uc_stack.ss_size = stack_sz;
		ctx->uc_link = NULL; //&main_co_uctx;

		makecontext(ctx, (void (*)(void))_co_wrap, 1, co);
		return 0;
	});
	if(rc){
		return NULL;
	}
	_c.cancel_all();
	return co;
}
PfRoutine::~PfRoutine(){
	free (ctx.uc_stack.ss_sp);
}

__thread PfEventThread* current_evt_thread;
__thread ucontext_t main_co_uctx;
__thread PfRoutine* current_co;


int _pf_co_enter(PfRoutine* co) //called by PfEventThread
{
	current_co = co;
	int rc = swapcontext(&main_co_uctx, &co->ctx);
	if(rc == -1){
		S5LOG_ERROR("Failed to swapcontext(enter), rc:%d", errno);
		return rc;
	}
	//run to here after co_yield
	return 0;
}
void co_yield() {
	ucontext_t *current_ctx = &current_co->ctx;
	current_co = NULL;
	//fprintf(stderr, "yield from %p to %p\n", old, current_ctx);
	int rc = swapcontext(current_ctx, &main_co_uctx);
	if (rc == -1) {
		S5LOG_ERROR("Failed to swapcontext(yield), rc:%d", errno);
		return ;
	}
	//fprintf(stderr, "swapcontext(yield), return:rc=%d\n", rc);
	//		setcontext(&jump_buffer0);
}
void co_enter(PfRoutine* co) {

	int rc = co->exe_thread->event_queue->post_event(EVT_CO_ENTER, 0, co);
	if(rc){
		S5LOG_ERROR("Failed to post event EVT_CO_ENTER, rc:%d" , rc);
	}

}
