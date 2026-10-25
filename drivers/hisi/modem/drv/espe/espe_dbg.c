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

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/icmp.h>
#include <linux/igmp.h>


#include <bsp_espe.h>
#ifdef CONFIG_HUAWEI_DUBAI
#include <chipset_common/dubai/dubai.h>
#endif

#include "espe.h"
#include "espe_dbg.h"
#include "espe_desc.h"
#include "espe_desc.h"


#define PORTNO_INVALID(portno) (portno >= SPE_PORT_NUM)

void espe_get_port_type(unsigned int portno, enum spe_enc_type enc_type)
{
    char *port_type_char = NULL;
    switch (enc_type) {
        case (SPE_ENC_NONE):
            port_type_char = "eth";
            break;
        case (SPE_ENC_NCM_NTB16):
            port_type_char = "ntb16";
            break;
        case (SPE_ENC_NCM_NTB32):
            port_type_char = "ntb32";
            break;
        case (SPE_ENC_RNDIS):
            port_type_char = "rndis";
            break;
        case (SPE_ENC_WIFI):
            port_type_char = "wifi";
            break;
        case (SPE_ENC_IPF):
            port_type_char = "ipf";
            break;
        case (SPE_ENC_CPU):
            port_type_char = "cpu";
            break;
        case (SPE_ENC_ACK):
            port_type_char = "ack";
            break;
        case (SPE_ENC_DIRECT_FW_HP):
            port_type_char = "direct_fw_hp";
            break;
        case (SPE_ENC_DIRECT_FW_MP):
            port_type_char = "direct_fw_mp";
            break;
        case (SPE_ENC_DIRECT_FW_PE):
            port_type_char = "direct_fw_pe";
            break;
        default:
            port_type_char = "error";
    }
    SPE_ERR("spe port %u type:%s \n", portno, port_type_char);
}

void espe_get_port_type_dbg(struct spe_port_ctrl *ctrl)
{
    if (ctrl->net != NULL) {
        SPE_ERR("device name     %s \n", ctrl->net->name);
    }
    espe_get_port_type(ctrl->portno, ctrl->property.bits.spe_port_enc_type);
}

void espe_all_port_show(void) {
    unsigned int i;
	unsigned int portno = 0;
    struct spe *spe = &g_espe;
	struct spe_port_ctrl *ctrl = NULL;

    portno = find_first_zero_bit(&spe->portmap[SPE_PORTS_NOMARL], SPE_PORT_NUM);

	for (i = 0; i < portno; i++) {
        ctrl = &spe->ports[i].ctrl;
	    espe_get_port_type(ctrl->portno, ctrl->property.bits.spe_port_enc_type);
	}
}

void spe_help(void)
{
    SPE_ERR("01. spe_dev_setting \n");
    SPE_ERR("02. spe_dev_info \n");
    SPE_ERR("03. spe_port_setting(port_num) \n");
    SPE_ERR("04. spe_port_rd_info(port_num) \n");
    SPE_ERR("05. spe_port_td_info(port_num) \n");
    SPE_ERR("06. spe_port_info_all(port_num) \n");
    SPE_ERR("07. espe_cpu_port_info \n");
    SPE_ERR("08. spe_dfs_get \n");
    SPE_ERR("09. spe_intr_set_interval(32.768khz) \n");
    SPE_ERR("10. spe_set_cpuport_maxrate(mbps) \n");

    SPE_ERR("11. spe_ipfw_setting \n");
    SPE_ERR("12. spe_macfw_entry_dump \n");
    SPE_ERR("13. espe_direct_fw_status \n");

    SPE_ERR("14. spe_add_err_show \n");
    SPE_ERR("15. spe_ip_fw_show \n");

    SPE_ERR("16. spe_print_td(port_num td_pos) :print info of one td \n");
    SPE_ERR("17. spe_print_rd(port_num rd_pos) :print info of one rd \n");

    SPE_ERR("18. spe_set_result_record:0:def 1:td 2:rd 3:rdsave 4:bugon \n");
    SPE_ERR("19. spe_set_msg:0:def 1:trace 2:info 3:dbg 4:tuple \n");
    SPE_ERR("20. spe_set_hids_ul:0:def 1:trace 2:info 3:dbg 4:tuple \n");
    SPE_ERR("21. espe_all_port_show \n");
}

void dbgen_en(void)
{
    struct spe *spe = &g_espe;
    spe_dbgen_t spe_dbg;

    spe_dbg.u32 = spe_readl(spe->regs, SPE_DBGEN);
    spe_dbg.bits.spe_dbgen = 1;
    spe_writel(spe->regs, SPE_DBGEN, spe_dbg.u32);
}

void dbgen_dis(void)
{
    struct spe *spe = &g_espe;
    spe_dbgen_t spe_dbg;

    spe_dbg.u32 = spe_readl(spe->regs, SPE_DBGEN);
    spe_dbg.bits.spe_dbgen = 0;
    spe_writel(spe->regs, SPE_DBGEN, spe_dbg.u32);
}

void espe_set_smp_def_core(unsigned int def_core)
{
    struct spe *spe = &g_espe;
    spe->smp.def_cpuport_core = def_core;
}

void espe_set_smp_hp_core(unsigned int hp_core)
{
    struct spe *spe = &g_espe;
    spe->smp.hp_cpuport_core = hp_core;
}

void espe_lock_smp_core(unsigned int core)
{
    struct spe *spe = &g_espe;
    spe->smp.def_cpuport_core = core;
    spe->smp.hp_cpuport_core = core;
    spe->smp.cpuport_core = core;
}

void espe_cpu_port_info(void)
{
    struct spe *spe = &g_espe;

    SPE_ERR("cpu_pkt_max_rate %u mbps \n", spe->cpuport.cpu_pkt_max_rate);
    SPE_ERR("cpu_pktnum_per_interval %u \n", spe->cpuport.cpu_pktnum_per_interval);

    SPE_ERR("cpu_updonly %u \n", spe->cpuport.cpu_updonly);
    SPE_ERR("cpu_updonly_comp %u \n", spe->cpuport.cpu_updonly_comp);
    SPE_ERR("cpu_rd_num %u \n", spe->cpuport.cpu_rd_num);
    SPE_ERR("cpu_rd_udp_drop %u \n", spe->cpuport.cpu_rd_udp_drop);
    SPE_ERR("cpu_rd_dfw_updonly_drop %u \n", spe->cpuport.cpu_rd_dfw_updonly_drop);

    SPE_ERR("cpu_rd_to_wan %u \n", spe->cpuport.cpu_rd_to_wan);
    SPE_ERR("cpu_rd_to_wan_fail %u \n", spe->cpuport.cpu_rd_to_wan_fail);
    SPE_ERR("cpu_rd_to_nic %u \n", spe->cpuport.cpu_rd_to_nic);
    SPE_ERR("cpu_rd_to_nic_fail %u \n", spe->cpuport.cpu_rd_to_nic_fail);
    SPE_ERR("cpu_rd_to_netif_rx %u \n", spe->cpuport.cpu_rd_to_netif_rx);
    SPE_ERR("cpu_rd_to_netif_rx_succ %u \n", spe->cpuport.cpu_rd_to_netif_rx_succ);
    SPE_ERR("cpu_rd_to_netif_rx_fail %u \n", spe->cpuport.cpu_rd_to_netif_rx_fail);
}

