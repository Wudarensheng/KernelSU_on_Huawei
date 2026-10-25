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

#include <linux/if_vlan.h>
#include <securec.h>

#include <mdrv_wan.h>
#include <bsp_maa.h>
#include <bsp_espe.h>
#include <bsp_trans_report.h>
#include "espe_desc.h"
#include "espe_port.h"
#include "espe_dbg.h"
#include "func/espe_direct_fw.h"



#define ESPE_RD_FLAG_MAGIC 0x53504500
#define ESPE_RD_MAX_GET_TIMES 1000
#define ESPE_RD_AVG_GET_TIMES 2
#define ESPE_RD_FLAG_SPORT_MASK 0xf0
#define ESPE_RD_FLAG_MASK 0xffffff00

//extern struct espe_cd_desc *g_cd_desc;
//extern void wan_spe_td_cd_pkt_proc_test (struct sk_buff *skb, unsigned int cd_num, struct spe_port_ctrl *port_ctrl);

/* desc number occupied by hardware */
static inline int spe_busy_td_num(struct spe *spe, int portno)
{
    struct spe_port_ctrl *ctrl = &spe->ports[portno].ctrl;
    int ret;

    ret = (ctrl->td_free >= ctrl->td_busy) ? (ctrl->td_free - ctrl->td_busy)
                                            : (ctrl->td_depth - ctrl->td_busy + ctrl->td_free);
    return ret;
}

/* desc number can be used by software */
static inline int spe_free_td_num(struct spe *spe, int portno)
{
    struct spe_port_ctrl *ctrl = &spe->ports[portno].ctrl;
    int ret;

    /* 1 for distinguish full and empty */
    ret = ctrl->td_depth - 1 - spe_busy_td_num(spe, portno);
    return ret;
}

static void espe_td_result_record(struct espe_td_result_s *td_result, struct spe_port_stat *stat)
{
    struct desc_result_s *result = &stat->result;

    result->td_result[td_result->td_trans_result]++;    // td result bit 0-1
    result->td_ptk_drop_rsn[td_result->td_drop_rsn]++;  // td result bit 2-5
    result->td_pkt_fw_path[__fls(td_result->td_trans_path)]++;  // td result bit 6-16
    result->td_pkt_type[td_result->td_pkt_type]++;      // td result bit 17-19
    result->td_warp[td_result->td_warp]++;              // td result bit 20-21
    result->td_unwrap[td_result->td_unwrap]++;          // td result bit 22-25

    return;
}

void espe_rd_rewind(unsigned int portno)
{
    struct spe *spe = &g_espe;

    struct spe_port_ctrl *ctrl = &spe->ports[portno].ctrl;
    unsigned long flags;
    unsigned int rd_wptr;
    unsigned int rd_rptr;

    spin_lock_irqsave(&ctrl->rd_lock, flags);

    /* get rdq ptr, include rd and wr ptr */
    rd_wptr = spe_readl_relaxed(spe->regs, SPE_RDQX_WPTR(portno));
    rd_rptr = spe_readl(spe->regs, SPE_RDQX_RPTR(portno));

    if (rd_wptr == rd_rptr) { /* no more rd, don't need rewind */
        spin_unlock_irqrestore(&ctrl->rd_lock, flags);
        return;
    }

    rd_wptr = rd_rptr;
    spe_writel(spe->regs, SPE_RDQX_WPTR(portno), rd_wptr);

    spin_unlock_irqrestore(&ctrl->rd_lock, flags);
    return;
}

static inline void espe_free_maa_mem(unsigned long long maa_addr)
{
    struct sk_buff *skb = NULL;
    unsigned long long maa_org_addr = 0;
    skb = bsp_maa_to_skb(maa_addr, &maa_org_addr);
    if (unlikely((NULL == skb) || (maa_org_addr == 0))) {
        struct spe *spe = &g_espe;
        SPE_ERR_ONCE("get td free maa 0x%llx fail \n", maa_addr);
        espe_bug(spe);
        return;
    }
    bsp_maa_free(maa_org_addr);

    return;
}

