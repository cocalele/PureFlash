#include </usr/include/errno.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include "cmdopt.h"


static int errorCode = 0;  //the error code
static char errorMsg[4096]; //string error message describe the error code
#define MAX_ARGNUM 1024
static const char* argName[MAX_ARGNUM]; //address of the each argument name
static const char** argValue;  //copy of argv
static int argCount;	//argc
static int iter_index;
static const char* name; //pointer to parameter name
static const char* value; //pointer to parameter value


static const char* paramName(const char* arg)
{
	if(arg == NULL)
		return NULL;
	int len = (int)strlen(arg);
	if(len == 0 || arg[0] != '-')
		return NULL;

	const char* p = arg+1;
	if(p[0] == '-')
		p++;
	return p;
}

int opt_initialize(int argc, const char** argv)
{
	errorCode = 0;
	if(argc>MAX_ARGNUM)
	{
		errorCode = OPT_TOOMANYARG;
		strcpy(errorMsg, "Too many argument in the command line. Maximum of 1024 are allowed");
		return errorCode;
	}
	for(int i=1; i < argc; i++)
	{
		argName[i] = paramName(argv[i]);
	}
	
	argCount = argc;
	argValue = argv;
	
	iter_index = 0;
	return errorCode;
}

int opt_has_next()
{
	errorCode = 0;

	iter_index++;
	if(iter_index < argCount)
	{
		name = argName[iter_index];
		value = NULL;
		return 1;
	}
	return 0;
}

const char* opt_next()
{	
	errorCode = 0;
	return name;
}

const char* opt_value()
{
	errorCode = 0;
	if(value == NULL)
	{
		if(name == NULL && iter_index < argCount)
		{
			value = argValue[iter_index];
			return value;
		}
		int next = iter_index+1;
		if( next < argCount && argName[next] == NULL)
		{
			iter_index = next;
			value = argValue[next];
			return value;
		}
	}
	return value;
}

int opt_value_as_int()
{
	errno  = 0;
	char* end=NULL;
	const char* dst_val = opt_value();
	if (!dst_val)
	{
		return -ENOENT;
	}
	
	int v = (int)strtol(dst_val, &end, 10);
	if(errno || end == value)
	{
		errorCode = OPT_FAILCONVERT;
		sprintf(errorMsg, "Fail to convert argument to integer: '%s=%s'", name, value);
	}
	return v;
}

long long opt_value_as_ull()
{
	errno  = 0;
	char* end=NULL;
	const char* dst_val = opt_value();
	if (!dst_val)
	{
		return -ENOENT;
	}
	long long v = (long long)strtoull(dst_val, &end, 10);
	if(errno || end == value)
	{
		errorCode = OPT_FAILCONVERT;
		sprintf(errorMsg, "Fail to convert argument to integer: '%s=%s'", name, value);
	}
	return v;
}


double opt_value_as_double()
{
	errno  = 0;
	char* end = NULL;
	const char* dst_val = opt_value();
	if (!dst_val)
	{
		return -ENOENT;
	}
	double v = strtod(dst_val, &end);
	if(errno || end == value)
	{
		errorCode = OPT_FAILCONVERT;
		sprintf(errorMsg, "Fail to convert argument to double: '%s=%s'", name, value);
	}
	return v;

}

int opt_value_as_bool()
{
	errorCode = 0;
	const char* p = opt_value();
	if(!p)
		return 1;
	if(!( strcasecmp(p, "yes") 
		&& strcasecmp(p, "true")
		&& strcasecmp(p, "1")
		))
		return 1;
	return 0;
}

int opt_error_code()
{
	return errorCode;
}

const char* opt_error_message()
{
	if( !errorCode )
		return "Operation OK";
	return errorMsg;
}

void opt_uninitialize()
{
}
