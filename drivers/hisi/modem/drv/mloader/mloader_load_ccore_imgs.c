/*
 * Copyright (C) Huawei Technologies Co., Ltd. 2012-2015. All rights reserved.
 * foss@huawei.com
 *
 * If distributed as part of the Linux kernel, the following license terms
 * apply:
 *
 * * This program is free software; you can redistribute it and/or modify
 * * it under the terms of the GNU General Public License version 2 and
 * * only version 2 as published by the Free Software Foundation.
 * *
 * * This program is distributed in the hope that it will be useful,
 * * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * * GNU General Public License for more details.
 * *
 * * You should have received a copy of the GNU General Public License
 * * along with this program; if not, write to the Free Software
 * * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Otherwise, the following license terms apply:
 *
 * * Redistribution and use in source and binary forms, with or without
 * * modification, are permitted provided that the following conditions
 * * are met:
 * * 1) Redistributions of source code must retain the above copyright
 * *    notice, this list of conditions and the following disclaimer.
 * * 2) Redistributions in binary form must reproduce the above copyright
 * *    notice, this list of conditions and the following disclaimer in the
 * *    documentation and/or other materials provided with the distribution.
 * * 3) Neither the name of Huawei nor the names of its contributors may
 * *    be used to endorse or promote products derived from this software
 * *    without specific prior written permission.
 *
 * * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */
/*
 * File Name       : mloader_load_image.c
 * Description     : load modem image(ccore image),run in ccore
 * History         :
 */
#include <product_config_drv.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <asm/dma-mapping.h>
#include <asm/cacheflush.h>
#include <bsp_shared_ddr.h>
#include <bsp_hardtimer.h>
#include <bsp_version.h>
#include <bsp_slice.h>
#include <bsp_ipc.h>
#include <bsp_ddr.h>
#include <of.h>
#include <mdrv_sysboot.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
#include <uapi/linux/sched/types.h>
#endif

#include <securec.h>
#include "mloader_comm.h"
#include "mloader_load_image.h"
#include "mloader_debug.h"

#ifdef BSP_CONFIG_PHONE_TYPE
#include <adrv.h>
#endif

#define THIS_MODU mod_mloader

u32 mloader_get_icc_cpu(u32 channel_id)
{
    u32 cpu_id = (channel_id >> 16) == ICC_CHN_MLOADER ? ICC_CPU_MODEM : ICC_CPU_NRCCPU;
    return cpu_id;
}

int mloader_icc_callback(u32 channel_id, u32 len, void *context)
{
    int ret;
    int index;
    mloader_img_icc_info_s mloader_msg;
    mloader_img_s *mloader_images = NULL;

    mloader_images = bsp_mloader_get_images_st();

    ret = memset_s(&mloader_msg, sizeof(mloader_msg), 0x0, sizeof(mloader_img_icc_info_s));
    if (ret) {
        mloader_print_err("<%s> memset_s error, ret = %d\n", __FUNCTION__, ret);
    }

    ret = bsp_icc_read(channel_id, (u8 *)&mloader_msg, sizeof(mloader_img_icc_info_s));
    if ((u32)ret != sizeof(mloader_img_icc_info_s)) {
        mloader_print_err("<%s> icc read error, ret = %d\n", __FUNCTION__, ret);
        return ret;
    }

    mloader_msg.channel_id = channel_id;
    osl_sem_down(&(mloader_images->msg_sem));
    index = (mloader_get_icc_cpu(channel_id) == ICC_CPU_MODEM) ? 0 : 1;
    mloader_images->mloader_msg[index] = mloader_msg;
    osl_sem_up(&(mloader_images->msg_sem));
    mloader_print_err("modem icc callback wakeup load task\n");

    __pm_stay_awake(&(mloader_images->wake_lock));

    osl_sem_up(&(mloader_images->task_sem));

    return 0;
}