void espe_td_complete_ipf_intr(void)
{
    struct spe *spe = &g_espe;

    struct spe_port_ctrl *ctrl = &spe->ports[spe->ipfport.portno].ctrl;
    struct spe_port_stat *stat = &spe->ports[spe->ipfport.portno].stat;
    struct espe_td_desc *td_base = (struct espe_td_desc *)ctrl->td_addr;
    struct espe_td_desc *td_cur = NULL;
    struct espe_td_result_s *td_result = NULL;

    unsigned long long maa_addr;
    unsigned long flag;
    unsigned int td_rptr;
    unsigned int idx;
    unsigned int td_result_u32;
    struct sk_buff *skb = NULL;
    unsigned long long maa_org_addr;

    spin_lock_irqsave(&ctrl->td_lock, flag);

    /* get tdq rptr, */
    td_rptr = spe_readl(spe->regs, SPE_TDQX_RPTR(spe->ipfport.portno));

     /* recycle td from td_busy to tdq rptr */
    for (idx = ctrl->td_busy; idx != td_rptr;) {
        if (unlikely(spe->bugon_flag)) {
            goto spe_bugon;
        }

        td_cur = &td_base[idx];
        td_result_u32 = td_cur->td_result;
        td_result = (struct espe_td_result_s *)&td_result_u32;

        maa_addr = (unsigned long long)td_cur->td_inaddr_lower +
                       (((unsigned long long)td_cur->td_inaddr_upper) << 32);

        if (spe->dbg_level & SPE_DBG_TD_RESULT) {
            espe_td_result_record(td_result, stat);
        }

        if (unlikely(td_result->td_trans_result == TD_RESULT_DISCARD ||
                     td_result->td_trans_result == TD_RESULT_WRAP_OR_LENTH_WRONG)) {
            stat->td_desc_fail_drop++;
            espe_free_maa_mem(maa_addr);
        } else {
            stat->td_desc_complete++;
            stat->td_pkt_complete += td_result->td_pkt_ext_cnt;
            if (unlikely(spe->wakeup_flag == TRUE)) {
                SPE_ERR("first dl pkt after wakeup ! \n");
                skb = bsp_maa_to_skb(maa_addr, &maa_org_addr);
                if (unlikely(skb == NULL)) {
                    SPE_ERR("first dl pkt skb is null ! \n");
                } else {
                    espe_print_pkt_info(skb->data);
                }
                spe->wakeup_flag = FALSE;
            }
        }

        idx = ((++idx) < ctrl->td_depth) ? idx : 0;
        if (unlikely(ctrl->port_flags.hids_upload)) {
            bsp_trans_report_mark_pkt_info(maa_addr, trans_type_ipf_dl, false, true);
        }
    }
    ctrl->td_busy = idx;
    spe_writel(spe->ipfport.ipf_rd_rptr_addr, 0, td_rptr);
spe_bugon:
    spin_unlock_irqrestore(&ctrl->td_lock, flag);
    stat->td_finsh_intr_complete++;
    return;
}

void espe_td_complete_intr(unsigned int portno)
{
    struct spe *spe = &g_espe;

    struct spe_port_ctrl *ctrl = &spe->ports[portno].ctrl;
    struct spe_port_stat *stat = &spe->ports[portno].stat;
    struct espe_td_desc *td_base = (struct espe_td_desc *)ctrl->td_addr;
    struct espe_td_desc *td_cur = NULL;
    struct espe_td_result_s *td_result = NULL;

    unsigned int td_rptr;
    unsigned int idx;
    unsigned long flag;
    unsigned long long maa_addr;
    unsigned int td_result_u32;

    if (portno == spe->ipfport.portno) {
        return;
    }

    spin_lock_irqsave(&ctrl->td_lock, flag);

    do {
        /* get tdq rptr, */
        td_rptr = spe_readl(spe->regs, SPE_TDQX_RPTR(portno));

        if (ctrl->td_busy == td_rptr) {
            break;
        }

        /* recycle td from td_busy to tdq rptr */
        for (idx = ctrl->td_busy; idx != td_rptr;) {
            if (spe->bugon_flag) {
                goto spe_bugon;
            }

            td_cur = &td_base[idx];
            td_result_u32 = td_cur->td_result;
            td_result = (struct espe_td_result_s *)&td_result_u32;

            if (spe->dbg_level & SPE_DBG_TD_RESULT) {
                espe_td_result_record(td_result, stat);
            }
            if (unlikely(td_result->td_trans_result == TD_RESULT_DISCARD ||
                         td_result->td_trans_result == TD_RESULT_WRAP_OR_LENTH_WRONG)) {
                if (portno != spe->usbport.portno) {
                    maa_addr = (unsigned long long)td_cur->td_inaddr_lower +
                               (((unsigned long long)td_cur->td_inaddr_upper) << 32);

                    espe_free_maa_mem(maa_addr);
                }
                stat->td_desc_fail_drop++;
            } else {
                stat->td_pkt_complete += td_result->td_pkt_ext_cnt;
                stat->td_desc_complete++;
            }

            if (unlikely(ctrl->ops.espe_finish_td)) {
                ctrl->ops.espe_finish_td(ctrl->td_param[idx], ctrl->port_priv);
            }

            idx = ((++idx) < ctrl->td_depth) ? idx : 0;
        }
        ctrl->td_busy = idx;

    } while (1);
spe_bugon:
    spin_unlock_irqrestore(&ctrl->td_lock, flag);
    stat->td_finsh_intr_complete++;
    return;
}

void espe_td_config_one(struct spe_port_ctrl *port_ctrl, unsigned long long maa_l2_addr, unsigned int len, void *param,
                        unsigned int flags)
{
    struct spe *spe = &g_espe;
    struct espe_td_desc *cur_desc = (struct espe_td_desc *)port_ctrl->td_addr;
    struct espe_td_desc *desc = port_ctrl->axi_td;
    struct wan_info_s *wan_info = NULL;
    int ret;

    cur_desc = cur_desc + port_ctrl->td_free;

    // word 0
    desc->td_irq_en = 1;
#ifdef IPF_UL_DMA
    desc->td_mode = 0;
#else
    desc->td_mode = 1;
#endif
    desc->td_pkt_len = len;
    desc->td_int_en = 1;
    desc->td_push_en = flags & ESPE_TD_KICK_PKT;

/* add by B5010 */
    /* word1-2 packet input header, suppose to point at ip header */
    desc->td_inaddr_lower = lower_32_bits(maa_l2_addr);
    desc->td_inaddr_upper = upper_32_bits(maa_l2_addr);

    /* word4 */
    desc->td_drop_ack_ind = 0;
    desc->td_usb_net_id = 0;

    if (spe->cpuport.portno != port_ctrl->portno) {
        /* word 0 */
        desc->td_fc_head = 0;
        desc->td_push_en = !!(flags & ESPE_TD_PUSH_USB);
        desc->td_high_pri_flag = 1;  // for ipf; high priority packet flag

        /* word3 */
        desc->td_pdu_ssid = 0;
        desc->td_modem_id = 0;
        desc->td_iptype = 0;
        /* word4 */
        desc->td_bypass_en = !!(port_ctrl->bypassport_en);
        desc->td_bypass_addr = port_ctrl->bypassport;

    } else {
        wan_info = (struct wan_info_s *)(((struct sk_buff *)param)->cb);

        /* word0 */
        desc->td_fc_head = wan_info->info.fc_head;
        desc->td_high_pri_flag = wan_info->info.higi_pri_flag;

        /* word3 */
        desc->td_pdu_ssid = wan_info->info.pdu_session_id;
        desc->td_modem_id = wan_info->info.modem_id;
        desc->td_iptype = 0;

        /* word4 */
        desc->td_bypass_en = 1;
        desc->td_bypass_addr = spe->ipfport.portno;
        desc->td_host_ana = wan_info->info.parse_en;

        /* word7-9 */
        desc->td_user_field0 = wan_info->userfield0;
        desc->td_user_field1 = wan_info->userfield1;
        desc->td_user_field2 = wan_info->userfield2;
    }

    ret = memcpy_s(cur_desc, sizeof(*cur_desc), desc, sizeof(struct espe_td_desc));
    if (unlikely(ret)) {
        WARN_ON_ONCE(1);
    }
}

