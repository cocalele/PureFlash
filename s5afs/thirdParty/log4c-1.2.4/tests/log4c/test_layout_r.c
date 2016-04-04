static const char version[] = "$Id$";

/*
 * test_layout.c
 *
 * Copyright 2001-2003, Meiosys (www.meiosys.com). All rights reserved.
 *
 * See the COPYING file for the terms of usage and distribution.
 *
 *
 * Note: there's a known issue with this program double closing the
 * test file pointer.  This is because the test shares the file with
 * the log4c stream appender and they both close it on exiting.  In
 * the context here this error is benign. The stream2 appender
 * does not have this issue as it will not close a passed-in
 * file pointer on exit.  In general you would not share a file pointer like
 * this with an appender so that behaviour of the stream appender is not a
 * serious issue.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <log4c/layout_type_basic_r.h>
#include <log4c/appender.h>
#include <log4c/category.h>
#include <log4c/init.h>

#include <log4c/rc.h>
#include <sd/test.h>
#include <sd/factory.h>
#include <stdio.h>

typedef struct {
    FILE *out;
} test_layout_r_udata_t;

static log4c_category_t* root = NULL;
static log4c_category_t* sub1 = NULL;
static test_layout_r_udata_t test_udata;

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
    log4c_layout_t*   layout1   = log4c_layout_get("layout1");
    log4c_appender_t* appender1 = log4c_appender_get("appender1");
    test_layout_r_udata_t *test_udata = sd_test_get_udata(a_test);

    log4c_layout_set_type(layout1, &log4c_layout_type_basic_r);

    log4c_appender_set_layout(appender1, layout1);
    test_udata->out = log4c_appender_get_udata(appender1);
    log4c_appender_set_udata(appender1,  sd_test_out(a_test));

    log4c_category_set_appender(sub1, appender1);
    log4c_category_set_priority(sub1, LOG4C_PRIORITY_ERROR);

    log4c_print(sd_test_out(a_test));
    return 1;
}
/******************************************************************************/
static int test1(sd_test_t* a_test, int argc, char* argv[])
{
    log4c_category_error(sub1, "let's log");
    return 1;
}
/******************************************************************************/
static int test2(sd_test_t* a_test, int argc, char* argv[])
{
    log4c_rc->config.bufsize = 32;
    log4c_category_error(sub1, "let's log a very long log that whill surely get trimmed because it is way to long");
    return 1;
}

/******************************************************************************/
static int test_restore(sd_test_t* a_test, int argc, char* argv[])
{
    log4c_appender_t* appender1 = log4c_appender_get("appender1");
    test_layout_r_udata_t *test_udata = sd_test_get_udata(a_test);

    log4c_appender_set_udata(appender1, test_udata->out);
    return 1;
}

/******************************************************************************/
int main(int argc, char* argv[])
{
    int ret;
    sd_test_t* t = sd_test_new(argc, argv);

    /* If we're not using GNU C then initialize our test categories
       explicitly
    */

    root = log4c_category_get("root");
    sub1 = log4c_category_get("sub1");

    log4c_init();

    sd_test_set_udata(t, &test_udata);
    sd_test_add(t, test0);
    sd_test_add(t, test1);
    sd_test_add(t, test2);
    /* The test shares the file with the log4c stream appender and they would
     *  be both closed on exiting. This will restore original file descriptors
     *  in appenders to prevent double closing. */
    sd_test_add(t, test_restore);

    ret = sd_test_run(t, argc, argv);

    sd_test_delete(t);

    log4c_fini();

    return ! ret;
}
