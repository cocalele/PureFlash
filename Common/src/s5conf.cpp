#include <string>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <iostream>
using namespace std;

#include <asm-generic/errno-base.h>
#include <errno.h>

#include "s5conf_utils.h"
#include "s5conf.h"
#include "s5strtol.h"
#include "s5_log.h"


#define CHECK_PARAM_NOT_NULL_OR_RETURN(name,rval) \
do{              \
	if(name == NULL){         \
		S5LOG_ERROR("Invalid param [%s] is null", #name); \
		return rval;        \
	}          \
}while(0)



extern "C"
conf_file_t conf_open(const char *conf_name)
{
	CHECK_PARAM_NOT_NULL_OR_RETURN(conf_name, NULL);

	ConfFile *cf = new ConfFile(conf_name);

	int  ret = cf->parse_file();
	if(ret != 0)
	{
		S5LOG_ERROR("Failed to parse file:%s failed!rc:%d", conf_name, ret);
		delete cf;
		errno = -ret;
		return NULL;
	}
	return (conf_file_t)cf;
}

extern "C"
int conf_close(conf_file_t conf)
{
	ConfFile *cf = (ConfFile *)conf;
	if(cf != NULL)
	    delete cf;
	return 0;
}


extern "C"
const char *conf_get(conf_file_t conf, const char *section, const char *key, const char* def_val, BOOL fatal_on_error)
{
	S5ASSERT(conf != NULL);
	S5ASSERT(section != NULL);
	S5ASSERT(key != NULL);

	ConfFile *cf = static_cast<ConfFile *>(conf);
	string section_name(section);
	string key_name(key);
	string *value_str = NULL;

	int ret = cf->read(section, key, &value_str);
	if(ret < 0)
	{
		errno = -ret;
		if (fatal_on_error)
			S5LOG_FATAL("Configuration not found! section:%s, key:%s", section, key);
		return def_val;
	}

	return  value_str->c_str();

}

extern "C"
int conf_get_int(conf_file_t conf, const char *section, const char *key, int def_val, BOOL fatal_on_error)
{
	S5ASSERT(conf != NULL);
	S5ASSERT(section != NULL);
	S5ASSERT(key != NULL);

	const char *buf =  conf_get(conf, section, key, NULL, fatal_on_error);
	if(!buf)
	{
		return def_val;
	}

	std::string err;
	int value = strict_strtol(buf, 10, &err);
	if(!err.empty())
	{
		errno = EINVAL;
		S5LOG_FATAL("Failed to strtol buf[%s], err[%s]", buf, err.c_str()) ;
		return def_val;
	}
	return value;
}

extern "C"
long conf_get_long(conf_file_t conf, const char *section, const char *key, long def_val, BOOL fatal_on_error)
{
	S5ASSERT(conf != NULL);
	S5ASSERT(section != NULL);
	S5ASSERT(key != NULL);

	const char *buf =  conf_get(conf, section, key, NULL, fatal_on_error);
	if(!buf)
	{
		return def_val;
	}

	std::string err;
	long value = strict_strtoll(buf, 10, &err);
	if(!err.empty())
	{
		errno = EINVAL;
		S5LOG_FATAL("Failed to strtol buf[%s], err[%s]", buf, err.c_str()) ;
		return def_val;
	}
	return value;
}


extern "C"
double conf_get_double(conf_file_t conf, const char *section, const char *key, double def_val, BOOL fatal_on_error)
{
	S5ASSERT(conf != NULL);
	S5ASSERT(section != NULL);
	S5ASSERT(key != NULL);

	const char *buf = conf_get(conf, section, key, NULL, fatal_on_error);
	if (!buf)
	{
		return def_val;
	}

	std::string err;
	double value = strict_strtod(buf, &err);
	if (!err.empty())
	{
		errno = EINVAL;
		S5LOG_FATAL("Failed to strtol buf[%s], err[%s]", buf, err.c_str());
		return def_val;
	}
	return value;
}


