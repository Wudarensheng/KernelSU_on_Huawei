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

#ifndef _SOCP_ENC_HAL_H
#define _SOCP_ENC_HAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* 头文件包含 */
#include "socp_balong_3.x.h"

#define SOCP_ENCSRC_CHAN_BLOCK(real_chan_id) &g_socp_stat.enc_src_chan[real_chan_id].enc_src_buff
#define SOCP_ENCSRC_CHAN_RD(real_chan_id) &g_socp_stat.enc_src_chan[real_chan_id].rd_buff
#define SOCP_ENCSRC_CHAN(real_chan_id) &g_socp_stat.enc_src_chan[real_chan_id]

#define SOCP_ENCDST_CHAN_BLOCK(real_chan_id) &g_socp_stat.enc_dst_chan[real_chan_id].enc_dst_buff
#define SOCP_ENCDST_CHAN(real_chan_id) &g_socp_stat.enc_dst_chan[real_chan_id]

/* 寄存器定义,偏移地址 */

/* 寄存器位域定义 */


/* 结构定义 */

/* 函数声明 */
void socp_es_state_write_done(u32 real_chan_id, u32 done_size);
void socp_es_state_push_encsrc_bufcfg(u32 chan_id);
s32 socp_es_op_buff_check_size(SOCP_BUFFER_RW_STRU *rw_buff, u32 write_size);
void socp_es_op_save_encsrc_attr(u32 chan_id, SOCP_CODER_SRC_CHAN_S *src_attr);
void socp_es_op_reset_enc_chan(u32 src_chan_id);
void socp_es_op_init_chan(void);
void socp_es_op_get_idle_buffer(u32 real_chan_id, SOCP_BUFFER_RW_STRU *p_rw_buff);
void socp_es_op_rd_get_data_buffer(u32 real_chan_id, SOCP_BUFFER_RW_STRU *p_rw_buff);

void socp_ed_state_write_done(u32 real_chan_id, u32 done_size);
void socp_ed_op_set_encdst_attr(u32 chan_id);
void socp_ed_op_save_encdst_attr(u32 chan_id, SOCP_CODER_DEST_CHAN_S *attr);
void socp_ed_op_reset_chan(u32 src_chan_id);
void socp_ed_op_init_chan(void);
void socp_ed_op_get_idle_buffer(u32 real_chan_id, SOCP_BUFFER_RW_STRU *p_rw_buff);
void socp_ed_op_timeout_proc(u32 chan_id, u32 timeout_mode);
s32 socp_ed_op_set_timeout(SOCP_TIMEOUT_EN_ENUM_UIN32 time_out_en, u32 time_out);
void socp_ed_op_pressure_test(u32 chan_id, u32 *is_handle);
void socp_ed_op_int_disable(u32 real_chan_id);
void socp_ed_op_int_enable(u32 real_chan_id);

#ifdef __cplusplus
}
#endif

#endif
