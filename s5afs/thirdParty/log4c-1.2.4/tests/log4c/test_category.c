static const char version[] = "$Id: test_category.c,v 1.14 2013/09/29 17:39:28 valtri Exp $";

/*
 * test_category.c
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

#include <log4c/appender.h>
#include <log4c/layout.h>
#include <log4c/category.h>
#include <log4c/init.h>
#include <sd/test.h>
#include <sd/factory.h>
#include <sd/sd_xplatform.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    FILE *out;
    FILE *out1;
    FILE *out2;
} test_category_udata_t;

static log4c_category_t* root = NULL;
static log4c_category_t* sub1 = NULL;
static log4c_category_t* sun1sub2 = NULL;
static test_category_udata_t test_udata;

/*******************************************************************************/
static const char* test_format(
    const log4c_layout_t*  	a_layout,
    const log4c_logging_event_t*a_event)
{
    static char buffer[1024];

    snprintf(buffer, sizeof(buffer), "logging %lu bytes.\n", strlen(a_event->evt_msg));
    return buffer;
}

/*******************************************************************************/
static const log4c_layout_type_t log4c_layout_type_test = {
  "test",
  test_format,
};

/*******************************************************************************/
static int test_append(log4c_appender_t* this, 
		       const log4c_logging_event_t* a_event)
{
    FILE* fp = log4c_appender_get_udata(this);

    return fprintf(fp, "[%s] %s", log4c_appender_get_name(this),
		   a_event->evt_rendered_msg);
}

/*******************************************************************************/
static const log4c_appender_type_t log4c_appender_type_test = {
  "test",
  NULL,
  test_append,
  NULL,
};

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
static int test00(sd_test_t* a_test, int argc, char* argv[])
{      
    int p;
    log4c_category_t*	cat = log4c_category_get("a category");

    for (p = 0; p < LOG4C_PRIORITY_UNKNOWN; p++)
	log4c_category_log(cat, p * 100, "this is a %s event", 
			   log4c_priority_to_string(p * 100));
    return 1;
}

/******************************************************************************/
static int test1(sd_test_t* a_test, int argc, char* argv[])
{   
    log4c_layout_t*   layout1   = log4c_layout_get("layout1");
    log4c_layout_t*   layout2   = log4c_layout_get("layout2");
    log4c_appender_t* appender  = log4c_appender_get(sd_test_get_name(a_test));
    log4c_appender_t* appender1 = log4c_appender_get("appender1");
    log4c_appender_t* appender2 = log4c_appender_get("appender2");
    test_category_udata_t *test_udata = sd_test_get_udata(a_test);

    log4c_layout_set_type(layout1, &log4c_layout_type_test);
    log4c_layout_set_type(layout2, &log4c_layout_type_test);

    test_udata->out = log4c_appender_get_udata(appender);
    log4c_appender_set_udata(appender, sd_test_out(a_test));

    log4c_appender_set_type(appender1,   &log4c_appender_type_test);
    log4c_appender_set_layout(appender1, layout1);
    test_udata->out = log4c_appender_get_udata(appender1);
    log4c_appender_set_udata(appender1,  sd_test_out(a_test));

    log4c_appender_set_type(appender2,   &log4c_appender_type_test);
    log4c_appender_set_layout(appender2, layout2);
    test_udata->out = log4c_appender_get_udata(appender2);
    log4c_appender_set_udata(appender2,  sd_test_out(a_test));

    log4c_category_set_appender(root, appender);
    log4c_category_set_appender(sub1, appender1);
    log4c_category_set_appender(sun1sub2, appender2);

    log4c_category_set_priority(root, LOG4C_PRIORITY_ERROR);

    log4c_print(sd_test_out(a_test));
    return 1;
}

#define foo(cat, level) \
{ \
    fprintf(sd_test_out(a_test), "\n# "#cat" "#level" (priority = %s)\n", \
	    log4c_priority_to_string(log4c_category_get_priority(cat))); \
    log4c_category_##level(cat, #cat" "#level); \
}

/******************************************************************************/
static int test2(sd_test_t* a_test, int argc, char* argv[])
{   
    foo(root, error);
    foo(root, warn);
    foo(sub1, error);
    foo(sub1, warn);
    foo(sun1sub2, error);
    foo(sun1sub2, warn);

    return 1;
}

/******************************************************************************/
static int test3(sd_test_t* a_test, int argc, char* argv[])
{   
    log4c_category_set_priority(sub1, LOG4C_PRIORITY_INFO);

    foo(root, error);
    foo(root, warn);
    foo(sub1, error);
    foo(sub1, warn);
    foo(sun1sub2, error);
    foo(sun1sub2, warn);

    return 1;
}

/******************************************************************************/
static int test4(sd_test_t* a_test, int argc, char* argv[])
{   
    log4c_category_set_priority(root, LOG4C_PRIORITY_TRACE);

    foo(root, info);
    foo(root, warn);
    foo(sub1, info);
    foo(sub1, warn);
    foo(sun1sub2, info);
    foo(sun1sub2, warn);
    return 1;
}
/******************************************************************************/
static int test5(sd_test_t* a_test, int argc, char* argv[])
{   
    int i, n;
    log4c_category_t* tab[10];

    n = log4c_category_list(tab, 10);
    if (n < 0)
	return 0;

    for (i = 0; i < n; i++)
	fprintf(sd_test_out(a_test), "cat[%d] = %s\n", i, 
		log4c_category_get_name(tab[i]));
    return 1;
}

/******************************************************************************/
static int test_restore(sd_test_t* a_test, int argc, char* argv[])
{
    log4c_appender_t* appender  = log4c_appender_get(sd_test_get_name(a_test));
    log4c_appender_t* appender1 = log4c_appender_get("appender1");
    log4c_appender_t* appender2 = log4c_appender_get("appender2");
    test_category_udata_t *test_udata = sd_test_get_udata(a_test);

    log4c_appender_set_udata(appender, test_udata->out);
    log4c_appender_set_udata(appender1, test_udata->out);
    log4c_appender_set_udata(appender2, test_udata->out);

    return 1;
}

/******************************************************************************/
int main(int argc, char* argv[])
{    
  int ret;
  sd_test_t* t = sd_test_new(argc, argv);

  log4c_init();

  root = log4c_category_get("root");
  sub1 = log4c_category_get("sub1");
  sun1sub2 = log4c_category_get("sub1.sub2"); 

    sd_test_set_udata(t, &test_udata);
    sd_test_add(t, test0);
    sd_test_add(t, test00);
    sd_test_add(t, test1);
    sd_test_add(t, test2);
    sd_test_add(t, test3);
    sd_test_add(t, test4);
    sd_test_add(t, test5);
    /* The test shares the file with the log4c stream appender and they would
     *  be both closed on exiting. This will restore original file descriptors
     *  in appenders to prevent double closing. */
    sd_test_add(t, test_restore);

    ret = sd_test_run(t, argc, argv);

    sd_test_delete(t);

    log4c_fini();
    return ! ret;
}
