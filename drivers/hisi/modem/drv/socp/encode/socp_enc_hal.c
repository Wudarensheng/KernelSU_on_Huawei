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
#include <securec.h>
#include "osl_sem.h"
#include "socp_enc_hal.h"

/*lint --e{124}*/

#define THIS_MODU mod_socp
extern socp_gbl_state_s g_socp_stat;

u32 g_throwout = 0;
/* for ind delay extern, now no use */
u32 g_deflate_status = 0;
u32 g_socp_debug_trace_cfg = 0;

/*
 * 函 数 名: socp_es_state_write_done
 * 功能描述: 更新缓冲区的写指针
 * 输入参数:  ring_buffer       待更新的环形buffer
 *                 size          更新的数据长度
 * 输出参数: 无
 * 返 回 值:  无
 */
void socp_es_state_write_done(u32 real_chan_id, u32 done_size)
{
    socp_ring_buff_s *ring_buffer = SOCP_ENCSRC_CHAN_BLOCK(real_chan_id);

    ring_buffer->write = (ring_buffer->write + done_size) % ring_buffer->length;

    SOCP_REG_WRITE(SOCP_REG_ENCSRC_BUFWPTR(real_chan_id), ring_buffer->write);
    return;
}

void socp_es_state_push_encsrc_bufcfg(u32 chan_id)
{
    socp_enc_src_chan_s *p_chan = SOCP_ENCSRC_CHAN(chan_id);

    SOCP_REG_SETBITS(SOCP_REG_ENCSRC_BUFCFG(chan_id), 1, 2, p_chan->chan_mode);
    SOCP_REG_SETBITS(SOCP_REG_ENCSRC_BUFCFG(chan_id), 3, 1, p_chan->trans_id_en);
    SOCP_REG_SETBITS(SOCP_REG_ENCSRC_BUFCFG(chan_id), 4, 2, p_chan->dst_chan_id);
    SOCP_REG_SETBITS(SOCP_REG_ENCSRC_BUFCFG(chan_id), 8, 2, p_chan->priority);
    SOCP_REG_SETBITS(SOCP_REG_ENCSRC_BUFCFG(chan_id), 10, 1, p_chan->bypass_en);
    SOCP_REG_SETBITS(SOCP_REG_ENCSRC_BUFCFG(chan_id), 11, 1, p_chan->data_type_en);
    SOCP_REG_SETBITS(SOCP_REG_ENCSRC_BUFCFG(chan_id), 12, 1, p_chan->ptr_img_en);
    SOCP_REG_SETBITS(SOCP_REG_ENCSRC_BUFCFG(chan_id), 16, 8, p_chan->data_type);
    SOCP_REG_SETBITS(SOCP_REG_ENCSRC_BUFCFG(chan_id), 31, 1, p_chan->debug_en);
}

s32 socp_es_op_buff_check_size(SOCP_BUFFER_RW_STRU *rw_buff, u32 write_size)
{
    if (rw_buff->u32Size + rw_buff->u32RbSize < write_size) {
        socp_error("rw_buff is not enough, wrtie Size=0x%x\n", write_size);
        return BSP_ERR_SOCP_INVALID_PARA;
    }
    return BSP_OK;
}

void socp_es_op_save_encsrc_attr(u32 chan_id, SOCP_CODER_SRC_CHAN_S *src_attr)
{
    socp_enc_src_chan_s *pEncSrcChan = SOCP_ENCSRC_CHAN(chan_id);
    pEncSrcChan->chan_mode = src_attr->eMode;
    pEncSrcChan->priority = src_attr->ePriority;
    pEncSrcChan->data_type = src_attr->eDataType;
    pEncSrcChan->data_type_en = src_attr->eDataTypeEn;
    pEncSrcChan->debug_en = src_attr->eDebugEn;
    pEncSrcChan->dst_chan_id = src_attr->u32DestChanID;
    pEncSrcChan->bypass_en = src_attr->u32BypassEn;
    pEncSrcChan->trans_id_en = src_attr->eTransIdEn;
    pEncSrcChan->ptr_img_en = src_attr->ePtrImgEn;
    pEncSrcChan->read_ptr_img_phy_addr = src_attr->eRptrImgPhyAddr;
    pEncSrcChan->read_ptr_img_vir_addr = src_attr->eRptrImgVirtAddr;
    pEncSrcChan->enc_src_buff.Start = (uintptr_t)src_attr->sCoderSetSrcBuf.pucInputStart;
    pEncSrcChan->enc_src_buff.End = (uintptr_t)src_attr->sCoderSetSrcBuf.pucInputEnd;
    pEncSrcChan->enc_src_buff.write = 0;
    pEncSrcChan->enc_src_buff.read = 0;
    pEncSrcChan->enc_src_buff.length = pEncSrcChan->enc_src_buff.End - pEncSrcChan->enc_src_buff.Start + 1;
    pEncSrcChan->enc_src_buff.idle_size = 0;

    if (SOCP_ENCSRC_CHNMODE_LIST == src_attr->eMode) {
        pEncSrcChan->rd_buff.Start = (uintptr_t)src_attr->sCoderSetSrcBuf.pucRDStart;
        pEncSrcChan->rd_buff.End = (uintptr_t)src_attr->sCoderSetSrcBuf.pucRDEnd;
        pEncSrcChan->rd_buff.write = 0;
        pEncSrcChan->rd_buff.read = 0;
        pEncSrcChan->rd_buff.length = pEncSrcChan->rd_buff.End - pEncSrcChan->rd_buff.Start + 1;
        pEncSrcChan->rd_threshold = src_attr->sCoderSetSrcBuf.u32RDThreshold;
    }
}

