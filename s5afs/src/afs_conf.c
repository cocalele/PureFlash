#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <asm-generic/errno-base.h>
#include <errno.h>

#include "s5conf_utils.h"
#include "s5conf.h"
#include "s5strtol.h"


#ifndef LOG_ERROR
#define LOG_ERROR printf
#endif

#define CHECK_PARAM_NOT_NULL_OR_RETURN(name,rval) \
do{              \
	if(name == NULL){         \
		LOG_ERROR("param [%s] is null\n", #name); \
		return rval;        \
	}          \
}while(0)

#define _EXTERN_C /*extern "C"*/

_EXTERN_C
conf_file_t conf_open(const char* conf_name)
{
         CHECK_PARAM_NOT_NULL_OR_RETURN(conf_name,NULL);
	 CONFFILE* cf = malloc(sizeof(CONFFILE));
	 ZERO_MEM(cf, sizeof(CONFFILE));
	 char *errors = (char *)malloc(1024);
	 const char *fname = conf_name;

	 int ret = cf_parse_file(cf, fname, errors, NULL);
         if(ret != 0){
                LOG_ERROR("parse file[%s] failed! ret[%d]\n", conf_name, ret);
		free((void*)errors);
                return NULL;
         }
	 free((void*)errors);
	 return (conf_file_t)cf;
}

_EXTERN_C
int conf_close(conf_file_t conf)
{
    /*
    * attention: we can't destroy the cf object,
      assert(conf != NULL);
    */

    //why we can't destroy the cf obj??
    cf_free((CONFFILE**)&conf);
    return 0;
}

_EXTERN_C
const char* conf_get(conf_file_t conf, const char* section, const char* key)
{
	 CHECK_PARAM_NOT_NULL_OR_RETURN(conf, NULL);
         //CHECK_PARAM_NOT_NULL_OR_RETURN(section, NULL);
         CHECK_PARAM_NOT_NULL_OR_RETURN(key, NULL);

	 CONFFILE* cf = (CONFFILE *)conf;
	 const char *value_str = NULL;

	 int ret = -1;
	 if (!section || strlen(section)==0)
	 {
	 	ret = cf_read(cf, DEFAULT_SEC, key, &value_str);
	 }
	 else
	 {
	 	ret = cf_read(cf, section, key, &value_str);
	 }

	 if(ret < 0){
	      LOG_ERROR("conf_get char section[%s], key[%s] failed! ret[%d]\n", section, key, ret);
	      return NULL;
	 }

	return  value_str;
}

_EXTERN_C
int conf_get_int(conf_file_t conf, const char* section, const char* key, int* value)
{
	CHECK_PARAM_NOT_NULL_OR_RETURN(conf, -EINVAL);
        //CHECK_PARAM_NOT_NULL_OR_RETURN(section, -EINVAL);
        CHECK_PARAM_NOT_NULL_OR_RETURN(key, -EINVAL);

	const char* buf =  conf_get(conf, section, key);
	if(!buf){
	    LOG_ERROR("conf_get int section[%s], key[%s] failed! \n", section, key);
	    return -EINVAL;
	}

	char err[300];
	err[0] = 0;
	*value = strict_strtol(buf, 10, err);
	if(strlen(err) != 0){
	    LOG_ERROR("strtol buf[%s], err[%s]\n", buf, err) ;
	    return -EINVAL;
	}
	return 0;
}

_EXTERN_C
int conf_get_long(conf_file_t conf, const char* section, const char* key, long* value)
{
	CHECK_PARAM_NOT_NULL_OR_RETURN(conf, -EINVAL);
        //CHECK_PARAM_NOT_NULL_OR_RETURN(section, -EINVAL);
        CHECK_PARAM_NOT_NULL_OR_RETURN(key, -EINVAL);

	const char* buf  = conf_get(conf, section, key);
        if(!buf){
            LOG_ERROR("conf_get long section[%s], key[%s] failed!\n", section, key);
            return -EINVAL;
        }

        char err[300];
	err[0] = 0;
        *value = strict_strtol(buf, 10, err);
	if(strlen(err) != 0){
            LOG_ERROR("strtol buf[%s], err[%s]\n", buf, err) ;
            return -EINVAL;
        }
	return 0;
}

_EXTERN_C
int conf_get_longlong(conf_file_t conf, const char* section, const char* key, long long* value)
{
	CHECK_PARAM_NOT_NULL_OR_RETURN(conf, -EINVAL);
        //CHECK_PARAM_NOT_NULL_OR_RETURN(section, -EINVAL);
        CHECK_PARAM_NOT_NULL_OR_RETURN(key, -EINVAL);

        const char* buf =  conf_get(conf, section, key);
        if(!buf){
            LOG_ERROR("conf_get longlong section[%s], key[%s] failed!\n", section, key);
            return -EINVAL;
        }

	char err[300];
	err[0] = 0;
        *value = strict_strtoll(buf, 10, err);
	if(strlen(err) != 0){
            LOG_ERROR("strtol buf[%s], err[%s]\n", buf, err);
            return -EINVAL;
        }
        return 0;
}

_EXTERN_C
int conf_get_double(conf_file_t conf, const char* section, const char* key, double* value)
{
	CHECK_PARAM_NOT_NULL_OR_RETURN(conf, -EINVAL);
        //CHECK_PARAM_NOT_NULL_OR_RETURN(section, -EINVAL);
        CHECK_PARAM_NOT_NULL_OR_RETURN(key, -EINVAL);

        const char* buf =  conf_get(conf, section, key);
        if(!buf){
            LOG_ERROR("conf_get longlong section[%s], key[%s] failed!\n", section, key);
            return -EINVAL;
        }

	char err[300];
	err[0] = 0;
        *value = strict_strtod(buf, err);
	if(strlen(err) != 0){
	    LOG_ERROR("strtol buf[%s], err[%s]\n", buf, err);
            return -EINVAL;
        }
        return 0;
}

_EXTERN_C
int conf_get_float(conf_file_t conf, const char* section, const char* key, float* value)
{
	CHECK_PARAM_NOT_NULL_OR_RETURN(conf, -EINVAL);
        //CHECK_PARAM_NOT_NULL_OR_RETURN(section, -EINVAL);
        CHECK_PARAM_NOT_NULL_OR_RETURN(key, -EINVAL);

        const char* buf =  conf_get(conf, section, key);
        if(!buf){
            LOG_ERROR("conf_get longlong section[%s], key[%s] failed!\n", section, key);
            return -EINVAL;
        }

	char err[300];
	err[0] = 0;
        *value = strict_strtof(buf, err);
	if(strlen(err) != 0){
            LOG_ERROR("strtol buf[%s], err[%s]\n", buf, err);
            return -EINVAL;
        }
        return 0;
}

int conf_print(conf_file_t conf)
{
	cf_print((CONFFILE*)conf);
	return 0;
}

