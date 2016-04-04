/*
 * test_big.c
 *
 * Test for SourceForge bug #3022803 - problem with variable argument lists
 * for strings longer than 1024 characters.
 *
 * This test logs into /dev/null.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include <log4c.h>
#include <sd/test.h>

static log4c_category_t* root = NULL;
static log4c_appender_t* appender = NULL;

static int test_big(sd_test_t* a_test, int argc, char *argv[]) {
	char *buf;
	size_t i;

	buf = malloc(1500);
	for (i = 0; i < 1500-1; i++) buf[i] = 'A';
	buf[i] = 0;

	log4c_category_log(root, LOG4C_PRIORITY_INFO, "%s", buf);

	free(buf);

	return 1;
}

int main(int argc, char *argv[]) {
	sd_test_t *t;
	int ret;
	FILE *f;

	t = sd_test_new(argc, argv);
	log4c_init();

	appender = log4c_appender_get("stream");
	f = fopen("/dev/null", "w+");
	log4c_appender_set_udata(appender, f);

	root = log4c_category_get("root");
	log4c_category_set_appender(root, appender);
	log4c_category_set_priority(root, LOG4C_PRIORITY_TRACE);

	sd_test_add(t, test_big);

	ret = sd_test_run(t, argc, argv);

	log4c_appender_set_udata(appender, NULL);
	fclose(f);

	sd_test_delete(t);

	log4c_fini();
	return ! ret;
}