void espe_print_one_td(struct espe_td_desc *cur_td)
{
    SPE_ERR("TD word 0 \n");
    SPE_ERR("td_int_en %u \n", cur_td->td_int_en);
    SPE_ERR("td_mode %u \n", cur_td->td_mode);
    SPE_ERR("td_fc_head %u \n", cur_td->td_fc_head);
    SPE_ERR("td_push_en %u \n", cur_td->td_push_en);
    SPE_ERR("td_high_pri_flag %u \n", cur_td->td_high_pri_flag);
    SPE_ERR("td_pkt_len %u \n", cur_td->td_pkt_len);

    SPE_ERR("TD word 1~2 \n");
    SPE_ERR("td_in_lower %x \n", cur_td->td_inaddr_lower);
    SPE_ERR("td_in_upper %x \n", cur_td->td_inaddr_upper);

    SPE_ERR("TD word 3 \n");
    SPE_ERR("td_pdu_ssid %u \n", cur_td->td_pdu_ssid);
    SPE_ERR("td_modem_id %u \n", cur_td->td_modem_id);
    SPE_ERR("td_iptype %u \n", cur_td->td_iptype);

    SPE_ERR("TD word 4 \n");
    SPE_ERR("td_bypass_en %u \n", cur_td->td_bypass_en);
    SPE_ERR("td_bypass_addr %u \n", cur_td->td_bypass_addr);
    SPE_ERR("td_host_ana %u \n", cur_td->td_host_ana);
    SPE_ERR("td_drop_ack_ind %u \n", cur_td->td_drop_ack_ind);
    SPE_ERR("td_usb_net_id %u \n", cur_td->td_usb_net_id);

    SPE_ERR("TD word 5-9 \n");
    SPE_ERR("td_result %x \n", cur_td->td_result);

    SPE_ERR("td_info %x \n", cur_td->td_info);

    SPE_ERR("td_user_field0 %x \n", cur_td->td_user_field0);
    SPE_ERR("td_user_field1 %x \n", cur_td->td_user_field1);
    SPE_ERR("td_user_field2 %x \n", cur_td->td_user_field2);
}

void spe_print_td(unsigned int port_num, unsigned int td_pos)
{
    struct spe_port_ctrl *ctrl = NULL;
    struct espe_td_desc *cur_td = NULL;
    struct espe_td_desc *td_base = NULL;

    if (PORTNO_INVALID(port_num)) {
        SPE_ERR("port num %u is invalid!\n", port_num);
        return;
    }
    ctrl = &g_espe.ports[port_num].ctrl;
    td_base = (struct espe_td_desc *)ctrl->td_addr;

    if (td_pos >= ctrl->td_depth) {
        SPE_ERR("td_pos %u is invalid!\n", td_pos);
        return;
    }
    cur_td = &td_base[td_pos];
    espe_print_one_td(cur_td);
    return;
}

void espe_print_one_rd(struct espe_rd_desc *cur_rd)
{
    SPE_ERR("rd_int_en %u \n", cur_rd->rd_int_en);
    SPE_ERR("rd_mode %u \n", cur_rd->rd_mode);
    SPE_ERR("rd_fc_head %u \n", cur_rd->rd_fc_head);
    SPE_ERR("rd_high_pri_flag %u \n", cur_rd->rd_high_pri_flag);
    SPE_ERR("rd_pkt_len %u \n", cur_rd->rd_pkt_len);

    SPE_ERR("\n RD word 1~2 \n");
    SPE_ERR("rd_out_lower %x \n", cur_rd->rd_outaddr_lower);
    SPE_ERR("rd_out_upper %x \n", cur_rd->rd_outaddr_upper);

    SPE_ERR("\n RD word 3 \n");
    SPE_ERR("rd_pdu_ssid %u \n", cur_rd->rd_pdu_ssid);
    SPE_ERR("rd_ips_id %u \n", cur_rd->rd_ips_id);
    SPE_ERR("rd_modem_id %u \n", cur_rd->rd_modem_id);
    SPE_ERR("rd_l2_hdr_offset %u \n", cur_rd->rd_l2_hdr_offset);
    SPE_ERR("rd_iptype %u \n", cur_rd->rd_iptype);

    SPE_ERR("\n RD word 4 \n");
    SPE_ERR("rd_trans_result %u \n", cur_rd->rd_trans_result);
    SPE_ERR("rd_drop_rsn %u \n", cur_rd->rd_drop_rsn);
    SPE_ERR("rd_trans_path %u \n", cur_rd->rd_trans_path);
    SPE_ERR("rd_trans_path_finish %u \n", cur_rd->rd_trans_path_finish);
    SPE_ERR("rd_pkt_type %u \n", cur_rd->rd_pkt_type);
    SPE_ERR("rd_finish_warp_res %u \n", cur_rd->rd_finish_warp_res);
    SPE_ERR("rd_tocpu_res %u \n", cur_rd->rd_tocpu_res);
    SPE_ERR("rd_updata_only %u \n", cur_rd->rd_updata_only);

    SPE_ERR("\n RD word 5 \n");
    SPE_ERR("rd_sport %x \n", cur_rd->rd_sport);
    SPE_ERR("rd_dport %x \n", cur_rd->rd_dport);
    SPE_ERR("rd_pktnum %x \n", cur_rd->rd_pktnum);
    SPE_ERR("rd_ethtype %x \n", cur_rd->rd_ethtype);

    SPE_ERR("\n RD word 6-9 \n");
    SPE_ERR("rd_ipfres_stmid %x \n", cur_rd->rd_ipfres_stmid);
    SPE_ERR("rd_user_field0 %x \n", cur_rd->rd_user_field0);
    SPE_ERR("rd_user_field1 %x \n", cur_rd->rd_user_field1);
    SPE_ERR("rd_user_field2 %x \n", cur_rd->rd_user_field2);
}

void spe_print_rd(unsigned int port_num, unsigned int rd_pos)
{
    struct spe_port_ctrl *ctrl = NULL;
    struct espe_rd_desc *cur_rd = NULL;
    struct espe_rd_desc *rd_base = NULL;

    if (PORTNO_INVALID(port_num)) {
        SPE_ERR("port num %u is invalid!\n", port_num);
        return;
    }
    ctrl = &g_espe.ports[port_num].ctrl;
    rd_base = (struct espe_rd_desc *)ctrl->rd_addr;

    if (rd_pos >= ctrl->rd_depth) {
        SPE_ERR("rd_pos %u is invalid!\n", rd_pos);
        return;
    }
    cur_rd = &rd_base[rd_pos];
    espe_print_one_rd(cur_rd);
    return;
}