int mloader_load_ccore_imgs(void)
{
    int ret;
    int index;
    u32 cpu_id;
    mloader_img_icc_status_s status_msg;
    mloader_img_icc_info_s mloader_msg;
    char file_name[MLOADER_FILE_NAME_LEN] = {0};
    mloader_img_s *mloader_images = NULL;
    mloader_addr_s *mloader_addr = NULL;
    struct cold_patch_info_s *mloader_cold_patch_info = NULL;
    enum modem_patch_type patch_type;
    int is_patch = 0;
    static u32 op_index = 0;

    mloader_cold_patch_info = mloader_get_cold_patch_info_st();
    mloader_addr = bsp_mloader_get_addr();
    mloader_images = bsp_mloader_get_images_st();
    mloader_update_imgs_op_start_time_debug_info(op_index);
    osl_sem_down(&(mloader_images->msg_sem));
    index = mloader_images->mloader_msg[0].op ? 0 : 1;
    mloader_msg = mloader_images->mloader_msg[index];
    mloader_update_imgs_op_core_id_debug_info(op_index, index);
    osl_sem_up(&(mloader_images->msg_sem));
    ret = memset_s(&(mloader_images->mloader_msg[index]), sizeof(mloader_images->mloader_msg[index]), 0x0,
                   sizeof(mloader_img_icc_info_s));
    if (ret) {
        mloader_print_err("<%s> memset_s error, ret = %d\n", __FUNCTION__, ret);
    }

    mloader_print_err("start loading %s\n", mloader_msg.name);

    ret = memset_s(&status_msg, sizeof(status_msg), 0x0, sizeof(mloader_img_icc_status_s));
    if (ret) {
        mloader_print_err("<%s> memset_s error, ret = %d\n", __FUNCTION__, ret);
    }

    ret = mloader_get_file_name(file_name, mloader_msg.name, 0);
    if (ret) {
        status_msg.result = ret; /* image not found */
        goto load_done;
    }
    mloader_update_imgs_op_img_idx_debug_info(op_index, mloader_msg.img_idx);
    mloader_update_imgs_op_op_debug_info(op_index, mloader_msg.op);

    mloader_print_err("load ccore imgs, op = %d\n", mloader_msg.op);
    /* solve icc cmd */
    if (mloader_msg.op == MLOADER_OP_VERIFY_IMAGE) {
        is_patch = mloader_is_patch_image(file_name, MLOADER_FILE_NAME_LEN);
        if (is_patch) {
            patch_type = mloader_patch_get_type(file_name);
            mloader_cold_patch_info->modem_patch_info[patch_type].patch_exist = 1;
            mloader_cold_patch_info->modem_patch_info[patch_type].patch_status = PUT_PATCH;
        } else {
            patch_type = MAX_PATCH;
        }
        ret = mloader_verify_modem_image((unsigned int)index);
#ifdef CONFIG_MLOADER_COLD_PATCH
        if (ret != 0) {
            mloader_record_cold_patch_splicing_ret_val(patch_type, ret);
        }
        if (mloader_msg.cmd_type == SPLICING_IMAGE) {
            mloader_update_modem_cold_patch_status(patch_type);
        }
#endif
        status_msg.result = ret == 0 ? 0 : ret;
    } else if (mloader_msg.op == MLOADER_OP_GET_PATCH_STATUS) {
        status_msg.result = mloader_cold_patch_info->modem_update_fail_count == 3 ? MLOADER_PATCH_LOAD_FAIL : MLOADER_PATCH_LOAD_SUCCESS;
    } else if (mloader_msg.op == MLOADER_OP_LOAD_FINISHED) {
        if (bsp_modem_cold_patch_is_exist()) {
            ret = bsp_nvem_cold_patch_write(mloader_cold_patch_info);
            if (ret) {
                bsp_err("update cold patch nve failed!\n");
            }
        }
        mloader_update_end_time_debug_info();
        mloader_print_err("ccore imgs load completed.\n");
        status_msg.result = ret == 0 ? 0 : ret;
        mloader_load_notifier_process(AFTER_ALL_IMAGE_LOAD);
    }

load_done:
    status_msg.op = mloader_msg.op;
    status_msg.img_idx = mloader_msg.img_idx;
    status_msg.image_addr = (u32)(mloader_addr->mloader_secboot_phy_addr[index]);
    status_msg.request_id = mloader_msg.request_id;
    status_msg.time_stamp = bsp_get_slice_value();

    cpu_id = mloader_get_icc_cpu(mloader_msg.channel_id);
    mloader_print_err("send icc to ccore, for load (%d)%d.\n", cpu_id, status_msg.img_idx);
    ret = bsp_icc_send(cpu_id, mloader_msg.channel_id, (u8 *)(&status_msg), sizeof(status_msg));
    if (sizeof(status_msg) != (u32)ret) {
        mloader_print_err("ret = 0x%x, msg_size = 0x%x\n", ret, (unsigned int)sizeof(status_msg));
    }
    mloader_print_err("load image (%d)%d complete\n", cpu_id, status_msg.img_idx);
    __pm_relax(&(mloader_images->wake_lock));
    mloader_update_imgs_op_end_time_debug_info(op_index);
    op_index = (op_index + 1)%MAX_DEBUG_SIZE;
    return ret;
}
