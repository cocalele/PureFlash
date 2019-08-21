#ifndef _LOG_H_
#define _LOG_H_

/**
* Copyright (C), 2014-2015.
* @file
* s5log macroes.
*
* This file defines s5log's macroes.
*/

#include <log4c.h>

/**
 * init a category for s5log.
 *
 * it should be called in the source file which include main function.
 * @param[in] category	name of category.
 */
#define S5LOG_INIT(category)							\
	static log4c_category_t* __s5_log_category = NULL;		\
	__attribute__((constructor))							\
	static void __init_category(void)						\
	{														\
		__s5_log_category = log4c_category_get(category);	\
	}														\
	inline log4c_category_t* get_category(){return __s5_log_category;}

/**
 * log the fatal type information.
 */
#define S5LOG_FATAL(fmt,args...)							\
log4c_category_log(get_category(), LOG4C_PRIORITY_FATAL,  "(%s:%d:%s) " fmt, __FILE__ , __LINE__ , __FUNCTION__ , ##args)

/**
 * log the error type information.
 */
#define S5LOG_ERROR(fmt,args...)							\
log4c_category_log(get_category(), LOG4C_PRIORITY_ERROR,  "(%s:%d:%s) " fmt, __FILE__ , __LINE__ , __FUNCTION__ , ##args)

/**
 * log the warn type information.
 */
#define S5LOG_WARN(fmt,args...)							\
log4c_category_log(get_category(), LOG4C_PRIORITY_WARN, "(%s:%d:%s) " fmt, __FILE__ , __LINE__ , __FUNCTION__ , ##args)


/**
 * log the info type information.
 */
#define S5LOG_INFO(fmt,args...)							\
log4c_category_log(get_category(), LOG4C_PRIORITY_INFO, "(%s:%d:%s) " fmt, __FILE__ , __LINE__ , __FUNCTION__ , ##args)


/**
 * log the debug type information.
 */
#define S5LOG_DEBUG(fmt,args...)							\
log4c_category_log(get_category(), LOG4C_PRIORITY_DEBUG,  "(%s:%d:%s) " fmt, __FILE__ , __LINE__ , __FUNCTION__ , ##args)


/**
 * log the trace type information.
 */
#define S5LOG_TRACE(fmt,args...)							\
log4c_category_log(get_category(), LOG4C_PRIORITY_TRACE, "(%s:%d:%s) " fmt, __FILE__ , __LINE__ , __FUNCTION__ , ##args)
#endif