void socp_es_op_reset_enc_chan(u32 src_chan_id)
{
    u32 reset_flag = 0;
    u32 i = 0;
    u32 reg;

    /* 复位通道 */
    reg = src_chan_id < 0x20 ? SOCP_REG_ENCRST_L : SOCP_REG_ENCRST_H;
    SOCP_REG_SETBITS(reg, src_chan_id % 0x20, 1, 1);
    /* 等待通道自清 */
    reset_flag = SOCP_REG_GETBITS(reg, src_chan_id % 0x20, 1);
    for (i = 0; i < SOCP_RESET_TIME; i++) {
        reset_flag = SOCP_REG_GETBITS(reg, src_chan_id % 0x20, 1);
        if (reset_flag == 0 ) {
            break;
        }
    }

    if (SOCP_RESET_TIME == i) {
        socp_error("reset Chan %x failed!\n", src_chan_id);
    }
}

void socp_es_op_init_chan(void)
{
    int i;
    socp_enc_src_chan_s *p_chan = NULL;
    for (i = 0; i < SOCP_MAX_ENCSRC_CHN; i++) {
        p_chan = SOCP_ENCSRC_CHAN(i);

        p_chan->chan_id = i;
        p_chan->chan_en = SOCP_CHN_DISABLE;
        p_chan->trans_id_en = 0;
        p_chan->ptr_img_en = 0;
        p_chan->alloc_state = SOCP_CHN_UNALLOCATED;
        p_chan->last_rd_size = 0;
        p_chan->dst_chan_id = 0xff;
        p_chan->bypass_en = 0;
        p_chan->priority = SOCP_CHAN_PRIORITY_3;
        p_chan->data_type = SOCP_DATA_TYPE_BUTT;
        p_chan->data_type_en = SOCP_DATA_TYPE_EN_BUTT;
        p_chan->debug_en = SOCP_ENC_DEBUG_EN_BUTT;
        p_chan->event_cb = BSP_NULL;
        p_chan->rd_cb = BSP_NULL;
    }
}

inline void socp_es_op_get_idle_buffer(u32 real_chan_id, SOCP_BUFFER_RW_STRU *p_rw_buff)
{
    socp_ring_buff_s *ring_buffer = SOCP_ENCSRC_CHAN_BLOCK(real_chan_id);

    if (ring_buffer->write < ring_buffer->read) {
        /* 读指针大于写指针，直接计算 */
        p_rw_buff->pBuffer = (char *)(uintptr_t)(ring_buffer->Start + ring_buffer->write);
        p_rw_buff->u32Size = ring_buffer->read - ring_buffer->write - 1;
        p_rw_buff->pRbBuffer = NULL;
        p_rw_buff->u32RbSize = 0;
    } else {
        /* 写指针大于读指针，需要考虑回卷 */
        if (ring_buffer->read != 0) {
            p_rw_buff->pBuffer = (char *)(uintptr_t)(ring_buffer->Start + ring_buffer->write);
            p_rw_buff->u32Size = (u32)(ring_buffer->End - (ring_buffer->Start + ring_buffer->write) + 1);
            p_rw_buff->pRbBuffer = (char *)(uintptr_t)(ring_buffer->Start);
            p_rw_buff->u32RbSize = ring_buffer->read;
        } else {
            p_rw_buff->pBuffer = (char *)(uintptr_t)(ring_buffer->write + ring_buffer->Start);
            p_rw_buff->u32Size = (u32)(ring_buffer->End - (ring_buffer->Start + ring_buffer->write));
            p_rw_buff->pRbBuffer = NULL;
            p_rw_buff->u32RbSize = 0;
        }
    }

    return;
}