// last pkt must push! add check;
int bsp_espe_config_td(int portno, unsigned long long maa_l2_addr, unsigned int len, void *param,
                       unsigned int td_flags)  // fixme:add maa addr check
{
    struct spe *spe = &g_espe;
    struct spe_port_ctrl *ctrl = NULL;
    struct spe_port_stat *stat = NULL;
    unsigned long flags;

    if (unlikely(portno >= SPE_PORT_NUM || !test_bit(portno, &spe->portmap[SPE_PORTS_NOMARL]))) {
        SPE_ERR_ONCE("port %d not exist!\n", portno);
        espe_bug(spe);
        return -EFAULT;
    }

    ctrl = &spe->ports[portno].ctrl;
    stat = &spe->ports[portno].stat;

    if (unlikely(!maa_l2_addr)) {
        stat->td_dma_null++;
        SPE_ERR_ONCE("port %d maa_l2_addr zero null!\n", portno);
        espe_bug(spe);
        return -EFAULT;
    }

    if (unlikely(!ctrl->port_flags.enable)) {
        stat->td_port_disabled++;
        SPE_ERR_ONCE("td config fail port %d still disabled! \n", portno);
        return -EIO;
    }

    if (unlikely(spe->flags & SPE_FLAG_SUSPEND)) {
        SPE_ERR_ONCE("td config port %d when spe suspend! \n", portno);
        return -EIO;
    }

    spin_lock_irqsave(&ctrl->td_lock, flags);

    if (unlikely(spe_free_td_num(spe, portno) == 0)) {
        spin_unlock_irqrestore(&ctrl->td_lock, flags);
        stat->td_full++;
        return -EBUSY;
    }
#ifndef CONFIG_ESPE_PHONE_SOC
    if (spe->spe_version == ESPE_VER_5000) {
        espe_ad_updata_wptr(spe);
    }
#endif
    ctrl->td_param[ctrl->td_free] = param;
    espe_td_config_one(ctrl, maa_l2_addr, len, param, td_flags);


    ctrl->td_free = (++ctrl->td_free < ctrl->td_depth) ? ctrl->td_free : 0;

    if (unlikely(td_flags & ESPE_TD_KICK_PKT)) {
        stat->td_kick++;
        spe_writel(spe->regs, SPE_TDQX_WPTR(portno), ctrl->td_free);
    }

    stat->td_config++;
    stat->td_config_bytes += len;

    spin_unlock_irqrestore(&ctrl->td_lock, flags);

    return 0;
}

static void espe_rd_record_result(struct espe_rd_desc *cur_desc, struct spe_port_stat *stat)
{
    struct desc_result_s *result = &stat->result;

    result->rd_result[cur_desc->rd_trans_result]++;               // rd desc bit0-1
    result->rd_pkt_drop_rsn[cur_desc->rd_drop_rsn]++;             // rd desc bit2-5
    result->rd_pkt_fw_path[__fls(cur_desc->rd_trans_path)]++;     // rd desc bit6-15
    result->rd_pkt_type[cur_desc->rd_pkt_type]++;                 // rd desc bit17-19
    result->rd_finsh_wrap_rsn[cur_desc->rd_finish_warp_res]++;    // rd desc bit20-22
    result->rd_send_cpu_rsn[cur_desc->rd_tocpu_res]++;            // rd desc bit23-26

    stat->result.rd_sport_cnt[cur_desc->rd_sport]++;
    stat->rd_finished_bytes += cur_desc->rd_pkt_len;

    return;
}

