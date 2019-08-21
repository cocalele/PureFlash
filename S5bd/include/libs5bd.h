/**
 * Copyright NetBRIC(C), 2014-2015.
* @file
* libs5bd - block storage driver for high available and scalable distributed block storage system S5.
*
* libs5bd acts as basic block storage driver for high available and scalable distributed block storage system S5. It
 * supplies varies block storage apis, covering synchronous/asynchronous IO, snapshot, clone and some other common
 * utilities in block storage. For some unique features of S5, like QoS control, please refer to libs5manager.
 *
 * 
*/
#ifndef __LIBS5BD_H__
#define __LIBS5BD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "s5_meta.h"
/**
 * Use macro to parse major version number from version number.
 *
 * Version number of libs5bd is an unsigned 32-bit number, consists of major version number, minor version
 * number and extra version number. In version number, the top eight bits are reserved, and the second-highest
 * eight bits are for major version number, and third-highest 8 bits are for minor version number, and the lowest
 * bits are for extra version number. Call s5bd_version if getting version of libs5bd is needed. 
 *
 * @param[in] version		version number of libs5bd, an unsigned 32-bit number.
 * @return an unsigned 32-bit number as major version number
 */
#define LIBS5BD_MAJOR_VER(version) ((version >> 16) & 0xff)

/**
 * Use macro to parse minor version number from version number.
 *
 * Version number of libs5bd is an unsigned 32-bit number, consists of major version number, minor version
 * number and extra version number. In version number, the top eight bits are reserved, and the second-highest
 * eight bits are for major version number, and third-highest 8 bits are for minor version number, and the lowest
 * bits are for extra version number. Call s5bd_version if getting version of libs5bd is needed.
 *
 * @param[in]	version		version number of libs5bd, an unsigned 32-bit number.
 * @return		an unsigned 32-bit number as minor version number
 */
#define LIBS5BD_MINOR_VER(version) ((version >> 8) & 0xff)

/**
 * Use Macro to parse extra version number from version number.
 *
 * Version number of libs5bd is an unsigned 32-bit number, consists of major version number, minor version
 * number and extra version number. In version number, the top eight bits are reserved, and the second-highest
 * eight bits are for major version number, and third-highest 8 bits are for minor version number, and the lowest
 * bits are for extra version number. Call s5bd_version if getting version of libs5bd is needed.
 *
 * @param[in]	version		version number of libs5bd, an unsigned 32-bit number.
 * @return		an unsigned 32-bit number as extra version number
 */
#define LIBS5BD_EXTRA_VER(version) (version & 0xff)

#define MAX_NAME_LEN 96	///< max length of name used in s5 modules.

typedef void* s5_ioctx_t;

/** Callback function type for asynchronous io. */
typedef void (*s5bd_callback_t)(void *arg, uint64_t res_len);

typedef void* s5_volume_t;

/**
 * Create io-context of S5
 *
 * Use this function to initialize entire S5 context. Meanwhile, user authority will be checked in it. For other functions,
 * if they are called as input arguments but not initialized by io-context, an undefined result will occur. If return value 
 * is not 0(failed), user can call "get_last_error_str" for detailed error information, furthermore refer to log of s5bd 
 * when it's needed. 
 * When this function successfully returns, s5ioctx has been initialized and can be retained for long term. If user
 * does not use it any more, user needs to call 's5_release_ioctx' to release it in case memory leak happens when use the function again.
 *
 * @param[in]		tenant_name		const string, which specify tenant name.
 * @param[in]		pswd			const string, password of tenant.
 * @param[in]		s5config		base path(relative or absolute) of S5 configuration file. If a relative path is entered,
 *									it will be relative to the location where executable file starts.
 * @param[in,out]	s5ioctx			object of s5_ioctx_t type, if function successfully returns, S5 configured information and tenant
 *									authority information will be recorded in it.
 *
 * @return	0 on success, negative error code on failure
 * @retval	0				success
 * @retval	-EINVAL			"tenant_name" is an empty string, NULL pointer, or length of it exceeds 96; "tenant_name" is not
 *							composed of letters or numbers or underlined spaces; "pswd" is an empty string, NULL pointer, or
 *							length of it exceeds 512; "s5config" is an empty string, NULL pointer, or length of it exceeds 
 *							512; errors occur when parse configuration file of S5; mode of S5 configuration file is invalid; 
 *							size of configuration exceeds 0x40000000 bytes.
 * @retval	-ENOMEM			run out of memory, this maybe occur in client-side or back-end server.
 * @retval	        -EACCES			Can't open S5 configuration file for permission reason or file not exists.
 * @retval	        -EOVERFLOW		S5 configuration file path exceeds the system limitation.
 * @retval	        -EIO			unexpected EOF while reading configuration file, concurrent modification may cause it.
 * @retval	        -S5_CONF_ERR	configuration file does not conform to configuration rules, and user needs check log for detailed info.
 */
