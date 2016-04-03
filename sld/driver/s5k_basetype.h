#ifndef __S5K_BASETYPE_H__
#define __S5K_BASETYPE_H__
#include <linux/types.h>

#ifndef EOK
#define EOK 0
#endif

#define MERGE_(a,b) a##b
#define LABEL_(a) MERGE_(__check_only, a)
#define __CHECK_ONLY LABEL_(__COUNTER__)
#define ASSERT_SIZE(t,size) typedef char __CHECK_ONLY[ (sizeof(t) == size) ? 1 : -1];


typedef unsigned char uchar;
typedef unsigned int BOOL;
#define TRUE 1
#define FALSE 0


#define S5ASSERT(x) \
	if(!(x)) \
	{ 	\
		while(1) \
		{ \
			LOG_ERROR(__FILE__ ":%d S5ASSERT:%s",  __LINE__, #x); \
			schedule_timeout_uninterruptible(HZ*30); \
		} \
	} \

#endif //__S5K_BASETYPE_H__