int espe_free_ncm_pkt(struct spe *spe, unsigned long long ncmh_maa_addr, unsigned int len)
{
    unsigned int pkt_num;
    unsigned long long cur_maa_addr;
    char *ncmh_addr = phys_to_virt(ncmh_maa_addr & 0xffffffffff);

    struct espe_nth32 *nth32 = (struct espe_nth32 *)ncmh_addr;
    struct espe_ndp32_hdr *ndp32 = (struct espe_ndp32_hdr *)(ncmh_addr + nth32->index);
    struct espe_ncm_data *cur_pkt = NULL;

    /*
     * data_region
     * skip cache and map phy_addr(data region)
     */
    dma_unmap_single(spe->dev, ncmh_maa_addr, MAA_OPIPE_LEVEL_1_SIZE, DMA_FROM_DEVICE);

    /* the index of the last spe_ndp32_data is zero.it is a symbol of end of this ndp. */
    if (unlikely(ndp32->nth_len <= (sizeof(struct espe_ndp32_hdr) + sizeof(struct espe_ndp32_data)))) {
        return -EINVAL;
    }

    pkt_num = (ndp32->nth_len - sizeof(struct espe_ndp32_hdr)) / (sizeof(struct espe_ndp32_data)) - 1;

    /* ptr_region */
    cur_pkt = (struct espe_ncm_data *)(ncmh_addr + ESPE_GETHER_RD_DIV);

    while (pkt_num > 0) {
        cur_maa_addr = ((unsigned long long)(cur_pkt->addr_high) << 32) + (unsigned long long)(cur_pkt->addr_low);
        espe_free_maa_mem(cur_maa_addr);
        cur_pkt++;
        pkt_num--;
    }
    espe_free_maa_mem(ncmh_maa_addr);
    return 0;
}

int espe_free_rndis_pkt(struct spe *spe, unsigned long long rndish_maa_addr, unsigned int len)
{
    unsigned long long cur_pkt_addr;
    unsigned int pkt_num = len / sizeof(struct espe_rndis_data);
    struct espe_rndis_data *rndis_data = (struct espe_rndis_data *)phys_to_virt(rndish_maa_addr & 0xffffffffff);

    dma_unmap_single(spe->dev, rndish_maa_addr, MAA_OPIPE_LEVEL_1_SIZE, DMA_FROM_DEVICE);

    while (pkt_num > 0) {
        cur_pkt_addr = ((unsigned long long)(rndis_data->addr_high) << 32) + (unsigned long long)(rndis_data->addr_low);

        /* bsp_maa_get_skb return maybe NULL */
        espe_free_maa_mem(cur_pkt_addr);
        rndis_data++;
        pkt_num--;
    }

    espe_free_maa_mem(rndish_maa_addr);

    return 0;
}

int espe_free_gethered_rd(struct spe *spe, unsigned long long maa_l2_addr, unsigned int len)
{
    /* ncm,rndis gather pkt */
    if ((spe->usbport.attr.enc_type == SPE_ENC_NCM_NTB32) || (spe->usbport.attr.enc_type == SPE_ENC_NCM_NTB16)) {
        if (espe_free_ncm_pkt(spe, maa_l2_addr, len)) {
            return -EINVAL;
        }
    } else if (spe->usbport.attr.enc_type == SPE_ENC_RNDIS) {
        if (espe_free_rndis_pkt(spe, maa_l2_addr, len)) {
            return -EINVAL;
        }
    } else {
        espe_bug(spe);
    }
    return 0;
}

static inline void espe_rd_transmit_desc(struct spe *spe, struct spe_port_ctx *port_ctx, unsigned long long maa_l2_addr,
                                         struct sk_buff *skb)
{
    if (port_ctx->ctrl.ops.espe_finish_rd && port_ctx->ctrl.port_flags.enable) {
        // Call netcard  cb
        port_ctx->ctrl.ops.espe_finish_rd(skb, skb->len, port_ctx->ctrl.port_priv, ESPE_RD_FLAG_MAGIC);
        port_ctx->stat.rd_sended++;
    } else {
        port_ctx->stat.rd_droped++;
        if (port_ctx->ctrl.portno == spe->usbport.portno) {
            if (espe_free_gethered_rd(spe, maa_l2_addr, skb->len)) {
                espe_bug(spe);
            }
        } else {
            espe_free_maa_mem(maa_l2_addr);
        }
    }
}

static inline void espe_rd_per_interval_cnt(struct spe_port_ctx *port_ctx, unsigned int rd_total_num)
{
    if (rd_total_num <= 1) {
        port_ctx->stat.rd_num_per_interval[NUM_1]++;
        return;
    }
    if (rd_total_num <= 256) {
        port_ctx->stat.rd_num_per_interval[NUM_256]++;
        return;
    }
    if (rd_total_num <= 512) {
        port_ctx->stat.rd_num_per_interval[NUM_512]++;
        return;
    }
    if (rd_total_num <= 1024) {
        port_ctx->stat.rd_num_per_interval[NUM_1024]++;
        return;
    }
    if (rd_total_num <= 2048) {
        port_ctx->stat.rd_num_per_interval[NUM_2048]++;
        return;
    }
    port_ctx->stat.rd_num_per_interval[NUM_TOOLARGE]++;
}