int s5_create_ioctx(const char* tenant_name, const char* pswd, const char* s5config, s5_ioctx_t* s5ioctx);

/**
 * Release io-context of S5
 *
 * Use this function to release entire S5 context. After call this function, memory space of s5ioctx will be
 * free and set as NULL.
 *
 * @param[in,out]	s5ioctx			object of s5_ioctx_t type, after call this function it will be NULL.
 *
 * @return		0 on success
 */
int s5_release_ioctx(s5_ioctx_t* s5ioctx);

/**
 * Stat version information of libs5bd
 *
 * @return		an unsigned 32-bit number as version of libs5bd
 */
uint32_t s5bd_version(void);

/**
 * Create volume of S5.
 *
 * To create volume in S5, creating only one volume under one tenant is suggested.
 *
 * @param[in]	s5ioctx			io-context of S5, of 's5_ioctx_t' type.
 * @param[in]	   volume_name	   name of the volume to create, of const char pointer type, cannot be NULL, or too long.
 * @param[in]      tenant_name     tenant of the volume to create, of const char pointer type, cannot be NULL, or too long.
 * @param[in]	   size			   capacity of the volume to create, with unit of byte, and value must be an integral multiple of
 *								   4M, also must be larger than 0.
 * @param[in]	   iops			   iops of the volume to create, and basic io size is 4K bytes. Also value must be an integral
 *								multiple of 1024, larger than 0, and no more than 1M(1024x1024) for now.
 * @param[in]	bw				   access bandwidth of the volume to create, and must be larger than 0.
 * @param[in]	flag			   additional features of the volume to create, e.g. encryption scheme, compress mode and
 *								something alike.
 * @param[in]   replica_num        volume replicas count.
 * @param[in]   tray_id            the tray id array 
 * @param[in]   s5store_name    the s5store array where the volume should be created.
 * @return		0 for success, and negative error code for errors.
 * @retval		0				success
 *				-EINVAL			   invalid s5ioctx, e.g. owner(tenant) of ioctx does not exist, or s5ioctx has no conductor information; 
 *								   volume size is not the integral multiple of 4M bytes or is 0; volume name is NULL or empty or exceeds 96 bytes; 
 *								   quotaset name is empty or exceeds 96 bytes; volume name is not composed of letters, numbers and underlines; 
 *								   quotaset name is not composed of letters, numbers and underlines; iops or bandwidth value is 0; 
 */
int s5_create_volume(const s5_ioctx_t s5ioctx, const char* tenant_name, const char* volume_name, uint64_t size, uint64_t iops, uint64_t bw,
                     uint64_t flag, uint32_t replica_num, int32_t tray_id[MAX_REPLICA_NUM], const char* s5store_name[MAX_REPLICA_NUM]);

/**
 * Open volume of S5.
 *
 * To open a volume, user needs to specify snap information, here snap name is required. If user wants to open volume in
 * head version, just set snap name to NULL. After open a volume, libs5bd has allocated memory space for 'volume_ctx' to 
 * store volume context info. The following ops on volume all need volume context, e.g. io, create snapshot, etc. When
 * user does not access to volume any more, user needs to release volume context with "s5_close_volume". Otherwise, memory
 * leak can be expected, and also other following ops on that volume will be influenced. For example, deleting that volume 
 * will be denied in this situation.
 *
 * @param[in]		s5ioctx			io-context of S5, of 's5_ioctx_t' type.
 * @param[in]   	tenant_name     tenant of the volume to open, of const char pointer type, cannot be NULL, or too long.
 * @param[in]		volume_name		name of the volume to create, of const char pointer type, cannot be NULL, or too long.
 * @param[in]		snap_name		snapshot name, if not null, volume of specified snapshot will be opened. Or else,
 *									volume specified will be opened in head version.
 * @param[in,out]	volume_ctx		in-out parameter, volume context, if function returns successfully, context information
 *									will be stored in it, otherwise, it will be NULL.
 *
 * @return			0 for success, and negative error code for errors.
 * @retval			0				success
 *					-EINVAL			invalid s5ioctx, e.g. owner(tenant) of ioctx no longer exists,
 *									volume name is NULL, tenant name is NULL or snap name is NULL.
 */
