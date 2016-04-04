/* $Id: error.h,v 1.7 2013/09/29 17:41:39 valtri Exp $
 *
 * Copyright 2001-2003, Meiosys (www.meiosys.com). All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#ifndef __sd_error_h
#define __sd_error_h

#include <stdarg.h>
#include <sd/defs.h>

extern int sd_debug(const char *fmt, ...) SD_ATTRIBUTE((format(printf, 1, 2)));
extern int sd_error(const char *fmt, ...) SD_ATTRIBUTE((format(printf, 1, 2)));

#endif /* __sd_error_h */
