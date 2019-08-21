#include "s5conf.h"
#include <stdio.h>
#include <asm-generic/errno-base.h>
#include <errno.h>
#include <gtest/gtest.h>

const char* file_name = "/etc/s5/conf.d/s5unittest.conf";

TEST(TESTConf, TestGet)
{
    const char* conf_name = file_name;
    const char* section = "global" ;
	const char* key = "db_path";
	const char* value;
	conf_file_t conf;

	conf = conf_open(conf_name);
	ASSERT_TRUE(conf);
    value = conf_get(conf, section, key);
	EXPECT_STREQ(value,"/etc/s5/s5meta.db");
}        

TEST(TESTConf, TestGetInt)
{
    const char* conf_name = file_name;
    const char* section = "daemon.0" ;
    const char* key = "front_port";
	conf_file_t conf;
	int value;
	int ret;
	
	conf = conf_open(conf_name);
	ASSERT_TRUE(conf);
	ret = conf_get_int(conf, section, key, &value);
	ASSERT_EQ(ret, 0);
	EXPECT_EQ(value,3000);
}

TEST(TESTConf, TestGetLong)
{
	const char* conf_name = file_name;
	const char* section = "daemon.0" ;
	const char* key = "front_port";
	long value;
	int ret;
	conf_file_t conf;

	conf = conf_open(conf_name);
	ASSERT_TRUE(conf);
	ret = conf_get_long(conf, section, key, &value);
	ASSERT_EQ(ret, 0);
	EXPECT_EQ(value,3000);
}

TEST(TESTConf, TestGetLong_F1) //failed case
{
	const char* conf_name = file_name;
	const char* section = "rge.0" ;
	const char* key = "front_port";
	long value;
	int ret;
	conf_file_t conf;

	conf = conf_open(conf_name);
	ASSERT_TRUE(conf);
	ret = conf_get_long(conf, section, key, &value);
	ASSERT_EQ(ret, -EINVAL);
}

TEST(TESTConf, TestGetLongLong)
{
	const char* conf_name = file_name;
	const char* section = "daemon.0" ;
	const char* key = "front_port";
	long long  value;
	int ret;
	conf_file_t conf;

	conf = conf_open(conf_name);
    ASSERT_TRUE(conf);
	ret = conf_get_longlong(conf, section, key, &value);
	ASSERT_EQ(ret, 0);
    EXPECT_EQ(value,3000);
}

TEST(TESTConf, TestGetLongLong_F1)
{
	const char* conf_name = NULL;
	const char* section = "daemon.0" ;
	const char* key = "for_fail";
	long long  value;
	int ret;
	conf_file_t conf;

	conf = conf_open(conf_name);
	EXPECT_EQ(NULL,conf);
	ret = conf_get_longlong(conf, section, key, &value);
	ASSERT_EQ(ret, -EINVAL);

}

TEST(TESTConf, TestGetLongLong_F2)
{
	const char* conf_name = file_name;
	const char* section = "daemon.0" ;
	const char* key="random";
	long long  value;
	int ret;
	conf_file_t conf;

	conf = conf_open(conf_name);
	ASSERT_TRUE(conf);
	ret = conf_get_longlong(conf, section, key, &value);
	EXPECT_EQ(ret, -EINVAL);
}

TEST(TESTConf, TestGetFloat)
{
	const char* conf_name = file_name;
	const char* section = "daemon.0" ;
	const char* key = "back_port";
	float  value;
	int ret;
	conf_file_t conf;

	conf = conf_open(conf_name);
	ASSERT_TRUE(conf);
	ret = conf_get_float(conf, section, key, &value);
	ASSERT_EQ(ret, 0);
    EXPECT_EQ(value,3000.000000);
}

TEST(TESTConf, TestGetDouble)
{
	const char* conf_name = file_name;
	const char* section = "daemon.0" ;
	const char* key = "front_port";
	double  value;
	int ret;
	conf_file_t conf;
	conf = conf_open(conf_name);
    ASSERT_TRUE(conf);

	ret = conf_get_double(conf, section, key, &value);
	ASSERT_EQ(ret, 0);
    EXPECT_EQ(value,3000.000000);
}

int main(int argc, char* argv[])
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
