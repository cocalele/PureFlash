#ifndef __S5CONF_H__
#define __S5CONF_H__

/**
 * Copyright (C), 2014-2015.
 * @file
 *
 * Configuration parse module for S5 modules.
 *
 * All apis and macros about S5 configuration are all defined here.
 *
 * @author xxx
 */
#include "basetype.h"

typedef void* conf_file_t;		///< configuration object

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Open and parse S5 configuration file.
 *
 * This function is used to open and parse S5 configuration file, or other configuration file with same syntax rules with
 * S5. If function returns successfully, a structured configuration object of 'conf_file_t' will be
 * returned. User can long term retain it until call 'conf_close' to close and release it. User should call 'conf_close'
 * to release the structured configuration object to avoid memory leak.
 * If errors occur in process, NULL will be returned and errno is set appropriately.To get detailed error info, user
 * can turn to log of s5bd or check errorno.
 *
 * Errnor:
 * @li	\b -EINVAL				The mode of S5 configuration file is invalid; size of configuration exceeds 0x40000000 bytes.
 * @li	\b -EACCES				Search permission is denied for one of the directories in the path prefix of
 *								S5 configuration file path.
 * @li	\b -ENOMEM				Out of memory (i.e., kernel memory).
 * @li	\b -EOVERFLOW			path refers to a file whose size cannot be represented in the type off_t.
 *								This can occur when an application compiled on a 32-bit platform without
 *								-D_FILE_OFFSET_BITS=64 calls stat() on a file whose size exceeds (2<<31)-1 bits.
 * @li	\b -EIO					unexpected EOF while reading configuration file, possible concurrent modification may cause it.
 * @li	\b -S5_INTERNAL_ERR		errors may be caused by internal bugs of S5, user can refer to log and S5 manual for help.
 * @li	\b -S5_CONF_ERR			configuration file does not conform to configuration rules
 *
 *
 * @param[in]	conf_name	configuration file path
 * @returns		NULL if error occurs, and errno is set appropriately. Otherwise, a structured configuration object of 'conf_file_t' will
 *				be returned.
 */
conf_file_t conf_open(const char* conf_name);

/**
 * Close and release S5 configuration object.
 *
 * @param[in]	conf	configuration objcet
 * @returns		0 on success.
 */
int conf_close(conf_file_t conf);

/**
 * Get string type value from configuration object.
 *
 * This function is used to get string type value from S5 configuration object. Configuration object (conf) must be
 * result of a successful run of 'conf_open'. Or else, unexpected results will occur. If function successfully returns,
 * string value of specified tag(key) in section(section) specified will be returned with a const char pointer. This
 * pointer can be long term retained until user call 'conf_close' to free the configuration object.
 * log of s5bd.
 *
 * If requested config is not found in file, his function will:
 *   - LOG_FATAL(i.e. terminate application) if `fatal_on_error` is TRUE
 *   - return `def_val` if `fatal_on_error` is FALSE
 * Errno:
 * @li \b	-ENOENT		section or key specified cannot be found in configuration data
 *
 * @param[in]	conf			configuration file object
 * @param[in]	section			name of section
 * @param[in]	key				name of tag in section specified
 * @param[in]	    def_value		default value to return if config not found and `fatal_on_error` is FALSE
 * @param[in]	    fatal_on_error	whether to LOG_FATAL if config not found
 * @returns		string value of config item, or def_value if config not found.
 */
const char* conf_get(conf_file_t conf, const char* section, const char* key, const char* def_val, BOOL fatal_on_error);

/**
 * Get int type value from configuration object.
 *
 * This function is used to get int type value from S5 configuration object. Configuration object (conf) must be
 * result of a successful run of 'conf_open'. Or else, unexpected results will occur. If function successfully returns,
 * int value of specified tag(key) in section(section) specified will be returned.
 * This function will always LOG_FATAL if the configuration is found, but its value is not a valid int. This behavior it
 * to help user found wrong configuration in file.
 * If requested config is not found in file, his function will:
 *   - LOG_FATAL(i.e. terminate application) if `fatal_on_error` is TRUE
 *   - return `def_val` if `fatal_on_error` is FALSE
 *
 * @param[in]		conf			configuration file object
 * @param[in]		section			name of section
 * @param[in]		key				name of tag in section specified
 * @param[in]	    def_value		default value to return if config not found and `fatal_on_error` is FALSE
 * @param[in]	    fatal_on_error	whether to LOG_FATAL if config not found
 * @returns		value of configuration item.
 */
int conf_get_int(conf_file_t conf , const char* section, const char* key, int def_val, BOOL fatal_on_error);
long conf_get_long(conf_file_t conf , const char* section, const char* key, long def_val, BOOL fatal_on_error);


/**
 * Get double type value from configuration object.
 *
 * This function is used to get double type value from S5 configuration object. Configuration object (conf) must be
 * result of a successful run of 'conf_open'. Or else, unexpected results will occur. If function successfully returns,
 * int value of specified tag(key) in section(section) specified will be returned.
 * This function will always LOG_FATAL if the configuration is found, but its value is not a valid double. This behavior it
 * to help user found wrong configuration in file.
 * If requested config is not found in file, his function will:
 *   - LOG_FATAL(i.e. terminate application) if `fatal_on_error` is TRUE
 *   - return `def_val` if `fatal_on_error` is FALSE
 *
 * @param[in]		conf			configuration file object
 * @param[in]		section			name of section
 * @param[in]		key				name of tag in section specified
 * @param[in]	    def_value		default value to return if config not found and `fatal_on_error` is FALSE
 * @param[in]	    fatal_on_error	whether to LOG_FATAL if config not found
 * @returns		value of configuration item.
 */
double conf_get_double(conf_file_t conf, const char* section, const char* key, double def_val, BOOL fatal_on_error);


#ifdef __cplusplus
}
#endif





#endif
