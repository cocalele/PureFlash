#include <fcntl.h>
#include <sys/resource.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <arpa/inet.h>           // For inet_addr()
#include <sys/socket.h>
#include <netinet/in.h>
#include "/usr/include/signal.h" //avoid confuse with Ceph's signal.h
#include "s5log.h"

#include "basetype.h"
#include "s5utils.h"

#define MAXLINE 4096
void write_pid_file(const char* filename)
{
	FILE* f = fopen(filename, "w");
	if(!f)
	{
		S5LOG_WARN("Fail to create pid file:%s", filename);
		return ;
	}
	fprintf(f, "%d\n", getpid());
	fclose(f);
}

int exit_thread(pthread_t pid)
{   
    int rc = 0;
    void* res;
    rc = pthread_cancel(pid);
    if(rc != 0)
        S5LOG_ERROR("Failed to do pthread_cancel,  pid(%llu): rc:%d.\n", (unsigned long long)pid, rc);
    
    rc = pthread_join(pid, &res);
    if (rc != 0)
        S5LOG_ERROR("Failed to do pthread_join pid(%llu) rc:%d.\n", (unsigned long long)pid, rc);
    if (res == PTHREAD_CANCELED)
        S5LOG_INFO("Thread(pid%llu) was canceled.\n", (unsigned long long)pid);
    else
        S5LOG_INFO("Thread(pid%llu) terminated normally\n", (unsigned long long)pid);
    return rc; 
}

char* safe_strcpy(char* dest, const char* src, size_t dest_size)
{
	char* p = strncpy(dest, src, dest_size);
	if (dest_size > 0)
		dest[dest_size - 1]= '\0';
	return p;
}

static BOOL isDigit(char digit)  
{  
	BOOL flag = FALSE;  
	if(digit >= '0' && digit <= '9')  
	{  
		flag = TRUE;  
	}	  
	return flag;  
}   
 
static BOOL isFormatValid(const char* ip)  
{  
	int dotCnt = 0;  
	BOOL flag = FALSE; 
	while(*ip != '\0')  
	{  
		if(*ip == '.')  
		{  
			dotCnt++;  
		}  
		else if(!isDigit(*ip))  
		{  
			return FALSE;  
		}	          
		flag = TRUE;  
		ip++; 
	}  
	if(dotCnt == 3)  
	{  
		return flag;  
	}  
	else  
	{  
		return FALSE;  
	}  
} 
 
static BOOL isValueValid(const char* ip)  
{  
	int integer = 0;  
	while(*ip != '\0')  
	{ 
		if(isDigit(*ip))  
		{	  
			integer = integer*10 + *ip - '0';  
		}  
		else  
		{  
			if(integer > 255)  
			{ 
				return FALSE;  
			}             
			integer = 0;  
		}		  
		ip++; 
	}
	return TRUE;  
}  

BOOL isIpValid(const char* ip)
{
	if(isFormatValid(ip) && isValueValid(ip))
	{
		return TRUE;
	}
	
	return FALSE;
}

uint64_t get_cbs_by_iops(uint64_t iops) 
{ 
    return (iops * (uint64_t)2); 
}

