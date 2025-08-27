//
// Created by lele on 2020/4/11.
//
#ifndef PUREFLASH_S5_CLIENT_API_H
#define PUREFLASH_S5_CLIENT_API_H
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif
#define PF_MAX_IO_DEPTH 255 //max 255, other wise may overflow on setting RDMA responder_resources, which is uint8
#define PF_MAX_IO_SIZE (128<<10) //max IO

#define S5_LIB_VER 0x00010000
struct PfClientVolume;
typedef void(*ulp_io_handler)(void *cbk_arg, int complete_status);

struct PfClientVolumeInfo {
	char status[64];
	char volume_name[128];
	char snap_name[128];

	uint64_t volume_size;
	uint64_t volume_id;
	int shard_count;
	int rep_count;
	int meta_ver;
	int snap_seq;
};

/**
 * this is a helper function used to get volume information only.
 * no runtime context is created
 */
struct PfClientVolume* pf_half_open_volume(const char* volume_name, const char* cfg_filename, const char* snap_name,
	int lib_ver);

struct PfClientVolume *pf_open_volume(const char *volume_name, const char *cfg_filename, const char *snap_name,
                                      int lib_ver);
/**
 * Query volume static information but not open it
 * NOTE: Though this API returns the same data struct as `pf_open_volume`, but the returned volume can't be used
 * to perform read/write IO.
 * @param volume_name, volume name
 * @param cfg_filename, config file name
 * @param snap_name
 * @param lib_ver, for version compatibility check, client must use S5_LIB_VER
 * @return
 */
int pf_query_volume_info(const char *volume_name, const char *cfg_filename, const char *snap_name,
                                       int lib_ver, /*out*/ struct PfClientVolumeInfo *volume);
uint64_t pf_get_volume_size(struct PfClientVolume* vol);
void pf_close_volume(struct PfClientVolume *volume);
const char *show_ver();
int pf_io_submit(struct PfClientVolume *volume, void *buf, size_t length, off_t offset,
                 ulp_io_handler callback, void *cbk_arg, int is_write);
				 
/**
 * Submit IO vector.
 * @param volume, the volume to submit IO
 * @param iov, io vector
 * @param iov_cnt, iovec count in param iov
 * @param length, total length of iovs
 * @param offset, offset in volume, of this IO to perform in volume
 * @param callback, the call back handler function to call when IO complete_status
 * @param cbk_arg, user defined data to passed to callback when IO complete. PureFlash system will not touch this argument
 * @param is_write, indict this IO is WRITE or READ. 1 for WRITE IO; 0 for READ IO
 * @return 0 on success, negative number to indicate submit error.
 * @retval -EINVAL  argument invalidate, length too large or offset not aligned on sector(512B)
 * @retval -EAGAIN  device is temporary busy, caller can retry this IO later
 *
 */
int pf_iov_submit(struct PfClientVolume* volume, const struct iovec *iov, const unsigned int iov_cnt, size_t length, off_t offset,
                  ulp_io_handler callback, void* cbk_arg, int is_write);
#define pf_io_submit_read(v, buf, len, off, cbk, arg)  pf_io_submit(v, buf, len, off, cbk, arg, 0)
#define pf_io_submit_write(v, buf, len, off, cbk, arg)  pf_io_submit(v, buf, len, off, cbk, arg, 1)

int pf_create_tenant(const char* tenant_name, const char* cfg_filename);
int pf_rename_volume(const char* vol_name, const char* new_name, const char* cfg_filename);
int pf_delete_volume(const char* vol_name, const char* cfg_filename);


#ifdef __cplusplus
}
#endif


#endif //PUREFLASH_S5_CLIENT_API_H