void spe_dev_setting(void)
{
    struct spe *spe = &g_espe;

    SPE_ERR("msg_level                   :%u\n", spe->msg_level);
    SPE_ERR("dbg_level                   :%u\n", spe->dbg_level);
    SPE_ERR("phy portmap                 :0x%lx\n", spe->portmap[SPE_PORTS_NOMARL]);
    SPE_ERR("br portmap                  :0x%lx\n", spe->portmap[SPE_PORTS_BR]);
    SPE_ERR("ipfw_timeout                :%u\n", spe->ipfw.ipfw_timeout);
    SPE_ERR("macfw_timeout               :%u\n", spe->macfw.macfw_timeout);
    SPE_ERR("flags                       :%u\n", spe->flags);
    SPE_ERR("mask_flags                       :%u\n", spe->mask_flags);

    // fixme:add each port info
}

int spe_set_hids_ul(unsigned int enable, unsigned int portno)
{
    struct spe *spe = &g_espe;
    int i;

    if (portno == SPE_PORT_NUM) {
        for (i = 0; i < SPE_PORT_NUM; i++) {
            spe->ports[i].ctrl.port_flags.hids_upload = !!enable;
        }
    } else if ((portno >= 0) && (portno < SPE_PORT_NUM)) {
        spe->ports[portno].ctrl.port_flags.hids_upload = !!enable;
    } else {
        SPE_ERR("input err\n");
    }
    return 0;
}

int spe_set_result_record(unsigned int level)
{
    struct spe *spe = &g_espe;
    switch (level) {
        case 0:
            spe->dbg_level =  SPE_DBG_WARN_ON;
            break;
        case 1:
            spe->dbg_level |= SPE_DBG_TD_RESULT;
            break;
        case 2:
            spe->dbg_level |= SPE_DBG_RD_RESULT;
            break;
        case 3:
            spe->dbg_level |= SPE_DBG_RD_SAVE;
            break;
        case 4:
            spe->dbg_level |= SPE_DBG_BUG_ON;
            break;
        case 5:
            spe->dbg_level |= SPE_DBG_PTR_WALK;
            break;
        default:
            spe->dbg_level |= SPE_DBG_TD_RESULT;
    }

    SPE_ERR("dbg_level                :%x\n", spe->dbg_level);

    return 0;
}

int spe_set_msg(unsigned int msg_level)
{
    struct spe *spe = &g_espe;
    switch (msg_level) {
        case 0:
            spe->msg_level = SPE_MSG_ERR | SPE_MSG_TRACE;
            break;
        case 1:
            spe->msg_level |= SPE_MSG_TRACE;
            break;
        case 2:
            spe->msg_level |= SPE_MSG_INFO;
            break;
        case 3:
            spe->msg_level |= SPE_MSG_DBG;
            break;
        case 4:
            spe->msg_level |= SPE_MSG_TUPLE;
            break;

        default:
            spe->msg_level |= SPE_MSG_ERR | SPE_MSG_TRACE;
    }

    SPE_ERR("msg_level                :%x\n", spe->msg_level);
    return 0;
}

int spe_set_msgdbg(unsigned int msg_level)
{
    struct spe *spe = &g_espe;
    spe->msg_level = msg_level;

    return 0;
}

void spe_dev_info(void)
{
    struct spe *spe = &g_espe;
    int i;

    SPE_ERR("ipfw_add                :%u\n", spe->stat.ipfw_add);
    SPE_ERR("ipfw_add_enter          :%u\n", spe->stat.ipfw_add_enter);
    SPE_ERR("ipfw_add_leave          :%u\n", spe->stat.ipfw_add_leave);
    SPE_ERR("ipfw_del                :%u\n", spe->stat.ipfw_del);
    SPE_ERR("ipfw_del_enter          :%u\n", spe->stat.ipfw_del_enter);
    SPE_ERR("ipfw_del_leave          :%u\n", spe->stat.ipfw_del_leave);
    SPE_ERR("ipfw_del_nothing_leave          :%u\n", spe->stat.ipfw_del_nothing_leave);
    SPE_ERR("ip_fw_not_add          :%u\n", spe->ipfw.ip_fw_not_add);

    SPE_ERR("disable_timeout         :%u\n", spe->stat.disable_timeout);
    SPE_ERR("wait_idle                   :%u\n", spe->stat.wait_idle);

    SPE_ERR("spe->suspend_count         :%u\n", spe->suspend_count);
    SPE_ERR("spe->resume_count                   :%u\n", spe->resume_count);

    SPE_ERR("modem_reset_count         :%u\n", spe->modem_reset_count);
    SPE_ERR("modem_unreset_count         :%u\n", spe->modem_unreset_count);
    SPE_ERR("modem_noreset_count                   :%u\n", spe->modem_noreset_count);

    for (i = 0; i < SPE_ADQ_BOTTOM; i++) {
        SPE_ERR("evt_ad_empty[%u]               :%u\n", i, spe->stat.evt_ad_empty[i]);
    }

    SPE_ERR("evt_td_errport              :%u\n", spe->stat.evt_td_errport);

    for (i = 0; i < SPE_PORT_NUM; i++) {
        if (test_bit(i, &spe->portmap[SPE_PORTS_NOMARL])) {
            SPE_ERR("port[%u]:               :%u\n", i, 1);

            SPE_ERR("evt_td_complt[%u]               :%u\n", i, spe->stat.evt_td_complt[i]);
            SPE_ERR("evt_td_full[%u]               :%u\n", i, spe->stat.evt_td_full[i]);
            SPE_ERR("evt_rd_complt[%u]               :%u\n", i, spe->stat.evt_rd_complt[i]);
            SPE_ERR("evt_rd_empty[%u]               :%u\n", i, spe->stat.evt_rd_empty[i]);
            SPE_ERR("evt_rd_full[%u]               :%u\n", i, spe->stat.evt_rd_full[i]);
        }
    }
}

void spe_print_mac_addr(unsigned char *mac)
{
    int i;
    for (i = 0; i < 6; i++) {
        SPE_ERR("  %2x  ", mac[i]);
    }
    SPE_ERR(" \n");
}

void spe_td_usb_result_print(struct spe_port_ctrl *ctrl, struct spe_port_stat *stat)
{
    // td result bit 22-25


    if (!(SPE_ENC_NCM_NTB16 == ctrl->property.bits.spe_port_enc_type ||
          SPE_ENC_NCM_NTB32 == ctrl->property.bits.spe_port_enc_type ||
          SPE_ENC_RNDIS == ctrl->property.bits.spe_port_enc_type)) {
        SPE_ERR("[td_no_wrap]:finish_success %u \n", stat->result.td_unwrap[TD_FINISH_SUCCESS]);
        SPE_ERR("[td_no_wrap]:port_disable %u \n", stat->result.td_unwrap[TD_PORT_DISABLE]);
        SPE_ERR("[td_no_wrap]:pkt_len_abnormity %u \n", stat->result.td_unwrap[TD_PKT_LEN_ABNORMITY]);
    }
}

