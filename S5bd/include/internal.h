
#ifndef _S5_API_INTERNAL_HEADER_
#define _S5_API_INTERNAL_HEADER_

/**
 * Copyright (C), 2014-2015.
 * @file
 *
 * APIs for s5bd internal use are all defined here.
 *
 * @author xxx
 */


#include "s5log.h"


/**
 * This function is used to check whether a str is composed of letters or numbers or underlined spaces.
 *
 * @param[in]	str		string to check
 * @returns  1 if param 'str' is composed of letters or numbers or underlined spaces, otherwise, returns 0
 */
int s5_name_info_check(const char* str);

/**
 * Dump error info to log file and also set it to last error info buffer.
 *
 * Parameters of this function are variable parameters, just like parameters of 'printf'.
 *
 * @param[in]		fmt			format of parameters, parameters will be formated with it.
 * @param[in]		args		variable parameters
 */
#define S5LOG_AND_SET_ERROR(fmt,args...)							\
	({		\
		S5LOG_ERROR(fmt, ##args);		\
		set_error_str(fmt, ##args);	\
	})

/**
 * Key argument check with errors dump to both log.
 *
 * If key arg is NULL, or length of it is 0, or length exceeds max_len, macro will return -EINVAL,
 * and dump error info to log. Otherwise, macro will return 0.
 *
 * @param[in]		name		argument to check
 * @param[in]		max_len		max length of name, if its length exceeds max_len, -EINVAL will be returned.
 * @param[in]		arg_name	argument name in api, used for error info dump.
 * @param[in]		api_name	api name, used for error info dump.
 * @returns			0 on success and -EINVAL for errors detect.
 */
#define  key_args_check_with_info(name, max_len, arg_name, api_name)  			({	\
	int rc = 0;		\
	do{						\
		if (!name || strlen(name) == 0)		\
		{		\
			S5LOG_AND_SET_ERROR("Parameter '%s' for %s cannot be NULL or an empty string.", arg_name, api_name);		\
			rc = -EINVAL;		\
		}		\
		else if(strlen(name) >= max_len)		\
		{				\
			S5LOG_AND_SET_ERROR("Parameter '%s' for %s cannot exceed %d bytes.", arg_name, api_name, max_len);		\
			rc = -EINVAL;		\
		}		\
	}while(0);		\
	rc;		\
})

/**
 * Key argument check with errors dump to both log.
 *
 * If key arg length is 0, or length exceeds max_len, macro will return -EINVAL,
 * and dump error info to log. Otherwise, macro will return 0.
 * Here, arg is NULL will be taken as valid.
 *
 * @param[in]		name		argument to check
 * @param[in]		max_len		max length of name, if its length exceeds max_len, -EINVAL will be returned.
 * @param[in]		arg_name	argument name in api, used for error info dump.
 * @param[in]		api_name	api name, used for error info dump.
 * @returns			0 on success and -EINVAL for errors detect.
 */
#define  args_check_with_info(name, max_len, arg_name, api_name)  			({	\
	int rc = 0;		\
	do{						\
		if (name && strlen(name) == 0)		\
		{		\
			S5LOG_AND_SET_ERROR("Parameter '%s' for %s cannot be an empty string.", arg_name, api_name);		\
			rc = -EINVAL;		\
		}		\
		else if (name && (strlen(name) >= max_len))		\
		{		\
			S5LOG_AND_SET_ERROR("Parameter '%s' for %s cannot exceed %d bytes.", arg_name, api_name, max_len);		\
			rc = -EINVAL;		\
		}		\
	}while(0);	\
	rc;		\
})

/**
 * Key argument check with errors dump to both log and last error buffer.
 *
 * If key arg is NULL, or length of it is 0, or length exceeds max_len, macro will return -EINVAL,
 * and dump error info to log and last error buffer. Otherwise, macro will return 0.
 *
 * @param[in]		buf			buffer to which name value will be set
 * @param[in]		name		argument to check
 * @param[in]		max_len		max length of name, if its length exceeds max_len, -EINVAL will be returned.
 * @param[in]		arg_name	argument name in api, used for error info dump.
 * @param[in]		api_name	api name, used for error info dump.
 * @returns			0 on success and -EINVAL for errors detect.
 */
#define  key_args_check_and_set_with_info(buf, name, max_len, arg_name, api_name)  			({\
	int rc = 0;		\
	do{						\
		int ofs = 0;			\
		if (!name || strlen(name) == 0)		\
		{		\
			S5LOG_AND_SET_ERROR("Parameter '%s' for %s cannot be NULL or an empty string.", arg_name, api_name);		\
			rc = -EINVAL;		\
		}		\
		else if((ofs = snprintf(buf, max_len - 1, "%s", name)) > max_len - 1){				\
			S5LOG_AND_SET_ERROR("Parameter '%s' for %s cannot exceed %d bytes.", arg_name, api_name, max_len);		\
			rc = -EINVAL;		\
			ofs = 0;	\
		}		\
		buf[ofs] = 0;		\
	}while(0);		\
	rc;		\
})

/**
 * Argument check with errors dump to both log and last error buffer.
 *
 * If key arg length is 0, or length exceeds max_len, macro will return -EINVAL,
 * and dump error info to log and last error buffer. Otherwise, macro will return 0.
 * Here, arg is NULL will be taken as valid.
 *
 * @param[in]		buf			buffer to which name value will be set
 * @param[in]		name		argument to check
 * @param[in]		max_len		max length of name, if its length exceeds max_len, -EINVAL will be returned.
 * @param[in]		arg_name	argument name in api, used for error info dump.
 * @param[in]		api_name	api name, used for error info dump.
 * @returns			0 on success and -EINVAL for errors detect.
 */
