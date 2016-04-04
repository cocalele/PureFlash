AC_DEFUN([LOG4C_VA_COPY],
[
	AC_CACHE_CHECK([for va_copy],
		[l4_cv_va_copy],
		[l4_cv_va_copy=no
		 AC_LINK_IFELSE([AC_LANG_PROGRAM(
			[[#include <stdarg.h>]],
			[[va_list ap1, ap2; va_copy(ap1, ap2);]])],
			[l4_cv_va_copy=yes])])

	if test "x$l4_cv_va_copy" != "xyes"; then
		AC_DEFINE([va_copy], [__va_copy], [Trying to use __va_copy on non-C89 systems])
	fi
])
