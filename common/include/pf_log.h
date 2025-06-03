#ifndef _S5LOG_H_
#define _S5LOG_H_

/**
* Copyright (C), 2014-2019.
* @file
* s5log macro.
*
* This file defines s5log's macro.
*/

void s5log(int level, const char * format, ...) __attribute__((format(printf, 2, 3)));

#define S5LOG_LEVEL_FATAL 0
#define S5LOG_LEVEL_ERROR 1
#define S5LOG_LEVEL_WARN 2
#define S5LOG_LEVEL_INFO 3
#define S5LOG_LEVEL_DEBUG 4
/**
 * log the fatal type information.
 */
#define S5LOG_FATAL(fmt,args...)							\
s5log(S5LOG_LEVEL_FATAL,  fmt "(%s:%d:%s) " , ##args, __FILE__ , __LINE__ , __FUNCTION__ )

/**
 * log the error type information.
 */
#define S5LOG_ERROR(fmt,args...)							\
s5log(S5LOG_LEVEL_ERROR,  fmt "(%s:%d:%s) " ,  ##args, __FILE__ , __LINE__ , __FUNCTION__ )

/**
 * log the warn type information.
 */
#define S5LOG_WARN(fmt,args...)							\
s5log(S5LOG_LEVEL_WARN,  fmt "(%s:%d:%s) " ,  ##args, __FILE__ , __LINE__ , __FUNCTION__ )


/**
 * log the info type information.
 */
#define S5LOG_INFO(fmt,args...)							\
s5log(S5LOG_LEVEL_INFO,  fmt "(%s:%d:%s) " ,  ##args, __FILE__ , __LINE__ , __FUNCTION__ )


/**
 * log the debug type information.
 */
#define S5LOG_DEBUG(fmt,args...)							\
s5log(S5LOG_LEVEL_DEBUG,  fmt "(%s:%d:%s) " ,  ##args, __FILE__ , __LINE__ , __FUNCTION__ )


#endif //_S5LOG_H_