#define  args_check_and_set_with_info(buf, name, max_len, arg_name, api_name)  			({	\
	int rc = 0;		\
	do{						\
		int ofs = 0;	\
		if (name && strlen(name) == 0)		\
		{		\
			S5LOG_AND_SET_ERROR("Parameter '%s' for %s cannot be an empty string.", arg_name, api_name);		\
			rc = -EINVAL;		\
		}		\
		else if (name && ((ofs = snprintf(buf, max_len - 1, "%s", name)) > max_len - 1))		\
		{		\
			S5LOG_AND_SET_ERROR("Parameter '%s' for %s cannot exceed %d bytes.", arg_name, api_name, max_len);		\
			rc = -EINVAL;		\
		}		\
		buf[ofs] = 0;		\
	}while(0);	\
	rc;		\
})

/**
 * Key argument check with no info dump.
 *
 * If key arg length is NULL or 0, or length exceeds max_len, macro will return -EINVAL. Otherwise,
 * macro will return 0.
 *
 * @param[in]		name		argument to check
 * @param[in]		max_len		max length of name, if its length exceeds max_len, -EINVAL will be returned.
 * @returns			0 on success and -EINVAL for errors detect.
 */
#define  key_args_check(name, max_len)  			({\
	int rc = 0;		\
	do{						\
		if (!name || strlen(name) == 0)		\
		{		\
			rc = -EINVAL;		\
		}		\
		else if(strlen(name) >= max_len){				\
			rc = -EINVAL;		\
		}		\
	}while(0);		\
	rc;		\
})

/**
 * Argument check with no errors dump.
 *
 * If arg length is 0, or length exceeds max_len, macro will return -EINVAL.
 * Otherwise, macro will return 0.
 * Here, arg is NULL will be taken as valid.
 *
 * @param[in]		name		argument to check
 * @param[in]		max_len		max length of name, if its length exceeds max_len, -EINVAL will be returned.
 * @returns			0 on success and -EINVAL for errors detect.
 */
#define  args_check(name, max_len)  			({	\
	int rc = 0;		\
	do{						\
		int ofs = 0;	\
		if (name && strlen(name) == 0)		\
		{		\
			rc = -EINVAL;		\
		}		\
		else if (name && (strlen(name) >= max_len))		\
		{		\
			rc = -EINVAL;		\
		}		\
	}while(0);	\
	rc;		\
})

/**
 * Key argument check with no info dump, and if no errors occur, arg value will be set to buffer
 * specified.
 *
 * If key arg length is 0, or length exceeds max_len, macro will return -EINVAL. Otherwise,
 * macro will return 0.
 *
 * @param[in]		buf			destination buffer, to which if no errors occur, value of 'name' will be set
 * @param[in]		name		argument to check
 * @param[in]		max_len		max length of name, if its length exceeds max_len, -EINVAL will be returned.
 * @returns			0 on success and -EINVAL for errors detect.
 */
#define  key_args_check_and_set(buf, name, max_len)  			({\
	int rc = 0;		\
	do{						\
		int ofs = 0;			\
		if (!name || strlen(name) == 0)		\
		{		\
			rc = -EINVAL;		\
		}		\
		else if((ofs = snprintf(buf, max_len - 1, "%s", name)) > max_len - 1){				\
			rc = -EINVAL;		\
			ofs = 0;	\
		}		\
		buf[ofs] = 0;		\
	}while(0);		\
	rc;		\
})

/**
 * Argument check with no info dump, and if no errors occur, arg value will be set to buffer specified.
 *
 * If key arg length is 0, or length exceeds max_len, macro will return -EINVAL. Otherwise,
 * macro will return 0.
 *
 * @param[in]		buf			destination buffer, to which if no errors occur, value of 'name' will be set
 * @param[in]		name		argument to check
 * @param[in]		max_len		max length of name, if its length exceeds max_len, -EINVAL will be returned.
 * @returns			0 on success and -EINVAL for errors detect.
 */
#define  args_check_and_set(buf, name, max_len)  			({	\
	int rc = 0;		\
	do{						\
		int ofs = 0;	\
		if (name && strlen(name) == 0)		\
		{		\
			rc = -EINVAL;		\
		}		\
		else if (name && ((ofs = snprintf(buf, max_len - 1, "%s", name)) > max_len - 1))		\
		{		\
			rc = -EINVAL;		\
		}		\
		buf[ofs] = 0;		\
	}while(0);	\
	rc;		\
})



/**
 * Set error info, and info set can be retrived with api 'get_last_error_str'.
 *
 * This function is only for S5(only s5bd and s5manager) internal use. Characters count of error info (not including
 * the trailing '\\0' used to end output to strings) printed to error info buffer will be returned if no error occurs.
 * If error info is too long to printed error info buffer, '-EOVERFLOW' will be returned.
 * @param[in]	fmt		error info format
 * @returns	number of characters of error info set (not including the trailing '\\0' used to end output to strings) if
 * function successfully returns. And '-EOVERFLOW' will be returned if error info is too long to printed error info
 * buffer.
 */
int set_error_str(const char *fmt , ...);
#endif
