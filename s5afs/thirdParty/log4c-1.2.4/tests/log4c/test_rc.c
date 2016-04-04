static const char version[] = "$Id: test_rc.c,v 1.7 2013/09/29 17:48:26 valtri Exp $";

/*
 * test_rc.c
 *
 * Copyright 2001-2003, Meiosys (www.meiosys.com). All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <log4c/init.h>
#include <log4c/rc.h>
#include <log4c/category.h>
#include <sd/test.h>
#include <log4c/layout.h>
#include <log4c/appender.h>
#include <sd/factory.h>
#include <stdio.h>

/******************************************************************************/
static void log4c_print(FILE* a_fp)
{   
    extern sd_factory_t* log4c_category_factory;
    extern sd_factory_t* log4c_appender_factory;
    extern sd_factory_t* log4c_layout_factory;

    sd_factory_print(log4c_category_factory, a_fp);	fprintf(a_fp, "\n");
    sd_factory_print(log4c_appender_factory, a_fp);	fprintf(a_fp, "\n");
    sd_factory_print(log4c_layout_factory, a_fp);	fprintf(a_fp, "\n");
}

/******************************************************************************/
static int test0(sd_test_t* a_test, int argc, char* argv[])
{    
    log4c_print(sd_test_out(a_test));
    return 1;
}

/******************************************************************************/
static int test1(sd_test_t* a_test, int argc, char* argv[])
{    
    log4c_rc_t rc;

    if (log4c_rc_load(&rc, SRCDIR "/test_rc.in") == -1)
	return 0;

    log4c_print(sd_test_out(a_test));
    return 1;
}

/******************************************************************************/
static int test2(sd_test_t* a_test, int argc, char* argv[])
{
    log4c_rc_t rc;

    if (log4c_rc_load(&rc, SRCDIR "/test_rc.in") == -1)
	return 0;

    if (log4c_rc_load(&rc, SRCDIR "/test_rc.in") == -1)
	return 0;

    log4c_print(sd_test_out(a_test));
    return 1;
}

/******************************************************************************/
int main(int argc, char* argv[])
{
    int ret;
    sd_test_t* t;

    /*
     * initialize log4c to prevent memleaks
     * (there is no "unload")
     */
    log4c_init();

    t = sd_test_new(argc, argv);

    sd_test_add(t, test0);
    sd_test_add(t, test1);
    sd_test_add(t, test2);

    ret = sd_test_run(t, argc, argv);

    sd_test_delete(t);

    log4c_fini();

    return !ret;
}
