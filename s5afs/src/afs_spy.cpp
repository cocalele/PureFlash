#include <pthread.h>
#include <sys/types.h>  
#include <sys/socket.h>
#include <sys/epoll.h>
#include <signal.h>
#include <semaphore.h>
#include <arpa/inet.h>		 // For inet_addr()
#include <unistd.h> 		 // For close()
#include <netinet/in.h> 	 // For sockaddr_in
#include <errno.h>
#include <string>
#include <iostream>
#include <sstream>
#include <map>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "spy.h"
#include "s5log.h"
#include "s5utils.h"

using namespace std;
static unsigned short spy_port;
static pthread_t spy_thread_id;

struct variable_info
{
	const void* var_adr;
	enum variable_type va_type;
	void* arg;
	std::string comment;
};

struct thread_coordinator
{
	pthread_mutex_t* mutex;
	pthread_cond_t*  cond;
	volatile BOOL*  done;
};

static map<string, variable_info> variables;

void spy_register_variable(const char* name, const void* var_addr, enum variable_type var_type, const char* comment)
{
	variable_info info={var_addr, var_type, NULL, comment};
	variables[name] = info;
};

void spy_register_property_getter(const char* name, 
						   property_getter get_func, 
						   void* obj_to_get,
						   enum variable_type prop_type,
						   const char* comment
						   )
{
	S5ASSERT(prop_type == vt_prop_int64);
	variable_info info={(void*)get_func, prop_type, obj_to_get, comment};
	variables[name] = info;
}

void spy_register_property_setter(const char* name, 
								  property_setter set_func, 
								  void* obj_to_get,
								  enum variable_type prop_type,
								  const char* comment
								  )
{
	S5ASSERT(prop_type == vt_write_property);
	variable_info info={(void*)set_func, prop_type, obj_to_get, comment};
	variables[name] = info;
}

void spy_unregister(const char* name)
{
	variables.erase(name);
}

static void do_write(int socket_fd, istringstream& cmdline, ostringstream &oss)
{
	if(cmdline.good())
	{
		string var_name;
        cmdline >> var_name;

        if(var_name.empty()) 
		{
			return;
		}
		else
		{
			map<string, variable_info>::iterator it = variables.find(var_name);
			if(it == variables.end())
        	{ 
            	oss << "No such var:" <<var_name;
            	return;
        	}
			else
			{
				assert(it->second.va_type == vt_write_property);
				string write_buf;
				string temp_buf;
			
				while(cmdline.good())
				{	
					cmdline >> temp_buf;
					write_buf += temp_buf;	
					write_buf += " ";	
				}
				((property_setter)it->second.var_adr)(it->second.arg, (char*)(write_buf.c_str()));
			} 		
		}
	}
}

static void do_read(int socket_fd, istringstream& cmdline, ostringstream &oss)
{
	while(cmdline.good())
	{
		string var_name;
		cmdline >> var_name;

		if(var_name.empty())
			continue;
		map<string, variable_info>::iterator it = variables.find(var_name);
		if(it == variables.end())
		{
			oss << "No such var:" <<var_name;
			return;
		}
		else
		{
			switch(it->second.va_type)
			{
			case vt_int32:
				oss << *((int32_t*)it->second.var_adr)<<" ";
				break;
			case vt_int64:
				oss << *((int64_t*)it->second.var_adr)<<" ";
				break;
			case vt_cstr:
				oss << ((const char*)it->second.var_adr)<<" ";
				break;
			case vt_prop_int64:
				oss << ((property_getter)it->second.var_adr)(it->second.arg) << " ";
				break;
            case vt_uint32:
                oss << *((unsigned int*)it->second.var_adr)<<" ";
                break;
            case vt_float:
                oss << *((float*)it->second.var_adr)<<" ";
                break; 
			default:
				oss<<"invalid type:"<<var_name;
				return;
			}
		}
	}
}