/*
 * 函 数 名: socp_get_data_buffer
 * 功能描述: 获取空闲缓冲区的数据
 * 输入参数:  ring_buffer       待查询的环形buffer
 *                 p_rw_buff         输出的环形buffer
 * 输出参数: 无
 * 返 回 值:  无
 */
void socp_es_op_rd_get_data_buffer(u32 real_chan_id, SOCP_BUFFER_RW_STRU *p_rw_buff)
{
    socp_ring_buff_s *ring_buffer = SOCP_ENCSRC_CHAN_RD(real_chan_id);

    if (ring_buffer->read <= ring_buffer->write) {
        /* 写指针大于读指针，直接计算 */
        p_rw_buff->pBuffer = (char *)(uintptr_t)(ring_buffer->Start + ring_buffer->read);
        p_rw_buff->u32Size = (u32)(ring_buffer->write - ring_buffer->read);
        p_rw_buff->pRbBuffer = NULL;
        p_rw_buff->u32RbSize = 0;
    } else {
        /* 读指针大于写指针，需要考虑回卷 */
        p_rw_buff->pBuffer = (char *)(uintptr_t)(ring_buffer->Start + ring_buffer->read);
        p_rw_buff->u32Size = (uintptr_t)(ring_buffer->End - (ring_buffer->Start + ring_buffer->read) + 1);
        p_rw_buff->pRbBuffer = (char *)(uintptr_t)(ring_buffer->Start);
        p_rw_buff->u32RbSize = ring_buffer->write;
    }

    return;
}

void socp_ed_op_set_encdst_attr(u32 chan_id)
{
    socp_encdst_chan_s *pEncDstChan = SOCP_ENCDST_CHAN(chan_id);

    SOCP_REG_WRITE(SOCP_REG_ENCDEST_BUFADDR_L(chan_id), (u32)pEncDstChan->enc_dst_buff.Start);
    SOCP_REG_WRITE(SOCP_REG_ENCDEST_BUFADDR_H(chan_id), (u32)((u64)pEncDstChan->enc_dst_buff.Start >> 32));
    SOCP_REG_WRITE(SOCP_REG_ENCDEST_BUFRPTR(chan_id), 0);
    SOCP_REG_WRITE(SOCP_REG_ENCDEST_BUFWPTR(chan_id), 0);

    SOCP_REG_WRITE(SOCP_REG_ENCDEST_BUFDEPTH(chan_id), pEncDstChan->enc_dst_buff.length);
    SOCP_REG_WRITE(SOCP_REG_ENCDEST_BUFTHRESHOLD(chan_id), (pEncDstChan->buf_thrhold));
    SOCP_REG_SETBITS(SOCP_REG_ENCDEST_BUFTHRH(chan_id), 0, 31, pEncDstChan->threshold);  // 阈值溢出
}

void socp_ed_op_save_encdst_attr(u32 chan_id, SOCP_CODER_DEST_CHAN_S *attr)
{
    socp_encdst_chan_s *pEncDstChan = SOCP_ENCDST_CHAN(chan_id);

    pEncDstChan->enc_dst_buff.Start = (uintptr_t)attr->sCoderSetDstBuf.pucOutputStart;
    pEncDstChan->enc_dst_buff.End = (uintptr_t)attr->sCoderSetDstBuf.pucOutputEnd;
    pEncDstChan->enc_dst_buff.read = 0;
    pEncDstChan->enc_dst_buff.write = 0;
    pEncDstChan->enc_dst_buff.length = pEncDstChan->enc_dst_buff.End - pEncDstChan->enc_dst_buff.Start + 1;  //lint !e834

    pEncDstChan->buf_thrhold = attr->sCoderSetDstBuf.u32Threshold;
    pEncDstChan->thrhold_high = attr->sCoderSetDstBuf.u32Threshold;
    pEncDstChan->thrhold_low = attr->sCoderSetDstBuf.u32Threshold;
    pEncDstChan->threshold = attr->u32EncDstThrh;
}

