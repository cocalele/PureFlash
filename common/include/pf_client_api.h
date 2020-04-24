//
// Created by lele on 2020/4/11.
//

#ifndef PUREFLASH_S5_CLIENT_API_H
#define PUREFLASH_S5_CLIENT_API_H
#define S5_LIB_VER 0x00010000
struct S5ClientVolumeInfo;
typedef void(*ulp_io_handler)(int complete_status, void* cbk_arg);

struct S5ClientVolumeInfo* pf_open_volume(const char* volume_name, const char* cfg_filename, const char* snap_name,
										  int lib_ver);
void pf_close_volume(S5ClientVolumeInfo* volume);
const char* show_ver();
int pf_io_submit(struct S5ClientVolumeInfo* volume, void* buf, size_t length, off_t offset,
				 ulp_io_handler callback, void* cbk_arg, int is_write);

#define pf_io_submit_read(v, buf, len, off, cbk, arg)  pf_io_submit(v, buf, len, off, cbk, arg, 0)
#define pf_io_submit_write(v, buf, len, off, cbk, arg)  pf_io_submit(v, buf, len, off, cbk, arg, 1)

#endif //PUREFLASH_S5_CLIENT_API_H