void spe_port_td_info(unsigned int portno)
{
    unsigned int i = portno;
    unsigned int td_wptr = 0;
    unsigned int td_rptr = 0;
    struct spe *spe = &g_espe;
    struct spe_port_stat *stat = &spe->ports[i].stat;
    struct spe_port_ctrl *ctrl = &spe->ports[i].ctrl;

    if (PORTNO_INVALID(portno)) {
        SPE_ERR("port num is invalid!\n");
        return;
    }
    espe_get_port_type_dbg(ctrl);
    SPE_ERR("td_busy 0x%x \n", ctrl->td_busy);
    SPE_ERR("td_free 0x%x \n", ctrl->td_free);

    td_wptr = spe_readl(spe->regs, SPE_TDQX_WPTR(portno));
    td_rptr = spe_readl(spe->regs, SPE_TDQX_RPTR(portno));

    SPE_ERR("td_hard_ptr wptr 0x%x \n", td_wptr);
    SPE_ERR("td_hard_ptr rptr  0x%x \n", td_rptr);

    SPE_ERR("td config %u \n", stat->td_config);

    SPE_ERR("td config(port disabled) %u \n", stat->td_port_disabled);
    SPE_ERR("td full times %u \n", stat->td_full);
    SPE_ERR("td finish event %u \n", stat->td_finsh_intr_complete);
    SPE_ERR("td desc complete %u \n", stat->td_desc_complete);
    SPE_ERR("td pkt complete %u \n", stat->td_pkt_complete);
    SPE_ERR("td desc fail drop %u \n", stat->td_desc_fail_drop);

    SPE_ERR("td push %u \n", stat->td_kick);
    SPE_ERR("td_dma_null %u \n", stat->td_dma_null);
}

void spe_td_result_print(struct spe_port_ctrl *ctrl, struct spe_port_stat *stat)
{
    // td result bit 0-1
    SPE_ERR("[td_result]:updata_only %u \n", stat->result.td_result[TD_RESULT_UPDATA_ONLY]);
    SPE_ERR("[td_result]:discard %u \n", stat->result.td_result[TD_RESULT_DISCARD]);
    SPE_ERR("[td_result]:normal %u \n", stat->result.td_result[TD_RESULT_NORMAL]);
    SPE_ERR("[td_result]:wrap_or_lenth_wrong %u \n", stat->result.td_result[TD_RESULT_WRAP_OR_LENTH_WRONG]);

    // td result bit 2-5
    SPE_ERR("[td_ptk_drop_rsn]:trans_succ %u \n", stat->result.td_ptk_drop_rsn[TD_DROP_RSN_TRANS_SUCC]);
    SPE_ERR("[td_ptk_drop_rsn]:unwrap_err %u \n", stat->result.td_ptk_drop_rsn[TD_DROP_RSN_UNWRAP_ERR]);
    SPE_ERR("[td_ptk_drop_rsn]:length_err %u \n", stat->result.td_ptk_drop_rsn[TD_DROP_RSN_LENGTH_ERR]);
    SPE_ERR("[td_ptk_drop_rsn]:sport_disable %u \n", stat->result.td_ptk_drop_rsn[TD_DROP_RSN_SPORT_DISABLE]);
    SPE_ERR("[td_ptk_drop_rsn]:mac_filt %u \n", stat->result.td_ptk_drop_rsn[TD_DROP_RSN_MAC_FILT]);
    SPE_ERR("[td_ptk_drop_rsn]:ttl_zero %u \n", stat->result.td_ptk_drop_rsn[TD_DROP_RSN_TTL_ZERO]);
    SPE_ERR("[td_ptk_drop_rsn]:ip_filt%u \n", stat->result.td_ptk_drop_rsn[TD_DROP_RSN_IP_FILT]);
    SPE_ERR("[td_ptk_drop_rsn]:udp_rate_limit %u \n", stat->result.td_ptk_drop_rsn[TD_DROP_RSN_UDP_RATE_LIMIT]);
    SPE_ERR("[td_ptk_drop_rsn]:mac_fw_entry_err %u \n", stat->result.td_ptk_drop_rsn[TD_DROP_RSN_MAC_FW_ENTRY_ERR]);
    SPE_ERR("[td_ptk_drop_rsn]:dport_disable %u \n", stat->result.td_ptk_drop_rsn[TD_DROP_RSN_DPORT_DISABLE]);
    SPE_ERR("[td_ptk_drop_rsn]:v6_hop_limit_zero %u \n", stat->result.td_ptk_drop_rsn[TD_DROP_RSN_V6_HOP_LIMIT_ZERO]);
    SPE_ERR("[td_ptk_drop_rsn]:rd_ptr_null %u \n", stat->result.td_ptk_drop_rsn[TD_DROP_RSN_RD_PTR_NULL]);

    // td result bit 6-16
    SPE_ERR("[td_pkt_fw_path]:mac_filter %u \n", stat->result.td_pkt_fw_path[TD_FW_MAC_FILTER]);
    SPE_ERR("[td_pkt_fw_path]:eth_type %u \n", stat->result.td_pkt_fw_path[TD_FW_ETH_TYPE]);
    SPE_ERR("[td_pkt_fw_path]:1st_macfw %u \n", stat->result.td_pkt_fw_path[TD_FW_1ST_MACFW]);
    SPE_ERR("[td_pkt_fw_path]:ip_hdr %u \n", stat->result.td_pkt_fw_path[TD_FW_IP_HDR_CHECK]);
    SPE_ERR("[td_pkt_fw_path]:ip_filter %u \n", stat->result.td_pkt_fw_path[TD_FW_IP_FILTER]);
    SPE_ERR("[td_pkt_fw_path]:tcp_or_udp %u \n", stat->result.td_pkt_fw_path[TD_FW_TCP_UDP_CHECK]);
    SPE_ERR("[td_pkt_fw_path]:hash_calculate %u \n", stat->result.td_pkt_fw_path[TD_FW_HASH_CALC]);
    SPE_ERR("[td_pkt_fw_path]:hash_cache %u \n", stat->result.td_pkt_fw_path[TD_FW_HASH_CACHE]);
    SPE_ERR("[td_pkt_fw_path]:hash_ddr %u \n", stat->result.td_pkt_fw_path[TD_FW_HASH_DDR]);
    SPE_ERR("[td_pkt_fw_path]:2nd_macfw %u \n", stat->result.td_pkt_fw_path[TD_FW_2ND_MACFW]);
    SPE_ERR("[td_pkt_fw_path]:complete %u \n", stat->result.td_pkt_fw_path[TD_FW_COMPLETE]);

    // td result bit 17-19
    SPE_ERR("[td_pkt_type]:ipv4_tcp %u \n", stat->result.td_pkt_type[TD_PKT_IPV4_TCP]);
    SPE_ERR("[td_pkt_type]:ipv4_udp %u \n", stat->result.td_pkt_type[TD_PKT_IPV4_UDP]);
    SPE_ERR("[td_pkt_type]:ipv4_non_udp_tcp %u \n", stat->result.td_pkt_type[TD_PKT_IPV4_NON_UDP_TCP]);
    SPE_ERR("[td_pkt_type]:ipv6_tcp %u \n", stat->result.td_pkt_type[TD_PKT_IPV6_TCP]);
    SPE_ERR("[td_pkt_type]:ipv6_udp %u \n", stat->result.td_pkt_type[TD_PKT_IPV6_UDP]);
    SPE_ERR("[td_pkt_type]:ipv6_non_udp_tcp %u \n", stat->result.td_pkt_type[TD_PKT_IPV6_NON_UDP_TCP]);
    SPE_ERR("[td_pkt_type]:non_udp_tcp %u \n", stat->result.td_pkt_type[TD_PKT_NON_UDP_TCP]);
    SPE_ERR("[td_pkt_type]:trans_before_ip_check %u \n", stat->result.td_pkt_type[TD_PKT_TRANS_BEFORE_IP_CHECK]);

    // td result bit 20-21
    SPE_ERR("[td_warp]:success %u \n", stat->result.td_warp[TD_WARP_SUCCESS]);
    SPE_ERR("[td_warp]:discard %u \n", stat->result.td_warp[TD_WARP_PART_DISCARD]);
    SPE_ERR("[td_warp]:all_discard %u \n", stat->result.td_warp[TD_WARP_ALL_DISCARD]);
}