int s5_open_volume(const s5_ioctx_t s5ioctx, const char* tenant_name, const char *volume_name, const char *snap_name, s5_volume_t *volume_ctx);

/**
 * Close volume of S5.
 *
 * To close volume in S5, s5ioctx must be initialized validly first with api 's5_create_ioctx', In case an unexpected result occurs when use ioctx again.
 *
 * @param[in,out]	volume_ctx		in-out parameter, volume context, if function returns successfully, it will be NULL.
 *
 * @return			0 for success, and negative error code for errors.
 * @retval			0				success
 *					-EINVAL         parameter invalid
 */
int s5_close_volume(s5_volume_t* volume_ctx);

/**
 * Update a volume information.
 *
 * If volume is opened, to update volume size will be denied. Updating QoS settings will go into effect immediately.
 *
 * @param[in]	s5ioctx				s5 io context, with executor information in it
 * @param[in]   tenant_name         tenant of volume to update, of const char pointer type, cannot be NULL, or too long
 * @param[in]	volume_name			name of volume to update, not NULL
 * @param[in]	new_name			if not NULL, it is the new name for volume to update; otherwise, volume name will
 *									keep unchanged
 * @param[in]	size				if not negative integer, it is the new size of volume; otherwise, volume size will
 *									not be updated
 * @param[in]	iops				if not negative integer, it is the new iops of volume; otherwise, volume iops will
 *									not be updated
 * @param[in]	bw					if not negative integer, it is the new bw of volume; otherwise, volume bw will not be
 *									updated
 * @param[in]	flag				reserved, pass 0 is required
 *
 * @return 0 for success and negative error code for errors.
 * @retval	0		success
 */
int s5_update_volume(const s5_ioctx_t s5ioctx, const char* tenant_name, const char* volume_name, const char* new_name, int64_t size,
                int64_t iops, int64_t bw, int64_t flag);

/**
 * Delete a volume in S5.
 *
 * If volume is opened, deleting volume size will be denied. To update volume in S5, s5ioctx must be initialized validly
 * first with api 's5_create_ioctx'. And if an uninitialized ioctx is used, an unexpected result will occur.
 *
 * @param[in]	s5ioctx				s5 io context, with executor information in it
 * @param[in]   tenant_name         tenant of the volume to delete, of const char pointer type, cannot be NULL, or too long
 * @param[in]	volume_name			name of the volume to delete, const char pointer type
 *
 * @return 0 for success and negative error code for errors.
 * @retval	0		success
 */
int s5_delete_volume(const s5_ioctx_t s5ioctx, const char* tenant_name, const char* volume_name);

/**
 * Rename a volume in S5.
 *
 * To rename volume in S5, s5ioctx must be initialized validly first with api 's5_create_ioctx'. And if an uninitialized
 * ioctx is used, an unexpected result will occur.
 *
 *
 * @param[in]	s5ioctx				s5 io context, with executor information in it
 * @param[in]   tenant_name         tenant of the volume to rename, of const char pointer type, cannot be NULL, or too long
 * @param[in]	old_name			original name of the volume to rename, not NULL
 * @param[in]	new_name			new name of the volume to rename, not NULL
 *
 * @return 0 for success and negative error code for errors.
 * @retval	0		success
 */
int s5_rename_volume(const s5_ioctx_t s5ioctx, const char* tenant_name, const char *old_name, const char *new_name);

/**
 * State volume information in S5.
 *
 * Use this function to state detailed info of volume in S5. To state volume information with this function, volume
 * context is required. So user needs to open volume first. For buffer for volume information (parameter 'volume_info'),
 * user is responsible for its allocation and release. If volume context(parameter 'volume_ctx') is not valid
 * result of 's5_open_volume', unexpected error will occur.
 *
 * @param[in]		volume_ctx			volume context, with volume detailed information in it
 * @param[in,out]	volume_info			volume info buffer, if success, detailed information of target volume will
 *										be stored in it
 *
 * @return 0 for success and negative error code for errors.
 * @retval	0		success
 */
int s5_stat_opened_volume(s5_volume_t volume_ctx, s5_volume_info_t* volume_info);