void socp_ed_op_reset_chan(u32 src_chan_id)
{
    u32 reset_flag = 0;
    u32 i = 0;
    u32 reg;
    /* 复位通道 */

    reg = src_chan_id < 0x20 ? SOCP_REG_ENCRST_L : SOCP_REG_ENCRST_H;
    src_chan_id %= 0x20;
    SOCP_REG_SETBITS(reg, src_chan_id, 1, 1);

    /* 等待通道自清 */
    reset_flag = SOCP_REG_GETBITS(reg, src_chan_id, 1);
    while (reset_flag) {
        reset_flag = SOCP_REG_GETBITS(reg, src_chan_id, 1);
        if (++i == (SOCP_RESET_TIME - 1)) {
            socp_error("reset channel 0x%x failed!\n", src_chan_id);
            break;
        }
    }
}

void socp_ed_op_init_chan(void)
{
    int i, ret;
    socp_encdst_chan_s *p_chan = NULL;
    for (i = 0; i < SOCP_MAX_ENCDST_CHN; i++) {
        p_chan = SOCP_ENCDST_CHAN(i);

        p_chan->chan_id = i;
        p_chan->threshold = 0;
        p_chan->set_state = SOCP_CHN_UNSET;
        p_chan->event_cb = BSP_NULL;
        p_chan->read_cb = BSP_NULL;
        p_chan->chan_event = (SOCP_EVENT_ENUM_UIN32)0;
        p_chan->struCompress.bcompress = SOCP_NO_COMPRESS;

        ret = memset_s(&p_chan->struCompress.ops, sizeof(p_chan->struCompress.ops), 0x0, sizeof(p_chan->struCompress.ops));
        if (ret) {
            socp_error("memset_s fail\n");
        }
    }
}

void socp_ed_op_timeout_proc(u32 chan_id, u32 timeout_mode)
{
    switch (timeout_mode) {
        case SOCP_TIMEOUT_TRF_LONG: /* 长超时 */
        {
            SOCP_REG_SETBITS(SOCP_REG_GBLRST, 4, 1, 1);
            SOCP_REG_SETBITS(SOCP_REG_ENCDEST_BUFMODE_CFG(chan_id), 3, 1, 1);
            socp_ed_op_set_timeout(SOCP_TIMEOUT_TRF_LONG, SOCP_TIMEOUT_TRF_LONG_MIN); /* 立即上报:长超时阈值=10ms */
            break;
        }

        case SOCP_TIMEOUT_TRF_SHORT: /* 短超时 */
        {
            SOCP_REG_SETBITS(SOCP_REG_GBLRST, 4, 1, 1);
            SOCP_REG_SETBITS(SOCP_REG_ENCDEST_BUFMODE_CFG(chan_id), 3, 1, 0);
            socp_ed_op_set_timeout(SOCP_TIMEOUT_TRF_SHORT, SOCP_TIMEOUT_TRF_SHORT_VAL);
            break;
        }
        case SOCP_TIMEOUT_TRF: {
            SOCP_REG_SETBITS(SOCP_REG_GBLRST, 4, 1, 0);
            break;
        }

        default:
            break;
    }
}

/*
 * 函 数 名: bsp_socp_set_timeout
 * 功能描述: 超时阈值设置函数
 * 输入参数:   time_out_en          设置对象选择及使能
 *             time_out        超时阈值
 * 输出参数:
 * 返 回 值: 设置成功与否的标识码
 */
