static const char version[] = "$Id: appender_type_syslog.c,v 1.8 2013/04/06 13:12:20 valtri Exp $";

/*
 * appender_syslog.c
 *
 * Copyright 2001-2003, Meiosys (www.meiosys.com). All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <log4c/appender.h>
#include <log4c/priority.h>

#ifdef HAVE_SYSLOG_H
#include <syslog.h>

/*******************************************************************************/
static int log4c_to_syslog_priority(int a_priority)
{
    static int priorities[] = {
	LOG_EMERG,	
	LOG_ALERT,	
	LOG_CRIT, 
	LOG_ERR,	
	LOG_WARNING,	
	LOG_NOTICE, 
	LOG_INFO, 	
	LOG_DEBUG 
    };
    int result;
    
    a_priority++;
    a_priority /= 100;
    
    if (a_priority < 0) {
	result = LOG_EMERG;
    } else if (a_priority > 7) {
	result = LOG_DEBUG;
    } else {
	result = priorities[a_priority];
    }
    
    return result;
}

/*******************************************************************************/
static int syslog_open(log4c_appender_t* this)
{
    int facility = (int) log4c_appender_get_udata(this);

    if (!facility)
	facility = LOG_USER;

    openlog(log4c_appender_get_name(this), LOG_PID, facility); 
    return 0;
}

/*******************************************************************************/
static int syslog_append(log4c_appender_t*	this, 
			 const log4c_logging_event_t* a_event)
{
    int facility = (int) log4c_appender_get_udata(this);

    if (!facility)
	facility = LOG_USER;

    syslog(log4c_to_syslog_priority(a_event->evt_priority) | facility, 
	   "%s", a_event->evt_rendered_msg);
    return 0;
}

/*******************************************************************************/
static int syslog_close(log4c_appender_t*	this)
{
    closelog();
    return 0;
}

#else

/*******************************************************************************/
static int syslog_open(log4c_appender_t* this)
{
    return 0;
}

/*******************************************************************************/
static int syslog_append(log4c_appender_t*	this, 
			 const log4c_logging_event_t* a_event)
{
    return 0;
}

/*******************************************************************************/
static int syslog_close(log4c_appender_t*	this)
{
    return 0;
}
#endif

extern int log4c_appender_syslog_set_facility(log4c_appender_t* this, int facility)
{
    return (int) log4c_appender_set_udata(this, (void*) facility);
}

/*******************************************************************************/
const log4c_appender_type_t log4c_appender_type_syslog = {
    "syslog",
    syslog_open,
    syslog_append,
    syslog_close,
};

