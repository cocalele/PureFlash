#include <semaphore.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <string.h>
#include <sys/prctl.h>

#include "pf_poller.h"
#include "pf_log.h"

PfPoller::PfPoller() :epfd(0),tid(0),max_fd(0)
{

}
PfPoller::~PfPoller()
{
	destroy();
}
static void on_ctl_event(int fd, uint32_t event, void* user_arg)
{
	PfPoller* poller = (PfPoller*)user_arg;
	PfFixedSizeQueue<S5Event>* q;
	if (poller->ctrl_queue.get_events(&q) == 0 && q != NULL)
	{
		while(!q->is_empty())
		{
			S5Event evt = q->dequeue();
			switch (evt.type)
			{

			case EVT_SYNC_INVOKE:
			{
				struct SyncInvokeArg* arg = (struct SyncInvokeArg*)evt.arg_p;
				arg->rc = arg->func();
				sem_post(&arg->sem);
				break;
			}
			default:
				S5LOG_ERROR( "Poller get unknown event type:%d", evt.type);
			}
		}

	}
}

int PfPoller::init(const char* name, int max_fd_count)
{
	int rc = 0;
	safe_strcpy(this->name, name, sizeof(this->name));
	this->max_fd = max_fd_count;
	rc = desc_pool.init(max_fd_count);
	if(rc != 0)
	{
		S5LOG_ERROR("Failed init desc_pool for poller:%s, rc:%d", name, rc);
		return rc;
	}
	rc = ctrl_queue.init("ctrl_q", 128, TRUE);
	if(rc != 0)
	{
		S5LOG_ERROR("Failed init ctrl_queue for poller:%s, rc:%d", name, rc);
		return rc;
	}
	epfd = epoll_create(max_fd_count);
	if (epfd <= 0)
	{
		rc = -errno;
		S5LOG_ERROR("Failed init epfd for poller:%s, rc:%d", name, rc);
		return rc;
	}
	int flags = fcntl(ctrl_queue.event_fd, F_GETFL, 0);
	fcntl(ctrl_queue.event_fd, F_SETFL, flags | O_NONBLOCK);
	rc = async_add_fd(ctrl_queue.event_fd, EPOLLIN, on_ctl_event, this);
	if(rc != 0)
	{
		S5LOG_ERROR("Failed add ctrl_queue to poller:%s, rc:%d", name, rc);
		return rc;
	}

	//must create thread at last, tid used to indicate this object is fully constructed
	rc = pthread_create(&tid, NULL, thread_entry, this);
	if (rc != 0)
	{
		tid=0;
		S5LOG_ERROR("Failed to start poller thread, rc:%d", rc);
		return -rc;

	}
	return 0;

}

void* PfPoller::thread_entry(void* arg)
{
	PfPoller* p = (PfPoller*)arg;
	p->run();
	return NULL;
}

int PfPoller::add_fd(int fd, uint32_t events, epoll_evt_handler event_handler, void* handler_arg)
{
	if (pthread_self() == tid)
	{
		return async_add_fd( fd, events, event_handler, handler_arg);
	}
	else
		return ctrl_queue.sync_invoke([this, fd, events, event_handler, handler_arg]()->int {
			return this->async_add_fd(fd, events, event_handler, handler_arg);
	});
}

int PfPoller::del_fd(int fd)
{
	if (pthread_self() == tid)
	{
		return async_del_fd(fd);
	}
	else
		return ctrl_queue.sync_invoke([this, fd]()->int {
			return this->async_del_fd(fd);
	});
}

void PfPoller::destroy()
{
	if(tid == 0)
		return;
	del_fd(ctrl_queue.event_fd);
	pthread_cancel(tid);
	pthread_join(tid, NULL);
	tid=0;
	close(epfd);
	epfd = 0;
	ctrl_queue.destroy();
	desc_pool.destroy();
}

int PfPoller::async_add_fd( int fd, uint32_t events, epoll_evt_handler event_handler, void* handler_arg)
{
	struct PollerFd* desc = desc_pool.alloc();
	if (desc == NULL)
	{
		S5LOG_ERROR("can't add more fd to poller:%s!", name);
		return -ENOMEM;
	}
	memset(desc, 0, sizeof(*desc));
	desc->fd = fd;
	desc->handler = event_handler;
	desc->cbk_arg = handler_arg;
	desc->events_to_watch.events = events;
	desc->events_to_watch.data.ptr = desc;
	int rc = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &desc->events_to_watch);
	if (rc)
	{
		S5LOG_ERROR("Failed call epoll_ctl. rc: %d", -errno);
		return -errno;
	}
	return 0;
}

int PfPoller::async_del_fd(int fd)
{
	int rc = epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
	if (rc)
	{
		S5LOG_ERROR("Failed call epoll_ctl. rc: %d", -errno);
		return -errno;
	}
	PollerFd* descs = desc_pool.data;
	for (int i = 0; i < desc_pool.obj_count; i++)
	{
		if (descs[i].fd == fd)
		{
			descs[i].fd = 0;
			desc_pool.free(&descs[i]);
			return 0;
		}
	}
	S5LOG_ERROR("Can't find fd:%d in poller to delete", fd);
	return -ENOENT;
}

void PfPoller::run()
{
	prctl(PR_SET_NAME, name);
	struct epoll_event rev[max_fd];
	struct sched_param sp;
	memset(&sp, 0, sizeof(sp));
	sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
	pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
	while (1)
	{
		int nfds = epoll_wait(epfd, rev, max_fd, -1);
		if (nfds == -1)
		{
			//interrupted, no message in
		}
		else if (nfds == 0)
		{
			//time out
		}

		for (int i = 0; i < nfds; i++)
		{
			PollerFd* desc = (PollerFd*)rev[i].data.ptr;
			if (desc->fd == 0) {
				//some event may already in poller ready list, though fd has been removed from interest list
				S5LOG_WARN("Get event on removed fd"/*, rev[i].data.fd*/);
				continue;
			}
			desc->handler(desc->fd, rev[i].events, desc->cbk_arg);
		}
	}
}