/**
 * Get volume size in S5.
 *
 * Use this function to get volume quota. To get volume quota with this function, volume context
 * is required. So user needs to open volume first. If volume context(parameter 'volume_ctx') is not valid
 * result of 's5_open_volume', unexpected error will occur. Volume size will be returned as unsigned 64-bit
 * number on success.
 *
 * @param[in]		volume_ctx			volume context, with volume detailed information in it
 *
 * @return volume size on success.
 */
uint64_t s5_get_opened_volume_size(s5_volume_t volume_ctx);

/**
 * Resize a volume in S5.
 *
 * If the volume to resize stays in open status, operation will be denied and -EBUSY is returned. 
 * The new size to update must be an integral multiple of 4M bytes and larger than 0.
 *
 * @param[in]	s5ioctx				s5 io context, with executor information in it
 * @param[in]	tenant_name			tenant of volume, of const char pointer type, cannot be NULL, or too long
 * @param[in]	volume_name			name of volume to resize, not null
 * @param[in]	size				new size of volume to update, of unsigned 64-bit number type
 *
 * @return	0 for success and negative error code for errors.
 *
 * @retval	0		success
 */
int s5_resize_volume(const s5_ioctx_t s5ioctx, const char* tenant_name, const char* volume_name, uint64_t size);

/**
 * Read a volume with asynchronous operation.
 *
 * To read a volume with asynchronous operation, volume context is required. So user needs to open volume first. 
 * If volume context(parameter 'volume_ctx') is not valid result of 's5_open_volume', unexpected error will occur. 
 *
 * @param[in]		volume		volume handler 
 * @param[in]		off		the starting position of the read operation
 * @param[in]		len		read the data length
 * @param[in]		buf		read the data 
 * @param[in]		cb_func		asynchronous callback function
 * @param[in]		cb_arg		asynchronous callback function parameters
 *
 * @return	0 for success and negative error code for errors.
 *
 * @retval	0		success
 *     		        -EINVAL     parameter invalid
 */
int s5_aio_read_volume(s5_volume_t volume, uint64_t off, size_t len, char *buf, s5bd_callback_t cb_func, void* cb_arg);

/**
 * Write a volume with asynchronous operation.
 *
 * To write a volume with asynchronous operation, volume context is required. So user needs to open volume first. 
 * If volume context(parameter 'volume_ctx') is not valid result of 's5_open_volume', unexpected error will occur. 
 *
 * @param[in]		volume		volume handler
 * @param[in]		off		the starting position of the read operation
 * @param[in]		len		write the data length
 * @param[in]		buf		write the data 
 * @param[in]		cb_func		asynchronous callback function
 * @param[in]		cb_arg		asynchronous callback function parameters
 *
 * @return	0 for success and negative error code for errors.
 *
 * @retval	0		success
 *		           -EINVAL      parameter invalid
 */
int s5_aio_write_volume(s5_volume_t volume, uint64_t off, size_t len, const char *buf, s5bd_callback_t cb_func, void* cb_arg);
/**
 * Write a volume with synchronous operation.
 * 
 * To read a volume with synchronous operation, volume context is required. So user needs to open volume first. 
 * If volume context(parameter 'volume_ctx') is not valid result of 's5_open_volume', unexpected error will occur. 
 *
 * @param[in]		volume		volume handler
 * @param[in]		ofs		the starting position of the read operation
 * @param[in]		len		write the data length
 * @param[in]		buf		write the data 
 *
 * @return	0 for success and negative error code for errors.
 *
 * @retval	0		success
 *		            -EINVAL     parameter invalid
 */
ssize_t s5_read_volume(s5_volume_t volume, uint64_t ofs, size_t len, char *buf);
/**
 * Read a volume with synchronous operation.
 *
 * To write a volume with synchronous operation, volume context is required. So user needs to open volume first. 
 * If volume context(parameter 'volume_ctx') is not valid result of 's5_open_volume', unexpected error will occur. 
 *
 * @param[in]		volume		volume handler
 * @param[in]		ofs		the starting position of the read operation
 * @param[in]		len		write the data length
 * @param[in]		buf		write the data 
 *
 * @return	0 for success and negative error code for errors.
 *
 * @retval	0		success
 *		            -EINVAL     parameter invalid
 */
ssize_t s5_write_volume(s5_volume_t volume, uint64_t ofs, size_t len, const char *buf);

/**
 * Open an volume in read-only mode.
 *
 * Attempting to write to a read-only volume will return -EROFS.
 *
 * @param[in]		s5ioctx			S5 io context
 * @param[in]		volume name		volume name
 * @param[in]		snap_name		name of snapshot to open, or NULL for no snapshot
 * @param[in,out]	volume_ctx		the location to store newly opened volume context
 * @returns 0 on success, negative error code on failure
 * @retval	0			success
 * @retval	        -EINVAL         parameter invalid
 */