static inline void espe_rd_set_skb(struct sk_buff *skb, struct espe_rd_desc *desc,
    unsigned long long maa_l2_addr, unsigned long long org_maa_addr)
{
    struct rx_cb_map_s *rx_cb = NULL;

    rx_cb = (struct rx_cb_map_s *)(&skb->cb);
    rx_cb->packet_info.u32 = 0;
    
    switch (desc->rd_pkt_type) {
        case (RD_PKT_IPV4_TCP):
            rx_cb->packet_info.bits.ip_proto = AF_INET;
            rx_cb->packet_info.bits.l4_proto = (unsigned char)IPPROTO_TCP;
            rx_cb->packet_info.bits.is_accable = ESPE_ACCELETABLE_PKT_FLAG;
            break;
        case (RD_PKT_IPV4_UDP):
            rx_cb->packet_info.bits.ip_proto = AF_INET;
            rx_cb->packet_info.bits.l4_proto = (unsigned char)IPPROTO_UDP;
            rx_cb->packet_info.bits.is_accable = ESPE_ACCELETABLE_PKT_FLAG;

            break;
        case (RD_PKT_IPV6_TCP):
            rx_cb->packet_info.bits.ip_proto = AF_INET6;
            rx_cb->packet_info.bits.l4_proto = (unsigned char)IPPROTO_TCP;
            rx_cb->packet_info.bits.is_accable = ESPE_ACCELETABLE_PKT_FLAG;
            break;
        case (RD_PKT_IPV6_UDP):
            rx_cb->packet_info.bits.ip_proto = AF_INET6;
            rx_cb->packet_info.bits.l4_proto = (unsigned char)IPPROTO_UDP;
            rx_cb->packet_info.bits.is_accable = ESPE_ACCELETABLE_PKT_FLAG;
            break;
        default:
            // because of the condition to get into this branch, default can not be reached.
            break;
    }

    rx_cb->ipf_result.u32 = desc->rd_ipfres_stmid;
    rx_cb->userfield0 = desc->rd_user_field0;
    rx_cb->userfield1 = desc->rd_user_field1;
    rx_cb->userfield2 = desc->rd_user_field2;
    rx_cb->maa_org = org_maa_addr;
    rx_cb->maa_l2 = maa_l2_addr;
    rx_cb->packet_info.bits.l2_hdr_offeset = desc->rd_l2_hdr_offset;

}


void espe_rd_cur_desc_dump(unsigned int *cur_desc, unsigned int pos) {
    unsigned int *p = NULL;
        
    SPE_ERR("cpu rd intr, pos: %d  \n", pos);
    p = (unsigned int *)cur_desc;
    SPE_ERR("rd w0-w4: %x %x %x %x %x \n", *p, *(p + 1), *(p + 2), *(p + 3), *(p + 4));
    SPE_ERR("rd w5-w9: %x %x %x %x %x \n", *(p + 5), *(p + 6), *(p + 7), *(p + 8), *(p + 9));
    return;
}


void espe_td_desc_dump(struct spe *spe) {
    struct spe_port_ctrl *ctrl = NULL;
    struct espe_td_desc *cur_td = NULL;
    struct espe_td_desc *td_base = NULL;
    unsigned int *p = NULL;
    unsigned int td_rptr;
    unsigned int td_wptr;   

    unsigned int i;
    
    ctrl = &(spe->ports[spe->ipfport.portno].ctrl);
    td_base = (struct espe_td_desc *)ctrl->td_addr;

    for (i = 0; i < ctrl->td_depth; i++) {
        cur_td = &td_base[i];
        p = (unsigned int *)cur_td;
        SPE_ERR("i:%d \n", i);
        SPE_ERR("td w0-w4: %x %x %x %x %x \n", *p, *(p + 1), *(p + 2), *(p + 3), *(p + 4));
        SPE_ERR("td w5-w9: %x %x %x %x %x \n", *(p + 5), *(p + 6), *(p + 7), *(p + 8), *(p + 9));
    }
    
    td_rptr = spe_readl(spe->regs, SPE_TDQX_RPTR(spe->ipfport.portno));
    td_wptr = spe_readl(spe->regs, SPE_TDQX_WPTR(spe->ipfport.portno));
        
    SPE_ERR("hard td_rptr: %d, td_wptr: %d \n", td_rptr, td_wptr);
    
    return;
}

void espe_ptr_walk_info(unsigned int portno) {
    struct spe *spe = &g_espe;
    unsigned long long tdbase32l;
    unsigned long long tdbase32h;    
    unsigned long long rdbase32l;
    unsigned long long rdbase32h;
    unsigned int rd_rptr;
    unsigned int rd_wptr;

     // td queue
    tdbase32l = spe_readl_relaxed(spe->regs, SPE_TDQX_BADDR_L(portno));
    tdbase32h = spe_readl_relaxed(spe->regs, SPE_TDQX_BADDR_H(portno));

    // rd queue
    rdbase32l = spe_readl_relaxed(spe->regs, SPE_RDQX_BADDR_L(portno));
    rdbase32h = spe_readl_relaxed(spe->regs, SPE_RDQX_BADDR_H(portno));

    //rptr wptr
    rd_rptr = spe_readl_relaxed(spe->regs, SPE_RDQX_RPTR(portno));
    rd_wptr = spe_readl_relaxed(spe->regs, SPE_RDQX_WPTR(portno));
      
    SPE_ERR("td base: %lx, rd base: %lx \n", tdbase32l + (tdbase32h << 32), rdbase32l + (rdbase32h << 32));
    SPE_ERR("rd_rptr: %d, rd_wptr: %d \n", rd_rptr, rd_wptr);    
    SPE_ERR("soft td base: %lx, rd base: %lx \n", spe->ports[portno].ctrl.td_addr, spe->ports[portno].ctrl.rd_addr);  

}

