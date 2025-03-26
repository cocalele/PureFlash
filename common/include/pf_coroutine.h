#ifndef pf_coroutine_h__
#define pf_coroutine_h__
#include <ucontext.h>
#include <functional>
class PfEventThread;

typedef void (*co_func)(void* p);
class PfRoutine {
public:
	enum State {
		READY = 1,
		RUNNING = 2,
		EXIT = 3,
	};
	ucontext_t ctx;
	PfEventThread* exe_thread;
	State state;
	std::function<void(void)> co_fn;


	~PfRoutine();
};
struct PfRoutine* co_create(PfEventThread* exe_thread, std::function<void(void)> f);
void co_yield();
void co_enter(struct PfRoutine* cor);

#endif // pf_coroutine_h__