int s5_open_volume_read_only(const s5_ioctx_t s5ioctx, const char* tenant_name, const char* volume_name, const char *snap_name, s5_volume_t *volume_ctx);


/**
 * List all volumes in S5.
 *
 *
 * @param[in]		s5ioctx				s5 io context, created with function 's5_create_ioctx'.
 * @param[in,out]	volume_list			volume list buffer. User is responsible for its allocation and release. Besides,
 *										before release this buffer, user should call 's5_release_volumelist' first. Or else,
 *										memory leak can be expected.
 *
 * @return 0 for success and negative error code for errors.
 * @retval	0		success
 */
int s5_list_volume(const s5_ioctx_t s5ioctx, s5_volume_list_t* volume_list);

/**
 * Release a volume list.
 *
 * The volume list to release must be result of a correct run of some list volumes operations. Otherwise,
 * unexpected errors will occur. Also, after call this function, if 'volume_list' is dynamically
 * allocated by user, user still needs to free it.
 *
 * @param[in,out]		volume_list		volume list buffer, if function successfully returned, volumes
 *										info will be stored in it.
 * @return	0 for success, and negative error code for errors.
 * @retval	0		success
 */
int s5_release_volumelist(s5_volume_list_t* volume_list);


/**
 * Import an image file outside into S5 to create a volume.
 *
 * This function will first create a new volume, and then read data from the image file outside into the volume created.
 * For the new created volume, all parameters need to be specified by user, except for volume size which is equal to
 * actual size of image file imported. All these parameters, including volume size, have the same constraints as
 * parameters of 's5_create_volume'. To perform this operation successfully, user needs to make sure that the resource
 * like volume capacity and QoS of tenant remained is enough for the newly created volume. The newly created
 * volume and source volume belong to one tenant. To perform this operation, also requires that source volume
 * is in open status, and has the volume context as an input parameter. And if volume context(parameter
 * 'volume_ctx') is not valid result of 's5_open_volume', unexpected error will occur.
 *
 * @param[in]		s5ioctx				S5 io context
 * @param[in]       tenant_name         tenant of volume to import image, of const char pointer type, cannot be NULL, or too long. 
 * @param[in]		volume_name			target volume name
 * @param[in]		image file			image file path
 * @param[in]		iops				iops of volume to create, count on 4k block size. Also it must be an integral
 *										multiple of 1024, larger than 0, and no more than 1M(1024x1024) for now
 * @param[in]		bw					access bandwidth of the volume to create, and must be larger than 0
 * @param[in]		flag				additional features of the volume to create, like encryption scheme, compress mode and
 *										something alike
 * @param[in]       replica_num         volume replicas count
 * @param[in]       tray_id             the tray id array where the volume should be created
 * @param[in]       s5store_name        the s5store array where the volume should be created
 
 * @returns 0 on success, negative error code on failure
 * @retval	0			success
 * @retval	        -EINVAL             parameter invalid
 */
int s5_import_image(const s5_ioctx_t s5ioctx, const char* tenant_name, const char* volume_name, const char* image_file, uint64_t iops, uint64_t bw,
              uint64_t flag, uint32_t replica_num,  int32_t tray_id[MAX_REPLICA_NUM], const char* s5store_name[MAX_REPLICA_NUM]);

/**
 * Export volume in S5 to image file outside.
 *
 * This function will first create target if it doesn't exist in specified path. And then read data from
 * volume in S5 and write to target image file.
 *
 * @param[in]		s5ioctx				S5 io context
 * @param[in]       tenant_name         tenant of volume to export image to image file, of const char pointer type, cannot be NULL, or too long. 
 * @param[in]		volume_name			source volume name
 * @param[in]		image file			target image file path
 *
 * @returns 0 on success, negative error code on failure
 * @retval	0			success
 * @retval	        -EINVAL             parameter invalid
 */
int s5_export_image(const s5_ioctx_t s5ioctx, const char* tenant_name, const char* image_file, const char* volume_name);

/**
 * Get the last error in string.
 *
 * This is the last error that shows to user. String returned from this function is an internal static buffer,
 * caller should not retain this for long time usage. since this is a static buffer, caller needn't to free it.
 *
 * @return const string of last error info.
 */
const char* get_last_error_str(void);

#ifdef __cplusplus
}
#endif

#endif
