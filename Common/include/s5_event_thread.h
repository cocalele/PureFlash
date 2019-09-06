#ifndef s5_event_thread_h__
#define s5_event_thread_h__

class EventThread
{
public:
	EventQueue event_queue;
	char name[32];
	int start();
	virtual int process_event(int event_type, int arg_i, void* arg_p) = 0;

	static void *start_routine(void* arg);
};
#endif // s5_event_thread_h__