/* start of new interface */
static inline struct sk_buff *espe_resolve_ipf_skb(struct spe *spe, struct espe_rd_desc *desc,
                                                   unsigned long long maa_l2_addr)
{
    struct sk_buff *skb = NULL;
    unsigned long long org_maa_addr;

    skb = bsp_maa_unmap(maa_l2_addr, &org_maa_addr);
    if (unlikely((skb == NULL) || (org_maa_addr < maa_l2_addr))) {
        SPE_ERR("fail to get maa from port %u maa %lx skb %lx \n", desc->rd_sport, maa_l2_addr, (uintptr_t)skb);
        espe_bug(spe);
        spe->dbg_level |= SPE_DBG_PTR_WALK;
        return NULL;
    }

    if (unlikely((skb->len != 0) || (skb->data_len != 0))) {
        SPE_ERR("maa skb error port %u maa %lx skb %lx len %u, data_len %u, data %lx, head %lx tail %lx\n", 
            desc->rd_sport, maa_l2_addr, (uintptr_t)skb, skb->len, skb->data_len, (uintptr_t)skb->data, 
            (uintptr_t)skb->head, (uintptr_t)skb->tail);
        espe_bug(spe);
        spe->dbg_level |= SPE_DBG_PTR_WALK;
        return NULL;
    }

    bsp_maa_reset_skb(skb);

    // in phone mode mac hdr must move to higher addr than maa hdr
    skb_push(skb, org_maa_addr - maa_l2_addr);
    skb_put(skb, desc->rd_pkt_len - (org_maa_addr - maa_l2_addr));

    espe_rd_set_skb(skb, desc, maa_l2_addr, org_maa_addr);

    if (unlikely(spe->dbg_level & SPE_DBG_PTR_WALK)) {
        spe->spe_rd_pkt_cnt++;
    }
    
    if (spe->spe_rd_pkt_cnt > 1024) {
        spe->dbg_level &= ~(SPE_DBG_PTR_WALK);
        spe->spe_rd_pkt_cnt = 0;
    }

    return skb;
}

static inline void espe_rd_process_one_ipf_desc(struct spe *spe, struct spe_port_ctx *port_ctx, void *long_buf)
{
    struct espe_rd_desc *desc = (struct espe_rd_desc *)long_buf;
    struct spe_cpuport_ctx *cpuport = &spe->cpuport;
    struct sk_buff *skb = NULL;

    unsigned long long maa_l2_addr;
    unsigned long long rdout32low;
    unsigned long long rdout32upper;

    rdout32low = (unsigned long long)desc->rd_outaddr_lower;  // long long for coverity
    rdout32upper = (unsigned long long)desc->rd_outaddr_upper;
    maa_l2_addr = rdout32low + (rdout32upper << 32) - desc->rd_l2_hdr_offset;

    // this is for dfs, DO NOT Remove
    port_ctx->stat.rd_finsh_pkt_num += desc->rd_pktnum;
    
    if (unlikely(maa_l2_addr == 0)) {
        spe->ports[cpuport->portno].stat.rd_maa_zero++;
        spe->ports[cpuport->portno].stat.rd_droped++;
        return;
    }

    if (spe->dbg_level & SPE_DBG_RD_RESULT) {
        espe_rd_record_result(desc, &port_ctx->stat);
    }

    skb = espe_resolve_ipf_skb(spe, desc, maa_l2_addr);
    if (unlikely(skb == NULL)) {
        spe->ports[cpuport->portno].stat.rd_droped++;
        return;
    }
    
    espe_rd_transmit_desc(spe, &spe->ports[cpuport->portno], maa_l2_addr, skb);
    return;
}