void spe_td_info_adv(unsigned int portno)
{
    unsigned int i = portno;
    struct spe *spe = &g_espe;
    struct spe_port_stat *stat = &spe->ports[i].stat;
    struct spe_port_ctrl *ctrl = &spe->ports[i].ctrl;

    if (PORTNO_INVALID(portno)) {
        SPE_ERR("port num is invalid!\n");
        return;
    }

    spe_port_td_info(portno);

    SPE_ERR("\n");

    spe_td_result_print(ctrl, stat);
    spe_td_usb_result_print(ctrl, stat);
}

void spe_port_setting(unsigned int portno)
{
    unsigned int i = portno;
    struct spe_port_ctrl *ctrl = NULL;
    spe_port_prop_t ch_propert;

    if (PORTNO_INVALID(portno)) {
        SPE_ERR("port num is invalid!\n");
        return;
    }
    
    ctrl = &g_espe.ports[i].ctrl;
    ch_propert.u32 = ctrl->property.u32;
    
    espe_get_port_type_dbg(ctrl);

    SPE_ERR("is_enable       0x%x \n", ch_propert.bits.spe_port_en);
    SPE_ERR("port_pad_en   0x%x \n", ch_propert.bits.spe_port_pad_en);
    SPE_ERR("port_attach     0x%x \n", ch_propert.bits.spe_port_attach_brg);
    SPE_ERR("enc_type        0x%x \n", ch_propert.bits.spe_port_enc_type);

    SPE_ERR("port mac addr :");
    if (ctrl->net != NULL) {
        spe_print_mac_addr(ctrl->net->dev_addr);
    } else {
        SPE_ERR("no dev\n");
    }

    SPE_ERR("rd_addr         0x%lx \n", (uintptr_t)ctrl->rd_addr);
    SPE_ERR("rd_depth        %u \n", ctrl->rd_depth);
    SPE_ERR("rd_evt_gap      0x%x \n", ctrl->rd_evt_gap);
    SPE_ERR("rd_dma          %lx \n", (uintptr_t)ctrl->rd_dma);

    SPE_ERR("td_addr         0x%lx \n", (uintptr_t)ctrl->td_addr);
    SPE_ERR("td_depth        %u \n", ctrl->td_depth);
    SPE_ERR("td_evt_gap      0x%x \n", ctrl->td_evt_gap);
    SPE_ERR("td_dma          %lx \n", (uintptr_t)ctrl->td_dma);

    SPE_ERR("udp_limit_time  0x%x \n", ctrl->udp_limit_time);
    SPE_ERR("udp_limit_cnt   0x%x \n", ctrl->udp_limit_cnt);
    SPE_ERR("rate_limit_time 0x%x \n", ctrl->rate_limit_time);
    SPE_ERR("rate_limit_byte 0x%x \n", ctrl->rate_limit_byte);
}

void spe_rd_result_print_bit0_19(struct spe_port_stat *stat)
{
    // bit0-1
    SPE_ERR("[rd_result]:rd_result_updata_only %u \n", stat->result.rd_result[RD_RESULT_UPDATA_ONLY]);
    SPE_ERR("[rd_result]:rd_result_discard %u \n", stat->result.rd_result[RD_RESULT_DISCARD]);
    SPE_ERR("[rd_result]:rd_result_success %u \n", stat->result.rd_result[RD_RESULT_SUCCESS]);
    SPE_ERR("[rd_result]:rd_result_wrap_or_length_wrong %u \n", stat->result.rd_result[RD_RESULT_WRAP_OR_LENGTH_WRONG]);

    // bit2-5
    SPE_ERR("[rd_pkt_drop_rsn]:rd_pkt_drop_rsn_undiscard %u \n", stat->result.rd_pkt_drop_rsn[RD_DROP_RSN_UNDISCARD]);
    SPE_ERR("[rd_pkt_drop_rsn]:rd_pkt_drop_rsn_rd_point_null %u \n",
            stat->result.rd_pkt_drop_rsn[RD_DROP_RSN_RD_POINT_NULL]);

    // rd bit 6-16
    SPE_ERR("[rd_pkt_fw_path]:rd_mac_filter %u \n", stat->result.rd_pkt_fw_path[RD_MAC_FILTER]);
    SPE_ERR("[rd_pkt_fw_path]:rd_eth_type %u \n", stat->result.rd_pkt_fw_path[RD_ETH_TYPE]);
    SPE_ERR("[rd_pkt_fw_path]:rd_1st_mac_fw %u \n", stat->result.rd_pkt_fw_path[RD_1ST_MAC_FW]);
    SPE_ERR("[rd_pkt_fw_path]:rd_ip_hdr_check %u \n", stat->result.rd_pkt_fw_path[RD_IP_HDR_CHECK]);
    SPE_ERR("[rd_pkt_fw_path]:rd_ip_filter %u \n", stat->result.rd_pkt_fw_path[RD_IP_FILTER]);
    SPE_ERR("[rd_pkt_fw_path]:rd_tcp_udp_check %u \n", stat->result.rd_pkt_fw_path[RD_TCP_UDP_CHECK]);
    SPE_ERR("[rd_pkt_fw_path]:rd_hash_check %u \n", stat->result.rd_pkt_fw_path[RD_HASH_CHECK]);
    SPE_ERR("[rd_pkt_fw_path]:rd_hash_cache_check %u \n", stat->result.rd_pkt_fw_path[RD_HASH_CACHE_CHECK]);
    SPE_ERR("[rd_pkt_fw_path]:rd_hash_ddr_check %u \n", stat->result.rd_pkt_fw_path[RD_HASH_DDR_CHECK]);
    SPE_ERR("[rd_pkt_fw_path]:rd_2nd_mac_check %u \n", stat->result.rd_pkt_fw_path[RD_2ND_MAC_CHECK]);
    SPE_ERR("[rd_pkt_fw_path]:rd_trans_complete %u \n", stat->result.rd_pkt_fw_path[RD_TRANS_COMPLETE]);

    // rd bit 17-19
    SPE_ERR("[rd_pkt_type]:rd_pkt_ipv4_tcp %u \n", stat->result.rd_pkt_type[RD_PKT_IPV4_TCP]);
    SPE_ERR("[rd_pkt_type]:rd_pkt_ipv4_udp %u \n", stat->result.rd_pkt_type[RD_PKT_IPV4_UDP]);
    SPE_ERR("[rd_pkt_type]:rd_pkt_ipv4_non_udp_tcp %u \n", stat->result.rd_pkt_type[RD_PKT_IPV4_NON_UDP_TCP]);
    SPE_ERR("[rd_pkt_type]:rd_pkt_ipv6_tcp %u \n", stat->result.rd_pkt_type[RD_PKT_IPV6_TCP]);
    SPE_ERR("[rd_pkt_type]:rd_pkt_ipv6_udp %u \n", stat->result.rd_pkt_type[RD_PKT_IPV6_UDP]);
    SPE_ERR("[rd_pkt_type]:rd_pkt_ipv6_non_udp_tcp %u \n", stat->result.rd_pkt_type[RD_PKT_IPV6_NON_UDP_TCP]);
    SPE_ERR("[rd_pkt_type]:rd_pkt_non_udp_tcp %u \n", stat->result.rd_pkt_type[RD_PKT_NON_UDP_TCP]);
    SPE_ERR("[rd_pkt_type]:rd_pkt_trans_before_ip_check %u \n", stat->result.rd_pkt_type[RD_PKT_TRANS_BEFORE_IP_CHECK]);
}

