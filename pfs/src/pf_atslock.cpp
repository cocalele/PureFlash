
#ifndef __sun
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#endif

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>
#include <getopt.h>

#include "pf_log.h"

#ifdef WITH_PFS2

#include "scsi/sg_lib.h"
#include "scsi/sg_cmds_basic.h"
#include "scsi/sg_pt.h"
#include "scsi/sg_unaligned.h"
#include "scsi/sg_pr2serr.h"



#define DEF_BLOCK_SIZE 512
#define DEF_BLOCKS_PER_TRANSFER 8
#define DEF_TIMEOUT_SECS 60

#define COMPARE_AND_WRITE_OPCODE (0x89)
#define COMPARE_AND_WRITE_CDB_SIZE (16)

#define SENSE_BUFF_LEN 64       /* Arbitrary, could be larger */

#define ME "sg_compare_and_write: "

static const struct option long_options[] = {

        {"help", no_argument, 0, 'h'},

        {"lba", required_argument, 0, 'l'},

        {0, 0, 0, 0},
};

struct caw_flags {
    bool dpo;
    bool fua;
    bool fua_nv;
    int group;
    int wrprotect;
};

struct opts_t {

    int timeout;
    int xfer_len;

    struct caw_flags flags;
};




static void
setup_args(struct opts_t* op)
{

    /* COMPARE AND WRITE defines 2*buffers compare + write */
    op->timeout = DEF_TIMEOUT_SECS;
    //op->numblocks = 1;
    //op->xfer_len = 2 * op->numblocks * DEF_BLOCK_SIZE;
    op->xfer_len = 2 * DEF_BLOCK_SIZE;
    
}

#define FLAG_FUA        (0x8)
#define FLAG_FUA_NV     (0x2)
#define FLAG_DPO        (0x10)
#define WRPROTECT_MASK  (0x7)
#define WRPROTECT_SHIFT (5)


static int
sg_build_scsi_cdb(uint8_t* cdbp, unsigned int blocks,
    int64_t start_block, struct caw_flags flags)
{
    memset(cdbp, 0, COMPARE_AND_WRITE_CDB_SIZE);
    cdbp[0] = COMPARE_AND_WRITE_OPCODE;
    cdbp[1] = (uint8_t) (flags.wrprotect & WRPROTECT_MASK) << WRPROTECT_SHIFT;
    if (flags.dpo)
        cdbp[1] |= FLAG_DPO;
    if (flags.fua)
        cdbp[1] |= FLAG_FUA;
    if (flags.fua_nv)
        cdbp[1] |= FLAG_FUA_NV;
    sg_put_unaligned_be64((uint64_t)start_block, cdbp + 2);
    /* cdbp[10-12] are reserved */
    cdbp[13] = (uint8_t)(blocks & 0xff);
    cdbp[14] = (uint8_t)(flags.group & GRPNUM_MASK);
    return 0;
}



/* Returns 0 for success, SG_LIB_CAT_MISCOMPARE if compare fails,
 * various other SG_LIB_CAT_*, otherwise -1 . */