static void *spy_thread_proc(void *arg)
{
	struct sigaction sa={{0}};
	sa.sa_handler=SIG_IGN;
	sigaction(SIGUSR1, &sa, NULL);

	struct sockaddr_in addr={0};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr=htonl(INADDR_ANY);
	addr.sin_port=htons(spy_port);
	int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in remote_addr;
	socklen_t addr_len;
	int rc;

	thread_coordinator* tc = (thread_coordinator*)(arg);
	
	while( (rc = bind(socket_fd, (sockaddr*)&addr, sizeof(addr))) != 0 && spy_port < 65535)
	{
		if(errno == EADDRINUSE)
		{
			spy_port ++;
			addr.sin_port=htons(spy_port);
			continue;
		}
		S5LOG_WARN("NBMonitor failed bind to port:%d. errno: %d %s.", spy_port, errno, strerror(errno));

		if(arg != NULL)
		{
			pthread_mutex_lock(tc->mutex);
			*(tc->done) = TRUE;
			pthread_mutex_unlock(tc->mutex);
			pthread_cond_signal(tc->cond);
		}
		return NULL;
	}
	if(arg != NULL)
	{
		pthread_mutex_lock(tc->mutex);
		*(tc->done) = TRUE;
		pthread_mutex_unlock(tc->mutex);
		pthread_cond_signal(tc->cond);
	}
		
	S5LOG_INFO("NBMonitor bind to port:%d.", spy_port);
	while(1)
	{
		char buf[4096];
		addr_len = sizeof(remote_addr);
		ssize_t len = recvfrom(socket_fd, buf, sizeof(buf)-1, 0, (sockaddr*)&remote_addr, &addr_len);
		if(len == -1)
		{
			return NULL;
		}
		buf[len]=0;
		istringstream iss(buf);
		string cmd;
		iss>>cmd;
		ostringstream oss;
		if(cmd == "read")
		{
			do_read(socket_fd, iss, oss);
		}
		else if(cmd == "write")
		{
			do_write(socket_fd, iss, oss);
		}
		else if(cmd == "list")
		{
			for(map<string, variable_info>::iterator i = variables.begin(); i != variables.end(); ++i)
			{
				oss<<i->first<<"\t: "<<i->second.comment<<endl;
			}
		}
		else
		{
			const char* help = "Invalid command. Commands are: read <var_name> [var_name] ...\n";
			oss <<help;
		}

		ssize_t rc1 = sendto(socket_fd, oss.str().c_str(), oss.str().length(), 0, (sockaddr*)&remote_addr, addr_len);
		if(rc1 == -1)
		{
			S5LOG_ERROR("Failed to send reply errno: %d %s.", errno, strerror(errno));
		}
	}
	return NULL;
}

void spy_sync_start(int port)
{
	BOOL done_or_not = FALSE;
	spy_port = (unsigned short)port;
	thread_coordinator tc; 

	pthread_mutex_t spy_mutex;
    pthread_cond_t spy_cond;

	pthread_mutex_init(&spy_mutex, NULL);
    pthread_cond_init(&spy_cond, NULL);


	tc.mutex = &spy_mutex;
	tc.cond  = &spy_cond;
	tc.done  = &done_or_not;   

	int rc = pthread_create(&spy_thread_id, NULL, spy_thread_proc, &tc);
    
	pthread_mutex_lock(tc.mutex);
    while(done_or_not == FALSE)
    {    
        pthread_cond_wait(tc.cond, tc.mutex);
    }    
    pthread_mutex_unlock(tc.mutex);

	if(rc)
    {   
        S5LOG_WARN("Failed to call spy thread. errno: %d %s.",  errno, strerror(errno));
    }
}

void spy_start(int port)
{
	spy_port = (unsigned short)port;
	int rc = pthread_create(&spy_thread_id, NULL, spy_thread_proc, NULL);
	if(rc)
	{
		S5LOG_WARN("Failed to call spy thread. errno: %d %s.",  errno, strerror(errno));
	}
}

unsigned short spy_get_port()
{
	return spy_port;
}

