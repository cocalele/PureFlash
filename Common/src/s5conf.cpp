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
#include "s5log.h"


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
		S5LOG_ERROR("Failed to parse file[%s] failed! ret[%d]", conf_name, ret);
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
const char *conf_get(conf_file_t conf, const char *section, const char *key)
{
	CHECK_PARAM_NOT_NULL_OR_RETURN(conf, NULL);
	CHECK_PARAM_NOT_NULL_OR_RETURN(section, NULL);
	CHECK_PARAM_NOT_NULL_OR_RETURN(key, NULL);

	ConfFile *cf = static_cast<ConfFile *>(conf);
	string section_name(section);
	string key_name(key);
	string *value_str = NULL;

	int ret = cf->read(section, key, &value_str);
	if(ret < 0)
	{
		errno = -ret;
		S5LOG_WARN("Failed to conf_get char section[%s], key[%s] failed! ret[%d]", section, key, ret);
		return NULL;
	}

	return  value_str->c_str();

}

extern "C"
int conf_get_int(conf_file_t conf, const char *section, const char *key, int *value)
{
	CHECK_PARAM_NOT_NULL_OR_RETURN(conf, -EINVAL);
	CHECK_PARAM_NOT_NULL_OR_RETURN(section, -EINVAL);
	CHECK_PARAM_NOT_NULL_OR_RETURN(key, -EINVAL);

	const char *buf =  conf_get(conf, section, key);
	if(!buf)
	{
		S5LOG_WARN("Failed to conf_get int section[%s], key[%s] failed! ", section, key);
		return -EINVAL;
	}

	std::string err;
	*value = strict_strtol(buf, 10, &err);
	if(!err.empty())
	{
		S5LOG_WARN("Failed to strtol buf[%s], err[%s]", buf, err.c_str()) ;
		return -EINVAL;
	}
	return 0;
}

extern "C"
int conf_get_long(conf_file_t conf, const char *section, const char *key, long *value)
{
	CHECK_PARAM_NOT_NULL_OR_RETURN(conf, -EINVAL);
	CHECK_PARAM_NOT_NULL_OR_RETURN(section, -EINVAL);
	CHECK_PARAM_NOT_NULL_OR_RETURN(key, -EINVAL);

	const char *buf  = conf_get(conf, section, key);
	if(!buf)
	{
		S5LOG_WARN("Failed to conf_get long section[%s], key[%s] failed!", section, key);
		return -EINVAL;
	}

	std::string err;
	*value = strict_strtol(buf, 10, &err);
	if(!err.empty())
	{
		S5LOG_ERROR("Failed to get strtol buf[%s], err[%s]", buf, err.c_str()) ;
		return -EINVAL;
	}
	return 0;
}

extern "C"
int conf_get_longlong(conf_file_t conf, const char *section, const char *key, long long *value)
{
	CHECK_PARAM_NOT_NULL_OR_RETURN(conf, -EINVAL);
	CHECK_PARAM_NOT_NULL_OR_RETURN(section, -EINVAL);
	CHECK_PARAM_NOT_NULL_OR_RETURN(key, -EINVAL);

	const char *buf =  conf_get(conf, section, key);
	if(!buf)
	{
		S5LOG_WARN("Failed to do conf_get longlong section[%s], key[%s]", section, key);
		return -EINVAL;
	}

	std::string err;
	*value = strict_strtoll(buf, 10, &err);
	if(!err.empty())
	{
		S5LOG_ERROR("Failed to get strtol buf[%s], err[%s]", buf, err.c_str()) ;
		return -EINVAL;
	}
	return 0;
}

extern "C"
int conf_get_double(conf_file_t conf, const char *section, const char *key, double *value)
{
	CHECK_PARAM_NOT_NULL_OR_RETURN(conf, -EINVAL);
	CHECK_PARAM_NOT_NULL_OR_RETURN(section, -EINVAL);
	CHECK_PARAM_NOT_NULL_OR_RETURN(key, -EINVAL);

	const char *buf =  conf_get(conf, section, key);
	if(!buf)
	{
		S5LOG_WARN("Failed to conf_get longlong section[%s], key[%s] failed!", section, key);
		return -EINVAL;
	}

	std::string err;
	*value = strict_strtod(buf, &err);
	if(!err.empty())
	{
		S5LOG_ERROR("Failed to get strtol buf[%s], err[%s]", buf, err.c_str()) ;
		return -EINVAL;
	}
	return 0;
}

extern "C"
int conf_get_float(conf_file_t conf, const char *section, const char *key, float *value)
{
	CHECK_PARAM_NOT_NULL_OR_RETURN(conf, -EINVAL);
	CHECK_PARAM_NOT_NULL_OR_RETURN(section, -EINVAL);
	CHECK_PARAM_NOT_NULL_OR_RETURN(key, -EINVAL);

	const char *buf =  conf_get(conf, section, key);
	if(!buf)
	{
		S5LOG_WARN("Failed to get conf_get longlong section[%s], key[%s] failed!", section, key);
		return -EINVAL;
	}

	std::string err;
	*value = strict_strtof(buf, &err);
	if(!err.empty())
	{
		S5LOG_ERROR("Failed to get strtol buf[%s], err[%s]", buf, err.c_str()) ;
		return -EINVAL;
	}
	return 0;
}