static int
sg_ll_compare_and_write(int sg_fd, uint8_t* buff, int blocks,
    int64_t lba, int xfer_len, struct caw_flags flags,
    bool noisy, int verbose)
{
    bool valid;
    int sense_cat, slen, res, ret;
    uint64_t ull = 0;
    struct sg_pt_base* ptvp;
    uint8_t cawCmd[COMPARE_AND_WRITE_CDB_SIZE];
    uint8_t sense_b[SENSE_BUFF_LEN] SG_C_CPP_ZERO_INIT;

    if (sg_build_scsi_cdb(cawCmd, blocks, lba, flags)) {
        S5LOG_ERROR("bad cdb build, lba=0x%" PRIx64 ", blocks=%d\n",
            lba, blocks);
        return -1;
    }
    ptvp = construct_scsi_pt_obj();
    if (NULL == ptvp) {
        S5LOG_ERROR("Could not construct scsit_pt_obj, out of memory\n");
        return -1;
    }

    set_scsi_pt_cdb(ptvp, cawCmd, COMPARE_AND_WRITE_CDB_SIZE);
    set_scsi_pt_sense(ptvp, sense_b, sizeof(sense_b));
    set_scsi_pt_data_out(ptvp, buff, xfer_len);
    if (verbose > 1) {
        char b[128];

        S5LOG_ERROR("    Compare and write cdb: %s\n",
            sg_get_command_str(cawCmd, COMPARE_AND_WRITE_CDB_SIZE, false,
                sizeof(b), b));
    }
    if ((verbose > 2) && (xfer_len > 0)) {
        S5LOG_ERROR("    Data-out buffer contents:\n");
        hex2stderr(buff, xfer_len, 1);
    }
    res = do_scsi_pt(ptvp, sg_fd, DEF_TIMEOUT_SECS, verbose);
    ret = sg_cmds_process_resp(ptvp, "COMPARE AND WRITE", res,
        noisy, verbose, &sense_cat);
    if (-1 == ret) {
        if (get_scsi_pt_transport_err(ptvp))
            ret = SG_LIB_TRANSPORT_ERROR;
        else
            ret = sg_convert_errno(get_scsi_pt_os_err(ptvp));
    }
    else if (-2 == ret) {
        switch (sense_cat) {
        case SG_LIB_CAT_RECOVERED:
        case SG_LIB_CAT_NO_SENSE:
            ret = 0;
            break;
        case SG_LIB_CAT_MEDIUM_HARD:
            slen = get_scsi_pt_sense_len(ptvp);
            valid = sg_get_sense_info_fld(sense_b, slen,
                &ull);
            if (valid)
                S5LOG_ERROR("Medium or hardware error starting "
                    "at lba=%" PRIu64 " [0x%" PRIx64
                    "]\n", ull, ull);
            else
                S5LOG_ERROR("Medium or hardware error\n");
            ret = sense_cat;
            break;
        case SG_LIB_CAT_MISCOMPARE:
            ret = sense_cat;
            if (!(noisy || verbose))
                break;
            slen = get_scsi_pt_sense_len(ptvp);
            valid = sg_get_sense_info_fld(sense_b, slen, &ull);
            if (valid)
                S5LOG_ERROR("Miscompare at byte offset: %" PRIu64
                    " [0x%" PRIx64 "]\n", ull, ull);
            else
                S5LOG_ERROR("Miscompare reported\n");
            break;
        case SG_LIB_CAT_ILLEGAL_REQ:
            if (verbose)
                sg_print_command_len(cawCmd,
                    COMPARE_AND_WRITE_CDB_SIZE);
            /* FALL THROUGH */
        default:
            ret = sense_cat;
            break;
        }
    }
    else
        ret = 0;

    destruct_scsi_pt_obj(ptvp);
    return ret;
}





/**
 * lock_location: lock location in Byte
 */
static int _ats_lock_unlock_(int devfd, int64_t lock_location, bool do_lock)
{
    int res, vb;
    vb=1;//verbose

    uint8_t* wrkBuff = NULL;
    uint8_t* free_wrkBuff = NULL;
    struct opts_t* op;
    struct opts_t opts;

    op = &opts;
    memset(op, 0, sizeof(opts));
    setup_args( op);


    wrkBuff = (uint8_t*)aligned_alloc(4096, 4096);
    if (NULL == wrkBuff) {
        S5LOG_ERROR("Not enough user memory\n");
        res = sg_convert_errno(ENOMEM);
        goto out;
    }

    if(do_lock) {
        memset(wrkBuff, 0, 512);//set first 512 to 0
        memset((char*)wrkBuff + 512, 0xff, 512);//set second 512 to 0xff
    } else {
		memset(wrkBuff, 0xff, 512);//set first 512 to 0
		memset((char*)wrkBuff + 512, 0, 512);//set second 512 to 0xff
    }
    res = sg_ll_compare_and_write(devfd, wrkBuff, 1, lock_location/512,
        op->xfer_len, op->flags, 1, vb);
    if (0 != res) {
        char b[80];

        switch (res) {
        case SG_LIB_CAT_MEDIUM_HARD:
        case SG_LIB_CAT_MISCOMPARE:
        case SG_LIB_FILE_ERROR:
            break;  /* already reported */
        default:
            sg_get_category_sense_str(res, sizeof(b), b, vb);
            pr2serr(ME "SCSI COMPARE AND WRITE: %s\n", b);
            break;
        }
    }
out:
    if (free_wrkBuff)
        free(free_wrkBuff);

    return res;
}

int pf_ats_lock(int devfd, int64_t lock_location)
{
    return _ats_lock_unlock_(devfd, lock_location, true);
}

int pf_ats_unlock(int devfd, int64_t lock_location)
{
	return _ats_lock_unlock_(devfd, lock_location, false);
}
#else //WITH_PFS2
//no pfs2 support
int pf_ats_lock(int devfd, int64_t lock_location)
{
    S5LOG_FATAL("pfs2 not enabled, please run cmake with -DWITH_PFS2=1");
	return -ENOTSUP;
}

int pf_ats_unlock(int devfd, int64_t lock_location)
{
	S5LOG_FATAL("pfs2 not enabled, please run cmake with -DWITH_PFS2=1");
	return -ENOTSUP;
}

#endif