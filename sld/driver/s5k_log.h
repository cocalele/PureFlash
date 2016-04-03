#ifndef __S5K_LOG_H__
#define __S5K_LOG_H__

#include <linux/printk.h>
//KERN_EMERG 0/*紧急事件消息，系统崩溃之前提示，表示系统不可用*/
//KERN_ALERT 1/*报告消息，表示必须立即采取措施*/
//KERN_CRIT 2/*临界条件，通常涉及严重的硬件或软件操作失败*/
//KERN_ERR 3/*错误条件，驱动程序常用KERN_ERR来报告硬件的错误*/
//KERN_WARNING 4/*警告条件，对可能出现问题的情况进行警告*/
//KERN_NOTICE 5/*正常但又重要的条件，用于提醒*/
//KERN_INFO 6/*提示信息，如驱动程序启动时，打印硬件信息*/
//KERN_DEBUG 7/*调试级别的消息*/

#define	DEBUG_LOG
#ifdef DEBUG_LOG
extern int printk(const char *fmt, ...);

//#define LOG_DETAIL
#ifdef LOG_DETAIL
#define LOG_FATAL(fmt, arg...) \
		do { \
			printk(KERN_EMERG fmt " %s()-%d.\n", ##arg, __FUNCTION__,  __LINE__); \
		}while(0)

#define LOG_ERROR(fmt, arg...) \
		do { \
			printk(KERN_ERR fmt " %s()-%d.\n", ##arg, __FUNCTION__,  __LINE__); \
		}while(0)

#define LOG_WARN(fmt, arg...) \
		do { \
			printk(KERN_WARNING fmt " %s()-%d.\n", ##arg, __FUNCTION__,  __LINE__); \
		}while(0)

#define LOG_INFO(fmt, arg...) \
		do { \
			printk(KERN_NOTICE fmt " %s()-%d.\n", ##arg, __FUNCTION__,  __LINE__); \
		}while(0)

#define LOG_DEBUG(fmt, arg...) \
		do { \
			printk(KERN_INFO fmt " %s()-%d.\n", ##arg, __FUNCTION__,  __LINE__); \
		}while(0)

#define LOG_TRACE(fmt, arg...) \
		do { \
			printk(KERN_DEBUG fmt " %s()-%d.\n", ##arg, __FUNCTION__,  __LINE__); \
		}while(0)
#else
#define LOG_FATAL(fmt, arg...) \
		do { \
			printk(KERN_EMERG fmt "\n", ##arg); \
		}while(0)

#define LOG_ERROR(fmt, arg...) \
		do { \
			printk(KERN_ERR fmt "\n", ##arg); \
		}while(0)

#define LOG_WARN(fmt, arg...) \
		do { \
			printk(KERN_WARNING fmt "\n", ##arg); \
		}while(0)

#define LOG_INFO(fmt, arg...) \
		do { \
			printk(KERN_NOTICE fmt "\n", ##arg); \
		}while(0)

#define LOG_DEBUG(fmt, arg...) \
		do { \
			printk(KERN_INFO fmt "\n", ##arg); \
		}while(0)

#define LOG_TRACE(fmt, arg...) \
		do { \
			printk(KERN_DEBUG fmt "\n", ##arg); \
		}while(0)
#endif

#else
#define LOG_FATAL(fmt, arg...)
#define LOG_ERROR(fmt, arg...)
#define LOG_WARN(fmt, arg...)
#define LOG_INFO(fmt, arg...)
#define LOG_DEBUG(fmt, arg...)
#define LOG_TRACE(fmt, arg...)
#endif

#endif //__S5K_LOG_H__