void spe_rd_result_print_bit20_26(struct spe_port_stat *stat)
{
    // bit20-22
    SPE_ERR("[spe_wraped_rsn]:rd_wrap_wrong_format %u \n", stat->result.rd_finsh_wrap_rsn[RD_WRAP_WRONG_FORMAT]);
    SPE_ERR("[spe_wraped_rsn]:rd_wrap_push_en %u \n", stat->result.rd_finsh_wrap_rsn[RD_WRAP_PUSH_EN]);
    SPE_ERR("[spe_wraped_rsn]:rd_wrap_num_oversize %u \n", stat->result.rd_finsh_wrap_rsn[RD_WRAP_NUM_OVERSIZE]);
    SPE_ERR("[spe_wraped_rsn]:rd_wrap_length_oversize %u \n", stat->result.rd_finsh_wrap_rsn[RD_WRAP_LENGTH_OVERSIZE]);
    SPE_ERR("[spe_wraped_rsn]:rd_wrap_timeout %u \n", stat->result.rd_finsh_wrap_rsn[RD_WRAP_TIMEOUT]);
    SPE_ERR("[spe_wraped_rsn]:rd_wrap_port_dis %u \n", stat->result.rd_finsh_wrap_rsn[RD_WRAP_PORT_DIS]);
    SPE_ERR("[spe_wraped_rsn]:rd_wrap_mutli_ncm %u \n", stat->result.rd_finsh_wrap_rsn[RD_WRAP_MUTLI_NCM]);

    // rd bit 23-26
    SPE_ERR("[spe_to_cpu_rsn]:rd_cpu_pkt_err %u \n", stat->result.rd_send_cpu_rsn[RD_CPU_PKT_ERR]);
    SPE_ERR("[spe_to_cpu_rsn]:rd_cpu_mac_non_stick_v4_v6 %u \n",
            stat->result.rd_send_cpu_rsn[RD_CPU_MAC_NON_STICK_V4_V6]);
    SPE_ERR("[spe_to_cpu_rsn]:rd_cpu_1stmac_ipver_err %u \n", stat->result.rd_send_cpu_rsn[RD_CPU_1STMAC_IPVER_ERR]);
    SPE_ERR("[spe_to_cpu_rsn]:rd_cpu_1stmac_dmac_smac_diff %u \n",
            stat->result.rd_send_cpu_rsn[RD_CPU_1STMAC_DMAC_SMAC_DIFF]);
    SPE_ERR("[spe_to_cpu_rsn]:rd_cpu_1stmac_mac_trans_fail %u \n",
            stat->result.rd_send_cpu_rsn[RD_CPU_1STMAC_MAC_TRANS_FAIL]);
    SPE_ERR("[spe_to_cpu_rsn]:rd_cpu_2sndmac_brg_dmac_fail %u \n",
            stat->result.rd_send_cpu_rsn[RD_CPU_2SNDMAC_BRG_DMAC_FAIL]);
    SPE_ERR("[spe_to_cpu_rsn]:rd_cpu_l3_check_l3_protocol_fail %u \n",
            stat->result.rd_send_cpu_rsn[RD_CPU_L3_CHECK_L3_PROTOCOL_FAIL]);
    SPE_ERR("[spe_to_cpu_rsn]:rd_cpu_l3_check_ipv4_hdr_fail %u \n",
            stat->result.rd_send_cpu_rsn[RD_CPU_L3_CHECK_IPV4_HDR_FAIL]);
    SPE_ERR("[spe_to_cpu_rsn]:rd_cpu_ipv6_hop_limit_1 %u \n", stat->result.rd_send_cpu_rsn[RD_CPU_IPV6_HOP_LIMIT_1]);
    SPE_ERR("[spe_to_cpu_rsn]:rd_cpu_ipv4_hdr_len_err %u \n", stat->result.rd_send_cpu_rsn[RD_CPU_IPV4_HDR_LEN_ERR]);
    SPE_ERR("[spe_to_cpu_rsn]:rd_cpu_ipv4_one_llt_left %u \n", stat->result.rd_send_cpu_rsn[RD_CPU_IPV4_ONE_TTL_LEFT]);
    SPE_ERR("[spe_to_cpu_rsn]:rd_cpu_ipv4_slice_pkt %u \n", stat->result.rd_send_cpu_rsn[RD_CPU_IPV4_SLICE_PKT]);
    SPE_ERR("[spe_to_cpu_rsn]:rd_cpu_l4_port_match %u \n", stat->result.rd_send_cpu_rsn[RD_CPU_L4_PORT_MATCH]);
    SPE_ERR("[spe_to_cpu_rsn]:rd_cpu_ip_table_mismatch %u \n", stat->result.rd_send_cpu_rsn[RD_CPU_IP_TABLE_MISMATCH]);
    SPE_ERR("[spe_to_cpu_rsn]:rd_cpu_fwsucc_or_discard %u \n", stat->result.rd_send_cpu_rsn[RD_CPU_FWSUCC_OR_DISCARD]);

    SPE_ERR("\n");
}

