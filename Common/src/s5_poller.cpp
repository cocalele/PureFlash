#include "s5_poller.h"
static void on_ctl_event(int fd, uint32_t event, void* user_arg)
{
	S5Poller* poller = (S5Poller*)user_arg;
	fixed_size_queue<S5Event>* q;
	if (poller->ctrl_queue.get_event(&q) == 0 && q != NULL)
	{
		while(!q->is_empty())
		{
			S5Event evt = q->dequeue();
			switch (evt.type)
			{
			case EVT_EPCTL_DEL:
			{
				struct epctl_del_arg* arg = (struct epctl_del_arg*)evt.arg_p;
				arg->rc = poller->async_del_fd(arg->fd);
				sem_post(&arg->sem);
				break;
			}
			case EVT_EPCTL_ADD:
			{
				struct epctl_add_arg* arg = (struct epctl_add_arg*)evt.arg_p;
				arg->rc = poller->async_add_fd(arg->fd, arg->events, arg->event_handler, arg->handler_arg);
				sem_post(&arg->sem);
				break;
			}
			case EVT_LAMBDA_CALL:
			{
				struct lambda_call_args* arg = (struct lambda_call_args*)evt->arg_p;
				arg->rc = arg->lambda();
				sem_post(&arg->sem);
				break;
			}
			default:
				S5LOG_ERROR( "Poller get unknown event type:%d", evt.type);
			}
		}

	}
}
int S5Poller::init(char* name, int max_fd_count)
{
	int rc = 0;
	safe_strcpy(this->name, name, sizeof(this->name));
	this->max_fd = max_fd_count;
	rc = desc_pool.init(max_fd_count);
	if(rc != 0)
	{
		S5LOG_ERROR("Failed init desc_pool for poller:%s, rc:%d", name, rc);
		goto release1;
	}
	rc = ctrl_queue.init(128);
	if(rc != 0)
	{
		S5LOG_ERROR("Failed init ctrl_queue for poller:%s, rc:%d", name, rc);
		goto release2;
	}
	epfd = epoll_create(max_fd_count);
	if (epfd <= 0)
	{
		rc = -errno;
		S5LOG_ERROR("Failed init epfd for poller:%s, rc:%d", name, rc);
		goto release3;
	}
	int flags = fcntl(ctrl_queue.event_fd, F_GETFL, 0);
	fcntl(ctrl_queue.event_fd, F_SETFL, flags | O_NONBLOCK);
	async_add_fd(ctrl_queue.fd, EPOLLIN, on_ctl_event, this);
	rc = pthread_create(&tid, NULL, thread_entry, this);
	if (rc != 0)
	{
		S5LOG_ERROR("Failed to start poller thread, rc:%d", rc);
		goto release4;
	}
	return 0;

}

void* S5poller::thread_entry(void* arg)
{
	S5Poller* p = (S5Poller*)arg;
	p->run();
	return NULL;
}

int S5Poller::add_fd(int fd, uint32_t events, epoll_evt_handler event_handler, void* handler_arg)
{
	if (pthread_self() == poller->tid)
	{
		return async_add_fd( fd, events, event_handler, handler_arg);
	}
	else
		return sync_lambda_call([this, fd, events, event_handler, handler_arg]()->int {
			return this->async_add_fd(fd, events, event_handler, handler_arg);
	});
}

int S5Poller::del_fd(int fd)
{
	if (pthread_self() == poller->tid)
	{
		return async_poller_del(poller, fd);
	}
	else
		return sync_lambda_call([fd, events, event_handler, handler_arg]()->int {
			return this->async_del_fd(fd, events, event_handler, handler_arg);
	});
}

void S5Poller::destroy()
{
	pthread_cancel(poller->tid);
	pthread_join(poller->tid, NULL);
	qfa_release_mem_pool(&poller->desc_pool);
	close(poller->epfd);
}

int S5Poller::async_add_fd( int fd, uint32_t events, qpoll_event_handler event_handler, void* handler_arg)
{
	struct PollerFd* desc = desc_pool.alloc();
	if (desc == NULL)
	{
		S5LOG_ERROR("can't add more fd to poller:%s!", name);
		return -ENOMEM;
	}
	memset(desc, 0, sizeof(*desc));
	desc->fd = fd;
	desc->callback = event_handler;
	desc->cbk_arg = handler_arg;
	desc->events.events = events;
	desc->events.data.ptr = desc;
	int rc = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &desc->events);
	if (rc)
	{
		S5LOG_ERROR("Failed call epoll_ctl. rc: %d", -errno);
		return -errno;
	}
	return 0;
}

int S5Poller::async_del_fd(int fd)
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

