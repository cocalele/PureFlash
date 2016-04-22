#ifndef spy_h__
#define spy_h__
#include <stdint.h>

/**
* Copyright (C), 2014-2015.
* @file
* spy C API.
*
* This file includes all spy's data structures and APIs.
*/

#ifdef __cplusplus
extern "C" {
#endif

#define IO_STAT_IOPS			"iops"			///< common prefixion for iops
#define IO_STAT_BANDWIDTH		"band_width"	///< common prefixion for band_width
#define IO_STAT_HITRATIO		"hit_ratio"		///< common prefixion for hit_ratio
#define IO_STAT_AVERAGE_LATENCY	"average_latency"	///< common prefixion for average_latency

#define STAT_IO_TIME_INTERVAL   2		///< interval.
#define STAT_VAR_LEN            150		///< max length of statistic variable.
#define	SPY_BUF_LEN_MAX			4096	///< length of statistic buffer.

/**
 * type of spy's variable.
 */
enum variable_type
{
	vt_uint32,	///< unsigned int 32bits.
	vt_int32,	///< signed int 32bits.
	vt_int64,	///< signed int 64bits.
	/**
	 * signed int 64bits.
	 * register vt_prop_int64 together with property_getter(defined as below) by spy_register_property,
	 * property_getter use to get the value of vt_prop_int64.
	 */
	vt_prop_int64,
	vt_cstr,	///< cstr
	vt_float,	///< float
	vt_write_property
};

/**
 * the type of pointer to function, get the value of vt_prop_int64.
 */
typedef int64_t (*property_getter)(void* obj);

/**
 * callback to execute write_property operation
 * @param[in]  obj, object to call write property ton
 * @param[in] cmd_line, the command line from spy client. 
 *            generally, user use spy command like this:
 *               # spy <target_ip> <port> write <variable_name> <ctx_id> <iops> <bandwidth>
 *		      for exmaple:
 *               # spy 192.168.0.59 2003 write runtime_info 00ff0020 1000 2000
 *            cmd_line include the parts variable_name and following arguments, e.g. for above example, cmd_line is a string:
 *               runtime_info 00ff0020 1000 2000
 */
typedef int (*property_setter)(void* obj, void* cmd_line);

/**
 * start spy server synchronously, make sure the handle thread had entered and port had been binded.
 *
 * used in spy server.
 * start listen port to receive requests.
 * create a thread to handle request of spy client.
 * reply the request refer to spy client registered variables.
 * @param[in] port	spy server listen port, may be increase if it is occupied.
 */
void spy_sync_start(int port);

/**
 * start spy server.
 *
 * used in spy server.
 * start listen port to receive requests.
 * create a thread to handle request of spy client.
 * reply the request refer to spy client registered variables.
 * @param[in] spy_port	spy server listen port, may be increase if it is occupied.
 */
void spy_start(int spy_port);


/**
 * get the listen port of spy server.
 *
 * used in spy server.
 */
unsigned short spy_get_port();

/**
 * register a variable to be read on run time.
 *
 * used in spy client.
 * @param[in] name variable's name, spy client use this name to read this variable
 * @param[in] var_addr	variable's address, must be a global available memory that can
 *                  be access at any time
 * @param[in] var_type	variable's type, vt_uint32, vt_int32, vt_int64 or vt_float
 * @param[in] comment	comments for variable.
 */
void spy_register_variable(const char* name, const void* var_addr, enum variable_type var_type, const char* comment);

/**
 * register a property.
 * A property is a value accessed via a getter function. For example, *depth* is a property of queue
 * to get a queue's depth, one should use code:
 *        int64_t depth = queue_get_depth(queue);
 * to register such a property, call spy_register_property like
 *        int64_t s5_get_qdepth(s5session* s5s);
 *        spy_register_property("s5_qdepth", s5_get_qdepth, s5session, vt_prop_int64);
 * so far, only property type int64_t is supported. i.e. prop_type must be vt_prop_int64
 * used in spy client.
 * @param[in] name 		variable's name, spy client use this name to read this variable.
 * @param[in] get_func	pointer function of property_getter to get the value of variable.
 * @param[in] obj_to_get	variable's address, must be a global available memory that can
 *                  be access at any time.
 * @param[in] prop_type	variable's type, vt_prop_int64.
 * @param[in] comment	comments for variable.
 */
void spy_register_property_getter(const char* name,
                           property_getter get_func,
                           void* obj_to_get,
                           enum variable_type prop_type,
                           const char* comment
                          );


/**
 * register a property setter.
 * A property is a value accessed via a setter function. For example, *depth* is a property of queue
 * to set a queue's depth, one should use code:
 *        int64_t depth = queue_set_depth(queue);
 * to register such a property, call spy_register_property like
 *        int64_t s5_set_qdepth(s5session* s5s);
 *        spy_register_property("s5_qdepth", s5_set_qdepth, s5session, vt_prop_int64);
 * so far, only property type int64_t is supported. i.e. prop_type must be vt_prop_int64
 * used in spy client.
 * @param[in] name 		variable's name, spy client use this name to read this variable.
 * @param[in] set_func	pointer function of property_setter to set the value of variable.
 * @param[in] obj_to_set	variable's address, must be a global available memory that can
 *                  be access at any time.
 * @param[in] prop_type	variable's type, vt_prop_int64.
 * @param[in] comment	comments for variable.
 */
void spy_register_property_setter(const char* name,
                           property_setter set_func,
                           void* obj_to_set,
                           enum variable_type prop_type,
                           const char* comment
                          );

/**
 * unregister a variable.
 *
 * used in spy client.
 * @param[in] name 		variable's name, spy client use this name to read this variable.
 */
void spy_unregister(const char* name);
#ifdef __cplusplus
}
#endif

#endif // spy_h__