void spe_port_rd_info(unsigned int portno)
{
    unsigned int rd_hard_wptr;
    unsigned int rd_hard_rptr;
    struct spe_port_stat *stat = NULL;
    struct spe_port_ctrl *ctrl = NULL;

    if (PORTNO_INVALID(portno)) {
        SPE_ERR("port num is invalid!\n");
        return;
    }

    stat = &g_espe.ports[portno].stat;
    ctrl = &g_espe.ports[portno].ctrl;
    
    espe_get_port_type_dbg(ctrl);

    rd_hard_wptr = spe_readl(g_espe.regs, SPE_RDQX_WPTR(portno));
    rd_hard_rptr = spe_readl(g_espe.regs, SPE_RDQX_RPTR(portno));

    SPE_ERR("rd_busy         0x%x \n", ctrl->rd_busy);
    SPE_ERR("rd_free         0x%x \n", ctrl->rd_free);

    SPE_ERR("rd_hard_ptr wptr 0x%x \n", rd_hard_wptr);
    SPE_ERR("rd_hard_ptr rptr 0x%x \n\n", rd_hard_rptr);

    SPE_ERR("rd configed         %u \n", stat->rd_config);
    SPE_ERR("rd finish           %u \n", stat->rd_finished);
    SPE_ERR("rd finish event     %u \n", stat->rd_finsh_intr_complete);
    SPE_ERR("rd finish event cb  %u \n", stat->rd_finsh_intr_complete_called);
    SPE_ERR("rd finish pkt num     %u \n", stat->rd_finsh_pkt_num);

    SPE_ERR("rd drop             %u \n", stat->rd_droped);
    SPE_ERR("rd rd_sended             %u \n", stat->rd_sended);
    SPE_ERR("rd rd_maa_zero             %u \n", stat->rd_maa_zero);
    
    SPE_ERR("rd result success   %u \n\n", stat->result.rd_result[RD_RESULT_SUCCESS]);
}

void spe_rd_info_adv(unsigned int portno, unsigned int password)
{
    unsigned int i = portno;
    struct spe_port_stat *stat = &g_espe.ports[i].stat;
    struct spe_port_ctrl *ctrl = &g_espe.ports[i].ctrl;
    if (PORTNO_INVALID(portno)) {
        SPE_ERR("port num is invalid!\n");
        return;
    }
    espe_get_port_type_dbg(ctrl);

    spe_port_rd_info(portno);
    spe_rd_result_print_bit0_19(stat);
    spe_rd_result_print_bit20_26(stat);

    for (i = 0; i < SPE_PORT_NUM; i++) {
        SPE_ERR("rd from port %u count: %u \n", i, stat->result.rd_sport_cnt[i]);
    }

    SPE_ERR("rd per interval count    1: %u \n", stat->rd_num_per_interval[NUM_1]);
    SPE_ERR("rd per interval count  256: %u \n", stat->rd_num_per_interval[NUM_256]);
    SPE_ERR("rd per interval count  512: %u \n", stat->rd_num_per_interval[NUM_512]);
    SPE_ERR("rd per interval count  1024: %u \n", stat->rd_num_per_interval[NUM_1024]);
    SPE_ERR("rd per interval count  2048: %u \n", stat->rd_num_per_interval[NUM_2048]);
    SPE_ERR("rd per interval count larger: %u \n", stat->rd_num_per_interval[NUM_TOOLARGE]);
}

void spe_ipfw_setting(void)
{
    struct spe_ip_fw_ctx *ipfw_ctx = &g_espe.ipfw;

    SPE_ERR("free_cnt %u \n", ipfw_ctx->free_cnt);
    SPE_ERR("free_threhold %u \n", ipfw_ctx->free_threhold);
    SPE_ERR("deadtime %u \n", ipfw_ctx->deadtime);
    SPE_ERR("hash list base %lx \n", (uintptr_t)ipfw_ctx->hbucket);
    SPE_ERR("hash list size %u \n", ipfw_ctx->hlist_size);
    SPE_ERR("hash list width %u \n", ipfw_ctx->hitem_width);
    SPE_ERR("hash list zone %u \n", ipfw_ctx->hzone);
    SPE_ERR("hash list rand %u \n", ipfw_ctx->hrand);
}

void spe_port_info_all(unsigned int port_num)
{
    if (PORTNO_INVALID(port_num)) {
        SPE_ERR("port num is invalid!\n");
        return;
    }
    spe_port_setting(port_num);
    spe_port_rd_info(port_num);
    spe_port_td_info(port_num);
}

void spe_ad_info(void)
{
    struct spe_adq_ctrl *cur_ctrl = NULL;
    struct spe * spe = &g_espe;
    enum spe_adq_num adq_num;
    unsigned int maa_stub_wptr = 0;
    unsigned int espe_ad_wptr = 0;
    unsigned int espe_ad_rptr = 0;
    
    for (adq_num = SPE_ADQ0; adq_num < SPE_ADQ_BOTTOM; adq_num++) {
        cur_ctrl = &spe->adqs_ctx[adq_num].ctrl;

        if (spe->spe_version == ESPE_VER_5000) { //evide maa hard error
            maa_stub_wptr = readl(cur_ctrl->maa_wptr_stub_addr);
        }
        espe_ad_wptr = spe_readl(spe->regs, SPE_ADQ_WPTR(adq_num));
        espe_ad_rptr = spe_readl(spe->regs, SPE_ADQ_RPTR(adq_num));

        SPE_ERR("AD[%u] cur_ctrl->maa_wptr_stub_addr %u \n", adq_num, (u32)(uintptr_t)cur_ctrl->maa_wptr_stub_addr);
        SPE_ERR("AD[%u] maa_stub_rptr %u \n", adq_num, maa_stub_wptr);
        SPE_ERR("AD[%u] espe_ad_wptr %u \n", adq_num, espe_ad_wptr);
        SPE_ERR("AD[%u] espe_ad_rptr %u \n", adq_num, espe_ad_rptr);

    }

    
}


static void print_hex(unsigned int *base, unsigned int size)
{
    int i;
    unsigned int *x = NULL;
    for (x = base, i = 0; i < size; i++, x++) {
        if (0 == i % 4) {
            printk("\n0x%4.4x: ", i);
        }
        printk("%8.8x ", *x);
    }
    SPE_ERR("\n");
}

static inline void espe_pr_pkg_pm(unsigned char *buf, int len)
{
    int j, count;

    count = len > 64 ? 64 : len;
    for (j = 0; j < count; j += 16) {
        SPE_ERR("%03x: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n", j,
                    buf[j], buf[j + 1], buf[j + 2], buf[j + 3], buf[j + 4], buf[j + 5], buf[j + 6], buf[j + 7],
                    buf[j + 8], buf[j + 9], buf[j + 0xa], buf[j + 0xb], buf[j + 0xc], buf[j + 0xd], buf[j + 0xe],
                    buf[j + 0xf]);
    }
    SPE_ERR("\n");
}

