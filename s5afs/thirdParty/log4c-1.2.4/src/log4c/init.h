/* $Id: init.h,v 1.6 2012/09/30 20:15:08 valtri Exp $
 *
 * Copyright 2001-2003, Meiosys (www.meiosys.com). All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#ifndef __log4c_init_h
#define __log4c_init_h

#include <log4c/defs.h>
#include <stdio.h>

__LOG4C_BEGIN_DECLS

/**
 * @file init.h
 *
 * @brief log4c constructors and destructors
 *
 **/   

/**
 * constructor
 * 
 * @returns 0 for success 
 **/
LOG4C_API int log4c_init(void);

/**
 * destructor
 *
 * @returns 0 for success 
 **/
LOG4C_API int log4c_fini(void);

/*
 * Dumps all the current appender, layout and rollingpolicy types
 * known by log4c.
 * @param stream to write to
 */
LOG4C_API void log4c_dump_all_types(FILE *fp);

/*
 * Dumps all the current instances of categories, appenders, layouts
 * and rollingpolicy objects.
 * An instances of a type consists of the base
 * type information (name plus function table) and an instance name and
 * configuration.  For example one can have an instance of the rollingfile
 * appender which logs to /var/tmp and another instance which logs to 
 * /usr/tmp.  They are both of type rollingfile, but are distinct instances of
 * it
 * @param stream to write t
 */
LOG4C_API void log4c_dump_all_instances(FILE *fp);

__LOG4C_END_DECLS

#endif