s32 socp_ed_op_set_timeout(SOCP_TIMEOUT_EN_ENUM_UIN32 time_out_en, u32 time_out)
{
    u32 temp;

    switch (time_out_en) {
        case SOCP_TIMEOUT_BUFOVF_DISABLE: {
            SOCP_REG_SETBITS(SOCP_REG_BUFTIMEOUT, 31, 1, 0);
            break;
        }
        case SOCP_TIMEOUT_BUFOVF_ENABLE: {
            SOCP_REG_SETBITS(SOCP_REG_BUFTIMEOUT, 31, 1, 1);
            SOCP_REG_SETBITS(SOCP_REG_BUFTIMEOUT, 0, 16, time_out);
            break;
        }

        case SOCP_TIMEOUT_TRF: {
            if (SOCP_REG_GETBITS(SOCP_REG_GBLRST, 4, 1)) {
                SOCP_REG_SETBITS(SOCP_REG_GBLRST, 4, 1, 0);
            }

            SOCP_REG_WRITE(SOCP_REG_INTTIMEOUT, time_out);
            break;
        }

        /* 从v206开始增加长中断超时计数和短中断超时计数 */
        case SOCP_TIMEOUT_TRF_LONG: {
            if (0 == SOCP_REG_GETBITS(SOCP_REG_GBLRST, 4, 1)) {
                SOCP_REG_SETBITS(SOCP_REG_GBLRST, 4, 1, 1);
            }

            if (time_out > 0xffffff) {
                socp_error("timeout value(0x%x) is too large!\n", time_out);
                return BSP_ERR_SOCP_INVALID_PARA;
            }

            SOCP_REG_READ(SOCP_REG_INTTIMEOUT, temp);
            time_out = (time_out << 8) | (temp & 0xff);
            SOCP_REG_WRITE(SOCP_REG_INTTIMEOUT, time_out);

            break;
        }

        case SOCP_TIMEOUT_TRF_SHORT: {
            if (0 == SOCP_REG_GETBITS(SOCP_REG_GBLRST, 4, 1)) {
                SOCP_REG_SETBITS(SOCP_REG_GBLRST, 4, 1, 1);
            }

            if (time_out > 0xff) {
                socp_error("the value(0x%x) is too large!\n", time_out);
                return BSP_ERR_SOCP_INVALID_PARA;
            }
            SOCP_REG_READ(SOCP_REG_INTTIMEOUT, temp);
            time_out = (temp & 0xffffff00) | time_out;
            SOCP_REG_WRITE(SOCP_REG_INTTIMEOUT, time_out);

            break;
        }

        default: {
            socp_error("invalid timeout choice type(0x%x)!\n", time_out_en);
            return BSP_ERR_SOCP_SET_FAIL;
        }
    }

    return BSP_OK;
}

void socp_ed_op_pressure_test(u32 chan_id, u32 *is_handle)
{
    unsigned long lock_flag;
    u32 write;
    if (g_throwout == 0x5a5a5a5a) {
        spin_lock_irqsave(&g_socp_stat.lock, lock_flag);
        SOCP_REG_READ(SOCP_REG_ENCDEST_BUFWPTR(chan_id), write);
        SOCP_REG_WRITE(SOCP_REG_ENCDEST_BUFRPTR(chan_id), write);
        SOCP_REG_SETBITS(SOCP_REG_ENC_RAWINT0, chan_id, 1, 1);
        SOCP_REG_SETBITS(SOCP_REG_ENC_CORE0_MASK0, chan_id, 1, 0);
        /* overflow int */
        SOCP_REG_SETBITS(SOCP_REG_ENC_RAWINT2, chan_id + 16, 1, 1);
        SOCP_REG_SETBITS(SOCP_REG_ENC_CORE0_MASK2, chan_id + 16, 1, 0);
        spin_unlock_irqrestore(&g_socp_stat.lock, lock_flag);

        *is_handle = BSP_FALSE;
    }
}

inline void socp_ed_op_int_disable(u32 real_chan_id)
{
    SOCP_REG_SETBITS(SOCP_REG_ENC_RAWINT0, real_chan_id, 1, 1);
    SOCP_REG_SETBITS(SOCP_REG_ENC_CORE0_MASK0, real_chan_id, 1, 1);
    SOCP_REG_SETBITS(SOCP_REG_ENC_RAWINT2, real_chan_id, 1, 1);
    SOCP_REG_SETBITS(SOCP_REG_ENC_CORE0_MASK2, real_chan_id, 1, 1);
    SOCP_REG_SETBITS(SOCP_REG_ENC_RAWINT2, real_chan_id + 16, 1, 1);
    SOCP_REG_SETBITS(SOCP_REG_ENC_CORE0_MASK2, real_chan_id + 16, 1, 1);
}

inline void socp_ed_op_int_enable(u32 real_chan_id)
{
    SOCP_REG_SETBITS(SOCP_REG_ENC_RAWINT0, real_chan_id, 1, 1);
    SOCP_REG_SETBITS(SOCP_REG_ENC_CORE0_MASK0, real_chan_id, 1, 0);
    SOCP_REG_SETBITS(SOCP_REG_ENC_RAWINT2, real_chan_id, 1, 1);
    SOCP_REG_SETBITS(SOCP_REG_ENC_CORE0_MASK2, real_chan_id, 1, 0);
    SOCP_REG_SETBITS(SOCP_REG_ENC_RAWINT2, real_chan_id + 16, 1, 1);
    SOCP_REG_SETBITS(SOCP_REG_ENC_CORE0_MASK2, real_chan_id + 16, 1, 0);
}