void espe_print_pkt_info(unsigned char *data)
{
    struct iphdr *iph = (struct iphdr *)data;
    struct udphdr *udph = NULL;
    struct tcphdr *tcph = NULL;
    struct icmphdr *icmph = NULL;
    struct ipv6hdr *ip6h = NULL;

    SPE_ERR("[ESPE] wakeup, ip version=%d\n", iph->version);

    if (iph->version == 4) {
        SPE_ERR("[ESPE]src ip:%d.%d.%d.x, dst ip:%d.%d.%d.x\n", iph->saddr & 0xff, (iph->saddr >> 8) & 0xff,
               (iph->saddr >> 16) & 0xff, iph->daddr & 0xff, (iph->daddr >> 8) & 0xff, (iph->daddr >> 16) & 0xff);
        if (iph->protocol == IPPROTO_UDP) {
            udph = (struct udphdr *)(data + sizeof(struct iphdr));
            SPE_ERR("[ESPE]UDP packet, src port:%d, dst port:%d.\n", ntohs(udph->source), ntohs(udph->dest));
#ifdef CONFIG_HUAWEI_DUBAI
            dubai_log_packet_wakeup_stats("DUBAI_TAG_MODEM_PACKET_WAKEUP_UDP_V4", "port", ntohs(udph->dest));
#endif
        } else if (iph->protocol == IPPROTO_TCP) {
            tcph = (struct tcphdr *)(data + sizeof(struct iphdr));
            SPE_ERR("[ESPE]TCP packet, src port:%d, dst port:%d\n", ntohs(tcph->source), ntohs(tcph->dest));
#ifdef CONFIG_HUAWEI_DUBAI
            dubai_log_packet_wakeup_stats("DUBAI_TAG_MODEM_PACKET_WAKEUP_TCP_V4", "port", ntohs(tcph->dest));
#endif
        } else if (iph->protocol == IPPROTO_ICMP) {
            icmph = (struct icmphdr *)(data + sizeof(struct iphdr));
            SPE_ERR("[ESPE]ICMP packet, type(%d):%s, code:%d.\n", icmph->type,
                   ((icmph->type == 0) ? "ping reply" : ((icmph->type == 8) ? "ping request" : "other icmp pkt")),
                   icmph->code);
#ifdef CONFIG_HUAWEI_DUBAI
            dubai_log_packet_wakeup_stats("DUBAI_TAG_MODEM_PACKET_WAKEUP", "protocol", (int)iph->protocol);
#endif
        } else if (iph->protocol == IPPROTO_IGMP) {
            SPE_ERR("[ESPE]ICMP packet\n");
#ifdef CONFIG_HUAWEI_DUBAI
            dubai_log_packet_wakeup_stats("DUBAI_TAG_MODEM_PACKET_WAKEUP", "protocol", (int)iph->protocol);
#endif
        } else {
            SPE_ERR("[ESPE]Other IPV4 packet\n");
#ifdef CONFIG_HUAWEI_DUBAI
            dubai_log_packet_wakeup_stats("DUBAI_TAG_MODEM_PACKET_WAKEUP", "protocol", (int)iph->protocol);
#endif
        }
    } else if (iph->version == 6) {
        ip6h = (struct ipv6hdr *)data;
        SPE_ERR("[ESPE]version: %d, payload length: %d, nh->nexthdr: %d. \n", ip6h->version,
               ntohs(ip6h->payload_len), ip6h->nexthdr);
        SPE_ERR("[ESPE]ipv6 src addr:%04x:%x:%xx:x:x:x:x:x  \n", ntohs(ip6h->saddr.in6_u.u6_addr16[7]),
               ntohs(ip6h->saddr.in6_u.u6_addr16[6]), (ip6h->saddr.in6_u.u6_addr8[11]));
        SPE_ERR("[ESPE]ipv6 dst addr:%04x:%x:%xx:x:x:x:x:x \n", ntohs(ip6h->saddr.in6_u.u6_addr16[7]),
               ntohs(ip6h->saddr.in6_u.u6_addr16[6]), (ip6h->saddr.in6_u.u6_addr8[11]));
#ifdef CONFIG_HUAWEI_DUBAI
        dubai_log_packet_wakeup_stats("DUBAI_TAG_MODEM_PACKET_WAKEUP", "protocol", IPPROTO_IPV6);
#endif
    } else {
        espe_pr_pkg_pm(data, 40);
    }
}


void spe_dump_desc(unsigned port_num, unsigned int start_desc, unsigned int desc_num)
{
    unsigned int size;
    unsigned int *addr = NULL;
    struct spe *spe = &g_espe;

    addr = spe->ports[port_num].ctrl.td_addr;
    if (port_num == spe->ipfport.portno && spe->ipfport.extend_desc) {
        size = desc_num * sizeof(struct espe_ext_td_desc);
    } else {
        size = desc_num * sizeof(struct espe_td_desc);
    }
    print_hex(addr, size);
}

int bsp_espe_get_om_info(struct espe_om_info *info)
{
    struct spe *spe = &g_espe;
    int portno;
    int trans_size = 0;

    if (info == NULL) {
        return -EINVAL;
    }

    info->cpu.updonly_comp = spe->cpuport.cpu_updonly_comp;
    info->cpu.rd_num = spe->cpuport.cpu_rd_num;
    info->cpu.rd_udp_drop = spe->cpuport.cpu_rd_udp_drop;
    info->cpu.rd_wan = spe->cpuport.cpu_rd_to_wan;
    info->cpu.rd_wan_fail = spe->cpuport.cpu_rd_to_wan_fail;
    info->cpu.rd_nic = spe->cpuport.cpu_rd_to_nic;
    info->cpu.rd_nic_fail = spe->cpuport.cpu_rd_to_nic_fail;
    info->cpu.rd_netif = spe->cpuport.cpu_rd_to_netif_rx;
    info->cpu.rd_netif_succ = spe->cpuport.cpu_rd_to_netif_rx_succ;
    info->cpu.rd_netif_fail = spe->cpuport.cpu_rd_to_netif_rx_fail;

    trans_size += sizeof(struct espe_cpu_port_om_info);

    for (portno = 0; portno < SPE_PORT_NUM; portno++) {
        if (test_bit(portno, &spe->portmap[SPE_PORTS_NOMARL])) {
            info->port[portno].rd_finished = spe->ports[portno].stat.rd_finished;
            info->port[portno].rd_finished_bytes = spe->ports[portno].stat.rd_finished_bytes;
            info->port[portno].rd_finsh_pkt = spe->ports[portno].stat.rd_finsh_pkt_num;
            info->port[portno].rd_droped = spe->ports[portno].stat.rd_droped;
            info->port[portno].rd_sended = spe->ports[portno].stat.rd_sended;
            info->port[portno].rd_larger = spe->ports[portno].stat.rd_num_per_interval[NUM_TOOLARGE];
            info->port[portno].td_config = spe->ports[portno].stat.td_config;
            info->port[portno].td_full = spe->ports[portno].stat.td_full;
            info->port[portno].td_complete = spe->ports[portno].stat.td_desc_complete;
            info->port[portno].td_fail_drop = spe->ports[portno].stat.td_desc_fail_drop;
            info->port[portno].td_comp = spe->ports[portno].stat.td_pkt_complete;

            trans_size += sizeof(struct espe_port_om_info);
        }
    }
    return trans_size;
}

void espe_pkts_transreport(void *buf, unsigned int len)
{
}

void espe_one_td_pkt_transreport(struct spe *spe, struct espe_td_desc *desc)
{
}

void espe_one_rd_pkt_transreport(struct spe *spe, struct espe_rd_desc *desc)
{
}

MODULE_ALIAS("hisilicon network hardware accelerator driver");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("smart packet engine(spe) driver");
