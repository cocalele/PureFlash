/* $Id: defs.h,v 1.4 2013/09/29 17:41:39 valtri Exp $
 *
 * Copyright 2001-2003, Meiosys (www.meiosys.com). All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#ifndef __sd_defs_h
#define __sd_defs_h

#ifdef  __cplusplus
# define __SD_BEGIN_DECLS  extern "C" {
# define __SD_END_DECLS    }
#else
# define __SD_BEGIN_DECLS
# define __SD_END_DECLS
#endif

/* GNU C attribute feature,
 * for the public API macro LOG4C_ATTRIBUTE is used instead */
#ifdef __GNUC__
#define SD_ATTRIBUTE(X) __attribute__(X)
#else
#define SD_ATTRIBUTE(X)
#endif

#endif