void espe_rd_complete_intr(unsigned int portno)
{
    struct spe *spe = &g_espe;
    struct spe_port_ctx *port_ctx = &spe->ports[portno];
    struct espe_rd_desc *rd_base = (struct espe_rd_desc *)port_ctx->ctrl.rd_addr;

    unsigned int rd_rptr = 0;
    unsigned int rd_wptr = 0;
    unsigned int rd_num = 0;
    unsigned int rd_botton_half_size = 0;
    unsigned long flags = 0;
    void *long_buf = port_ctx->ctrl.rd_long_buf;
    int ret = 0;
    int i = 0;

    port_ctx->stat.rd_finsh_intr_complete++;

    if (unlikely(spe->bugon_flag)) {
        return;
    }


    spin_lock_irqsave(&port_ctx->ctrl.rd_lock, flags);

    /* get rdq ptr, include rd and wr ptr */
    rd_rptr = spe_readl_relaxed(spe->regs, SPE_RDQX_RPTR(portno));
    rd_wptr = spe_readl_relaxed(spe->regs, SPE_RDQX_WPTR(portno));
    rd_num = spe_readl(spe->regs, SPE_RDQX_DEPTH(portno));

    if (rd_wptr > rd_rptr) {  // normal
        rd_num = rd_wptr - rd_rptr;

        ret = memcpy_s(long_buf, port_ctx->ctrl.rd_depth * sizeof(struct espe_rd_desc), &rd_base[rd_rptr],
                       rd_num * sizeof(struct espe_rd_desc));
        if (unlikely(ret)) {
            port_ctx->stat.normal_cpy_fail++;
            SPE_ERR("normal_cpy_fail \n");
            espe_bug(spe);
        }

    } else if (rd_wptr < rd_rptr) {  // rewind
        rd_num = (port_ctx->ctrl.rd_depth - rd_rptr + rd_wptr);

        // cpy button half
        rd_botton_half_size = (port_ctx->ctrl.rd_depth - rd_rptr) * sizeof(struct espe_rd_desc);
        ret = memcpy_s(long_buf, port_ctx->ctrl.rd_depth * sizeof(struct espe_rd_desc), &rd_base[rd_rptr],
                       rd_botton_half_size);
        if (unlikely(ret)) {
            port_ctx->stat.rewind_bh_cpy_fail++;
            SPE_ERR("rewind_bh_cpy_fail \n");
            espe_bug(spe);
        }

        long_buf = long_buf + rd_botton_half_size;

        ret = memcpy_s(long_buf, port_ctx->ctrl.rd_depth * sizeof(struct espe_rd_desc) - rd_botton_half_size,
                       &rd_base[0], rd_wptr * sizeof(struct espe_rd_desc));
        if (unlikely(ret)) {
            port_ctx->stat.rewind_th_cpy_fail++ ;
            SPE_ERR("rewind_th_cpy_fail \n");
            espe_bug(spe);
        }
    } else {
        rd_num = 0;
    }
    
    spe_writel(spe->regs, SPE_RDQX_RPTR(portno), rd_wptr);
    
    /* recycle rd from rd_busy to rdq rptr */
    for (i = 0; i < rd_num; i++) {  // idx useless
        port_ctx->stat.rd_finished++;
        long_buf = port_ctx->ctrl.rd_long_buf + i * sizeof(struct espe_rd_desc);
        espe_rd_process_one_ipf_desc(spe, port_ctx, long_buf);
        
        if (unlikely(spe->dbg_level & SPE_DBG_PTR_WALK)) {
            espe_rd_cur_desc_dump((unsigned int *)long_buf, i);
        } 
    }
    
    if (unlikely(spe->dbg_level & SPE_DBG_PTR_WALK)) {
        SPE_ERR("rd_rptr: %d, rd_wptr: %d  \n", rd_rptr, rd_wptr);    

        espe_td_desc_dump(spe);
        
        espe_ptr_walk_info(portno);
    }
    port_ctx->ctrl.rd_busy = rd_wptr;
        
    if (port_ctx->ctrl.ops.espe_complete_rd) {
        port_ctx->ctrl.ops.espe_complete_rd(port_ctx->ctrl.port_priv);
        port_ctx->stat.rd_finsh_intr_complete_called++;
    }

    espe_rd_per_interval_cnt(port_ctx, rd_num);

    if (unlikely((spe->spe_dfs_div_enable != 0) && 
        (rd_num > spe->dfs_div_threshold_pktnum) && 
        (spe->clk_div > spe->div_profile[ESPE_CLK_DIV_FULL_RATE_LEVEL]))) {
        spe_div_ctrl_t spe_div;
        spe_div.u32 = ESPE_CLK_DIV_DEFAULT;
        spe_div.bits.spe_div = spe->div_profile[ESPE_CLK_DIV_FULL_RATE_LEVEL];
        writel_relaxed(spe_div.u32, spe->peri.crg_addr + ESPE_CLK_DIV);
        spe->clk_div = spe->div_profile[ESPE_CLK_DIV_FULL_RATE_LEVEL] + 1;
    }
    spin_unlock_irqrestore(&port_ctx->ctrl.rd_lock, flags);

    return;
}


//this function for maa hard error eviding
void espe_ad_updata_wptr(struct spe *spe)
{
    struct spe_adq_ctrl *cur_ctrl = NULL;
    unsigned int wptr = 0;
    unsigned int adq_num = 0;
    unsigned long flags = 0;

    spin_lock_irqsave(&spe->spe_ad_lock, flags);
    for (adq_num = 0; adq_num < SPE_ADQ_BOTTOM; adq_num++) {
        cur_ctrl = &spe->adqs_ctx[adq_num].ctrl;
        wptr = readl(cur_ctrl->maa_wptr_stub_addr);

        if (wptr < spe->adqs_ctx[adq_num].ctrl.adq_size) {
            spe_writel(spe->regs, SPE_ADQ_WPTR(adq_num), wptr);
        }
    }
    spin_unlock_irqrestore(&spe->spe_ad_lock, flags);

    return;
}

#ifndef CONFIG_ESPE_PHONE_SOC
static void espe_adq_ctrl_timer(unsigned long arg)
{
    struct spe *spe = (struct spe *)(uintptr_t)arg;
    unsigned long flags = 0;

    spin_lock_irqsave(&spe->reset_lock, flags);
    if (spe->modem_resetting != 1) {
        spin_unlock_irqrestore(&spe->reset_lock, flags);
        espe_ad_updata_wptr(spe);
    } else {
        spin_unlock_irqrestore(&spe->reset_lock, flags);
    }

    mod_timer(&spe->spe_adq_timer, SPE_DFS_T(spe->spe_adq_time_interval));

    return;
}

void espe_adq_timer_init(struct spe *spe)
{
    init_timer(&spe->spe_adq_timer);
    spe->spe_adq_timer.function = espe_adq_ctrl_timer;
    spe->spe_adq_timer.data = (unsigned long)(uintptr_t)spe;
    spe->spe_adq_time_interval = SPE_ADQ_INTERVAL_DEFAULT;
    spe->spe_adq_timer.expires = SPE_DFS_T(spe->spe_adq_time_interval);
    add_timer(&spe->spe_adq_timer);
}
#endif

