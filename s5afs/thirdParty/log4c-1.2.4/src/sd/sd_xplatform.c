static const char version[] = "$Id$";

/*
 * sd_xplatform.c
 *
 * See the COPYING file for the terms of usage and distribution.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#if !defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#else
#include <windows.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/****************** getopt *******************************/

#define	EOF	(-1)
 
 int sd_opterr = 1;
 int sd_optind = 1;
 int sd_optopt = 0;
 char *sd_optarg = NULL;
 int _sp = 1;
 
#define warn(a,b,c)fprintf(stderr,a,b,c)
 
 void
 getopt_reset(void)
 {
 	sd_opterr = 1;
 	sd_optind = 1;
 	sd_optopt = 0;
 	sd_optarg = NULL;
 	_sp = 1;
 }
 
 int
 sd_getopt(int argc, char *const *argv, const char *opts)
 {
 	char c;
 	char *cp;
 
 	if (_sp == 1) {
 		if (sd_optind >= argc || argv[sd_optind][0] != '-' ||
 		    argv[sd_optind] == NULL || argv[sd_optind][1] == '\0')
 			return (EOF);
 		else if (strcmp(argv[sd_optind], "--") == 0) {
 			sd_optind++;
 			return (EOF);
 		}
 	}
 	sd_optopt = c = (unsigned char)argv[sd_optind][_sp];
 	if (c == ':' || (cp = strchr(opts, c)) == NULL) {
 		if (opts[0] != ':')
 			warn("%s: illegal option -- %c\n", argv[0], c);
 		if (argv[sd_optind][++_sp] == '\0') {
 			sd_optind++;
 			_sp = 1;
 		}
 		return ('?');
 	}
 
 	if (*(cp + 1) == ':') {
 		if (argv[sd_optind][_sp+1] != '\0')
 			sd_optarg = &argv[sd_optind++][_sp+1];
 		else if (++sd_optind >= argc) {
 			if (opts[0] != ':') {
 				warn("%s: option requires an argument"
 				    " -- %c\n", argv[0], c);
 			}
 			_sp = 1;
 			sd_optarg = NULL;
 			return (opts[0] == ':' ? ':' : '?');
 		} else
 			sd_optarg = argv[sd_optind++];
 		_sp = 1;
 	} else {
 		if (argv[sd_optind][++_sp] == '\0') {
 			_sp = 1;
 			sd_optind++;
 		}
 		sd_optarg = NULL;
 	}
 	return (c);
 }
 

#if !defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)

/* POSIX-like environment */

/*
 * Get last changetime of a file
 */
int sd_stat_ctime(const char* path, time_t* time)
{
	struct stat astat;
	int statret=stat(path,&astat);
	if (0 != statret)
	{
		return statret;
	}
	*time=astat.st_ctime;
	return statret;
}

#if !defined(HAVE_GMTIME_R) || !HAVE_DECL_GMTIME_R
/* non-thread-safe replacement */
struct tm *sd_gmtime_r(const time_t *timep, struct tm *result) {
	struct tm *tmp = NULL;
	tmp = gmtime(timep);
	if (tmp) *result = *tmp;

	return tmp;
}
#endif

#if !defined(HAVE_LOCALTIME_R) || !HAVE_DECL_LOCALTIME_R
/* non-thread-safe replacement */
struct tm *sd_localtime_r(const time_t *timep, struct tm *result) {
	struct tm *tmp = NULL;
	tmp = localtime(timep);
	if (tmp) *result = *tmp;

	return tmp;
}
#endif

#else /* _WIN32 && !__MINGW32__ && !__MINGW64__ */

/* native Windows environment */

int sd_gettimeofday(LPFILETIME lpft, void* tzp) {

    if (lpft) {
        GetSystemTimeAsFileTime(lpft);
    }
    /* 0 indicates that the call succeeded. */
    return 0;
}

/*
 * Placeholder for WIN32 version to get last changetime of a file
 */
int sd_stat_ctime(const char* path, time_t* time)
{ return -1; }

#endif /* _WIN32 && !__MINGW32__ && !__MINGW64__ */

#if defined(__MINGW32__) || defined(__MINGW64__)

/* snprintf()/vsnprintf() on MinGW not adding null character and returns -1 */

int sd_vsnprintf(char *str, size_t size, const char *format, va_list ap) {
	int ret;

	ret = vsnprintf(str, size, format, ap);
	if (ret < 0) ret = size;
	if (ret >= size) str[size - 1] = '\0';

	return ret;
}

int sd_snprintf(char *str, size_t size, const char *format, ...) {
	va_list ap;
	int ret;

	va_start(ap, format);
	ret = sd_vsnprintf(str, size, format, ap);
	va_end(ap);

	return ret;
}
#endif
