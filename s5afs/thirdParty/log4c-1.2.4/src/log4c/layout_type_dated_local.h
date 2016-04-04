/* $Id: layout_type_dated_local.h,v 1.1 2013/09/29 17:50:09 valtri Exp $
 *
 * layout_type_dated_local.h
 * 
 * Copyright 2001-2003, Meiosys (www.meiosys.com). All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#ifndef log4c_layout_type_dated_local_h
#define log4c_layout_type_dated_local_h

/**
 * @file layout_type_dated_local.h
 *
 * @brief Implement a dated layout with local time.
 *
 * In @c log4j.PatternLayout conventions, the dated layout has the following
 * conversion pattern: @c "%d %P %c - %m\n".
 *
 * Where 
 * @li @c "%d" is the date of the logging event
 * @li @c "%P" is the priority of the logging event
 * @li @c "%c" is the category of the logging event
 * @li @c "%m" is the application supplied message associated with the
 * logging event
 *
 * 
 * 
 **/

#include <log4c/defs.h>
#include <log4c/layout.h>

__LOG4C_BEGIN_DECLS

extern const log4c_layout_type_t log4c_layout_type_dated_local;

__LOG4C_END_DECLS

#endif