int espe_ad_init(struct spe *spe)
{
    struct spe_adq_ctrl *cur_ctrl = NULL;
    enum spe_adq_num adq_num;
    dma_addr_t maa_rptr_reg_dma_addr;
    dma_addr_t ad_wptr_reg_addr;

    unsigned int ad_thr;
    int ret = 0;

    if(spe->spe_version == ESPE_VER_5000){
        ad_thr = SPE_MIN_PKT_SIZE - 1;
    } else {
        ad_thr = SPE_AD_PKT_LEN_THR;
    }

    spe->adqs_ctx[SPE_ADQ0].ctrl.maa_ipip_type = MAA_IPIPE_FOR_SPE_512;
    spe->adqs_ctx[SPE_ADQ1].ctrl.maa_ipip_type = MAA_IPIPE_FOR_SPE_2K;
    spe->adqs_ctx[SPE_ADQ0].ctrl.adbuf_len = SPE_AD0_PKT_LEN;
    spe->adqs_ctx[SPE_ADQ1].ctrl.adbuf_len = SPE_AD1_PKT_LEN;
    spe->adqs_ctx[SPE_ADQ0].ctrl.adq_size = SPE_ADQ_DEF_SIZE;
    spe->adqs_ctx[SPE_ADQ1].ctrl.adq_size = SPE_ADQ_DEF_SIZE;

    spe_writel_relaxed(spe->regs, SPE_ETH_MAXLEN, SPE_AD1_PKT_LEN);
    spe_writel_relaxed(spe->regs, SPE_ADQ_THRESHOLD, ad_thr);
    spe_writel_relaxed(spe->regs, SPE_ADQ_LEN(SPE_ADQ0), SPE_ADQ_SHORT);
    spe_writel_relaxed(spe->regs, SPE_ADQ_LEN(SPE_ADQ1), SPE_ADQ_LONG);

    for (adq_num = SPE_ADQ0; adq_num < SPE_ADQ_BOTTOM; adq_num++) {
        cur_ctrl = &spe->adqs_ctx[adq_num].ctrl;

        cur_ctrl->ad_base_addr = dma_alloc_coherent(spe->dev, cur_ctrl->adq_size * sizeof(struct espe_ad_desc),
                                                    &cur_ctrl->ad_dma_addr, GFP_KERNEL);

        if (cur_ctrl->ad_base_addr == NULL) {
            SPE_ERR("alloc adq desc pool failed.\n");
            ret = -ENOMEM;
        }

        spe_writel_relaxed(spe->regs, SPE_ADQ_BADDR_L(adq_num), lower_32_bits(cur_ctrl->ad_dma_addr));
        spe_writel_relaxed(spe->regs, SPE_ADQ_BADDR_H(adq_num), upper_32_bits(cur_ctrl->ad_dma_addr));
        spe_writel_relaxed(spe->regs, SPE_ADQ_SIZE(adq_num), SPE_ADQ_SIZE_SETTING);

        if (spe->spe_version == ESPE_VER_5000) { //evide maa hard error

            cur_ctrl->maa_wptr_stub_addr = dma_alloc_coherent(spe->dev, sizeof(unsigned long long),
                                                    &cur_ctrl->maa_wptr_stub_dma_addr, GFP_KERNEL);
            if (cur_ctrl->maa_wptr_stub_addr == NULL){
                SPE_ERR("map maa stub fail.\n");
                ret = -ENOMEM;
            }

            maa_rptr_reg_dma_addr = (dma_addr_t)bsp_maa_set_adqbase((unsigned long long)cur_ctrl->ad_dma_addr,
                                                            cur_ctrl->maa_wptr_stub_dma_addr, SPE_ADQ_DEF_SIZE,
                                                            spe->adqs_ctx[adq_num].ctrl.maa_ipip_type);

        } else {
            ad_wptr_reg_addr = spe->res->start + SPE_ADQ_WPTR(adq_num);
            maa_rptr_reg_dma_addr = (dma_addr_t)bsp_maa_set_adqbase((unsigned long long)cur_ctrl->ad_dma_addr,
                                                                lower_32_bits(ad_wptr_reg_addr), SPE_ADQ_DEF_SIZE,
                                                                spe->adqs_ctx[adq_num].ctrl.maa_ipip_type);
        }

        spe_writel(spe->regs, SPE_ADQ_RPTR_UPDATE_ADDR_L(adq_num), lower_32_bits(maa_rptr_reg_dma_addr));
        spe_writel(spe->regs, SPE_ADQ_RPTR_UPDATE_ADDR_H(adq_num), upper_32_bits(maa_rptr_reg_dma_addr));

        spe_writel(spe->regs, SPE_ADQ_EN(adq_num), 1);
    }

    spe_writel_relaxed(spe->regs, SPE_ADQ_EMPTY_INTA_MASK, SPE_AD_INTR_EN);

    return ret;
}

void espe_ad_exit(struct spe *spe)
{
    struct spe_adq_ctrl *cur_ctrl = NULL;
    enum spe_adq_num adq_num;

    for(adq_num = SPE_ADQ0; adq_num < SPE_ADQ_BOTTOM; adq_num++) {
        cur_ctrl = &spe->adqs_ctx[adq_num].ctrl;
        if(cur_ctrl->ad_base_addr != NULL){
            dma_free_coherent(spe->dev, cur_ctrl->adq_size*sizeof(struct espe_ad_desc),
                              cur_ctrl->ad_base_addr, cur_ctrl->ad_dma_addr);
            cur_ctrl->ad_base_addr = NULL;
        }
        if (spe->spe_version == ESPE_VER_5000) {
            if (cur_ctrl->maa_wptr_stub_addr != NULL){
                dma_free_coherent(spe->dev, sizeof(unsigned long long),
                                    cur_ctrl->maa_wptr_stub_addr, cur_ctrl->maa_wptr_stub_dma_addr);
                cur_ctrl->maa_wptr_stub_addr = NULL;
            }
            del_timer(&spe->spe_adq_timer);
        }
    }
    return;
}

MODULE_LICENSE("GPL");

