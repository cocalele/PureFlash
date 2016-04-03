#ifndef spy_h__
#define spy_h__
#include "s5k_basetype.h"
#ifdef __cplusplus
extern "C" {
#endif

#define IO_STAT_IOPS			"iops"
#define IO_STAT_BANDWIDTH		"band_width"
#define IO_STAT_HITRATIO		"hit_ratio"
#define IO_STAT_AVERAGE_LATENCY	"average_latency"

#define STAT_IO_TIME_INTERVAL   2
#define STAT_VAR_LEN            150
#define	SPY_BUF_LEN_MAX			4096

#define	FLOAT_PRECISION			(10000)
#define POINT_LEFT(addr)			((long)*(double*)addr)
#define POINT_RIGHT(addr)			((long)((*(double*)addr-(long)*(double*)addr) * FLOAT_PRECISION))

#define DEF_SPY_PORT	2002

enum variable_type
{
	vt_uint32,
	vt_int32,
	vt_int64,
	vt_prop_int64,
	vt_cstr,
	vt_float
};
typedef int64_t (*property_getter)(void* obj);
void spy_start(int spy_port);
void spy_end(void);
//EXPORT_SYMBOL(spy_end);
int spy_get_port(void);
int* get_spy_port_ptr(void);

/**
 * register a variable to be read on run time.
 *
 *
 * @param[in]	name	variable's name, spy clinet use this name to read this variable
 * @param[in]	var_addr	variable's address, must be a global available memory that can
 *                 	be access at any time
 * @param[in]	var_type	variable's type, vt_uint32, vt_int32, vt_int64 or vt_float
 * @param[in]	comment	info about the variable
 */
void spy_register_variable(const char* name, const void* var_addr, enum variable_type var_type, const char* comment);

/**
 * register a property.
 *
 *
 * A property is a value accessed via a getter function. For example, *depth* is a property of queue
    to get a queue's depth, one should use code:
    int64_t depth = queue_get_depth(queue);
    to register such a property, call spy_register_property like
    int64_t s5_get_qdepth(s5session* s5s);
    spy_register_property("s5_qdepth", s5_get_qdepth, s5session, vt_prop_int64, info);
    so far, only property type int64_t is supported. i.e. prop_type must be vt_prop_int64
 *
 * @param[in]	name	variable's name, spy clinet use this name to read this variable
 * @param[in]	get_func	fun pointer of type property_getter, spy call this fun to get variable
 * @param[in]	obj_to_get	param of fun get_func
 * @param[in]	prop_type	must be vt_prop_int64
 * @param[in]	comment	info about the variable
 */
void spy_register_property(const char* name,
                           property_getter get_func,
                           void* obj_to_get,
                           enum variable_type prop_type,
                           const char* comment
                          );
/**
 * unregister a varable that registered by fun spy_register_varable or spy_register_property
 */
void spy_unregister(const char* name);
#ifdef __cplusplus
}
#endif

#endif // spy_h__
