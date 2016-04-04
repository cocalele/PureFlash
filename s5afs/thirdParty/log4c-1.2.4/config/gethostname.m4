AC_DEFUN([LOG4C_FUNC_GETHOSTNAME],
[
	GETHOSTNAME_LIB=
	AC_CHECK_FUNCS([gethostname], , [
		AC_CACHE_CHECK([for gethostname in winsock.h and -lws2_32],
			[l4_cv_w32_gethostname],
			[l4_cv_w32_gethostname=no
			 ac_save_LIBS="$LIBS"
			 LIBS="$LIBS -lws2_32"
			 AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#ifdef _WIN32
#include <winsock.h>
#endif
#include <unistd.h>
#include <stddef.h>
]], [[gethostname(NULL, 0);]])], [l4_cv_w32_gethostname=yes])
			 LIBS="$ac_save_LIBS"
			])
		if test "x$l4_cv_w32_gethostname" = "xyes"; then
			GETHOSTNAME_LIB="-lws2_32"
		fi
	])
	AC_SUBST([GETHOSTNAME_LIB])
])
