/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2012-2019. All rights reserved.
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

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/dma-mapping.h>
#include <linux/static_key.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#if (defined(CONFIG_HUAWEI_BASTET) || defined(CONFIG_HW_DPIMARK_MODULE))
#include <net/inet_sock.h>
#include <linux/version.h>
#endif
#include "securec.h"
#include "ads_iface_handle.h"
#include "ads_iface_dpl.h"
#include "ads_iface_debug.h"


/*
 * ads_iface_check_ipv4_sync - check ipv4 tcp sync skb
 * @skb: sk buffer
 *
 * Return true: ipv4 tcp sync skb; false: not ipv4 tcp sync skb.
 */
 /*lint -save -e801*/
bool ads_iface_check_ipv4_sync(struct sk_buff *skb)
{
	struct iphdr *iph = NULL;
	struct tcphdr *tcph = NULL;

	if (unlikely(!pskb_may_pull(skb, sizeof(*iph))))
		goto out;

	iph = (struct iphdr *)skb->data;
	if (iph->ihl != 5 || iph->version != 4)
		goto out;

	if (iph->protocol == IPPROTO_TCP) {
		if (sizeof(*iph) + sizeof(*tcph) > skb->len)
			goto out;
		tcph = (struct tcphdr *)(skb->data + sizeof(*iph));
		if (tcph->syn)
			return true;
	}
out:
	return false;
}
/*lint -restore +e801*/

/*
 * ads_iface_check_ipv6_sync - check ipv6 tcp sync skb
 * @skb: sk buffer
 *
 * Return true: ipv6 tcp sync skb; false: not ipv6 tcp sync skb.
 */
 /*lint -save -e801*/
bool ads_iface_check_ipv6_sync(struct sk_buff *skb)
{
	struct ipv6hdr *ipv6h = NULL;
	struct tcphdr *tcph = NULL;
	uint32_t pkt_len;

	if (unlikely(!pskb_may_pull(skb, sizeof(*ipv6h))))
		goto out;

	ipv6h = (struct ipv6hdr *)skb->data;
	if (ipv6h->version != 6)
		goto out;

	pkt_len = ntohs(ipv6h->payload_len);
	if (ipv6h->nexthdr == IPPROTO_TCP && pkt_len >= sizeof(*tcph)) {
		if (pkt_len + sizeof(*ipv6h) > skb->len)
			goto out;
		tcph = (struct tcphdr *)(skb->data + sizeof(*ipv6h));
		if (tcph->syn)
			return true;
	}
out:
	return false;
}
/*lint -restore +e801*/

/*
 * ads_iface_get_skb_ps_info - Get ps info .
 * @skb: sk buffer
 *
 * Return pointer to ps information structure.
 */
STATIC struct ads_iface_ps_info_s *ads_iface_get_skb_ps_info(
			struct ads_iface_priv_s *priv, struct sk_buff *skb)
{
	struct ads_iface_ps_info_s *ps_info = NULL;

	switch (ntohs(skb->protocol)) {
	case ETH_P_IP:
		ps_info = &priv->ps_info_array[ADS_IPV4_ADDR];
		break;
	case ETH_P_IPV6:
		ps_info = &priv->ps_info_array[ADS_IPV6_ADDR];
		break;
	default:
		ps_info = &priv->ps_info_array[ADS_RAW_ADDR];
		break;
	}

	return ps_info;
}

/*
 * ads_iface_set_skb_protocol - Set protocol of sk buffer.
 * @priv: iface private info
 * @skb: sk buffer
 *
 * Return pointer to ps information structure.
 */
STATIC struct ads_iface_ps_info_s *ads_iface_set_skb_protocol(
			struct ads_iface_priv_s *priv,
			struct sk_buff *skb, struct rx_cb_map_s *map_info)
{
	struct ads_iface_ps_info_s *ps_info = NULL;

	if (priv->addr_family_mask & priv->ip_family_mask) {
		if (!map_info->ipf_result.bits.ip_type) {
			skb->protocol = htons(ETH_P_IP);/*lint !e778*/
			ps_info = &priv->ps_info_array[ADS_IPV4_ADDR];
		} else {
			skb->protocol = htons(ETH_P_IPV6);/*lint !e778*/
			ps_info = &priv->ps_info_array[ADS_IPV6_ADDR];
		}
	} else {
		skb->protocol = 0;
		ps_info = &priv->ps_info_array[ADS_RAW_ADDR];
	}

	return ps_info;
}

/*
 * ads_iface_build_user_field2 - Build userfield2 of espe BD.
 * @skb: sk buffer
 *
 * Return userfield2 value.
 */
STATIC uint32_t ads_iface_build_user_field2(struct sk_buff *skb)
{
#if (defined(CONFIG_HUAWEI_BASTET) || defined(CONFIG_HW_DPIMARK_MODULE))
	struct sock *sk = NULL;
#ifdef CONFIG_HW_DPIMARK_MODULE
	uint8_t *tmp = NULL;
#endif
	uint32_t value;

	if (unlikely(skb == NULL))
		return 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 23))
	sk = skb_to_full_sk(skb);
#else
	sk = skb->sk;
#endif
	if (sk != NULL) {
		value = 0;
#ifdef CONFIG_HUAWEI_BASTET
		value |= (sk->acc_state & 0x01);
		value |= (((uint32_t)sk->discard_duration << 8) & 0xFF00);
#endif
#ifdef CONFIG_HW_DPIMARK_MODULE
		tmp = (uint8_t *)&(sk->sk_hwdpi_mark);
		value |= tmp[0];
		value |= tmp[1];
#endif
		return value;
	}
#endif

	return 0;
}

/*
 * ads_iface_kfree_skb - Free skb base on flag..
 * @skb: sk buffer
 * @flag: maa own flag
 */
void ads_iface_kfree_skb(struct sk_buff *skb, uint32_t flag)
{
	dev_kfree_skb_any(skb);
}

/*
 * ads_iface_hold_wakelock - Hold wake lock for specific timeout
 * @lock: The structure of wakeup source and configuration.
 */
STATIC void ads_iface_hold_wakelock(struct ads_iface_wakelock_s *lock)
{
	if (likely(lock->timeout))
		__pm_wakeup_event(&lock->ws, lock->timeout);
}

/*
 * ads_iface_data_egress - Iface data egress handle function.
 * @priv: iface private info
 * @skb: sk buffer
 * @flag: flag of maa own skb
 *
 * Return 0 on success, negative on failure.
 */
/*lint -save -e801*/
int ads_iface_data_egress(struct ads_iface_priv_s *priv,
			  struct sk_buff *skb, uint32_t flag)
{
	struct ads_iface_context_s *iface_ctx = ADS_IFACE_CTX();
	struct ads_iface_ps_info_s *ps_info = NULL;
	struct wan_info_s *wan_info = NULL;
	unsigned int maa_flag = 0;

	priv->data_stats.tx_total_packets++;
	iface_ctx->dsflow_stats.tx_packets++;
	iface_ctx->dsflow_stats.tx_bytes += skb->len;

	if (!skb->len) {
		priv->data_stats.tx_length_errors++;
		goto out_free_drop;
	}

	ps_info = ads_iface_get_skb_ps_info(priv, skb);
	if (!test_bit(ps_info->addr_family, &priv->addr_family_mask)) {
		priv->data_stats.tx_state_errors++;
		goto out_free_drop;
	}

	wan_info = (struct wan_info_s *)skb->cb;

	if (!(flag & ADS_DATA_FLAG_MAA))
		wan_info->userfield2 = ads_iface_build_user_field2(skb);

	if (!(flag & ADS_DATA_FLAG_LTEV))
		wan_info->userfield1 = 0;

	wan_info->userfield0 = (wan_info->userfield2) ?
				priv->iface_id | 0x80000000 : priv->iface_id;

	memcpy_s(&wan_info->info, sizeof(struct ipf_info_s),
		 &ps_info->ipf_info, sizeof(struct ipf_info_s));

	if (ps_info->check_tcp_sync != NULL)
		wan_info->info.parse_en = ps_info->check_tcp_sync(skb) ? 0 : 1;

	ads_iface_dpl_tx_ip_pkt_info_rec(iface_ctx, priv, skb);

	/**
	 * skb->data: point to mac header
	 * skb->mac_len: ETH_HLEN
	 */
	skb_push(skb, ETH_HLEN);
	skb->mac_len = ETH_HLEN;

	if (flag & ADS_DATA_FLAG_MAA)
		maa_flag |= WAN_MAA_OWN;
	else
		((struct ethhdr *)skb->data)->h_proto = skb->protocol;

	iface_ctx->espe_stats.tx_total_packets++;

	if (mdrv_wan_tx(skb, maa_flag)) {
		iface_ctx->espe_stats.tx_dropped++;
		iface_ctx->espe_stats.tx_wan_errors++;
	} else {
		iface_ctx->espe_stats.tx_packets++;
	}

	priv->data_stats.tx_packets++;
	return 0;

out_free_drop:
	ads_iface_kfree_skb(skb, flag);
	priv->data_stats.tx_dropped++;
	return -EFAULT;
}
/*lint -restore +e801*/

/*
 * ads_iface_data_ingress - Iface data ingress handle function.
 * @priv: iface private info
 * @skb: sk buffer
 *
 * Return 0 on success, negative on failure.
 */
/*lint -save -e801*/
STATIC int ads_iface_data_ingress(struct ads_iface_priv_s *priv,
				  struct sk_buff *skb)
{
	struct ads_iface_context_s *iface_ctx = ADS_IFACE_CTX();
	struct rx_cb_map_s *map_info = (struct rx_cb_map_s *)skb->cb;
	struct ads_iface_ps_info_s *ps_info = NULL;
	ads_iface_rx_func_t rx_func;

	priv->data_stats.rx_total_packets++;
	iface_ctx->dsflow_stats.rx_packets++;
	iface_ctx->dsflow_stats.rx_bytes += skb->len;

	ps_info = ads_iface_set_skb_protocol(priv, skb, map_info);
	if (!test_bit(ps_info->addr_family, &priv->addr_family_mask)) {
		priv->data_stats.rx_state_errors++;
		goto out_free_drop;
	}

	rx_func = priv->rx_func;
	if (rx_func) {
		priv->rx_cmplt_mark = true;
		if (!priv->rx_dma_support)
			dma_unmap_single(iface_ctx->dev,
					 virt_to_phys(skb->data - ETH_HLEN),
					 skb->len + ETH_HLEN, DMA_FROM_DEVICE);
		rx_func(priv->user_data, skb);
	} else {
		priv->data_stats.rx_carrier_errors++;
		goto out_free_drop;
	}

	priv->data_stats.rx_packets++;
	return 0;

out_free_drop:
	ads_iface_kfree_skb(skb, 0);
	priv->data_stats.rx_dropped++;
	return -EFAULT;
}
/*lint -restore +e801*/

/*
 * ads_iface_data_ingress - Iface special data handle function.
 * @iface_ctx: iface context
 * @skb: sk buffer
 *
 * Return 0 on success, negative on failure.
 */
STATIC int ads_iface_special_data_handle(struct ads_iface_context_s *iface_ctx,
					 struct sk_buff *skb)
{
	if (iface_ctx->hook_ops.special_data_hook) {
		dma_unmap_single(iface_ctx->dev,
				 virt_to_phys(skb->data - ETH_HLEN),
				 skb->len + ETH_HLEN, DMA_FROM_DEVICE);
		iface_ctx->hook_ops.special_data_hook(skb);
		return 0;
	}

	ADS_LOGE("special_data_hook is null.");
	ads_iface_kfree_skb(skb, 0);
	return -EFAULT;
}

/*
 * ads_iface_espe_rx_complete - Rx complete handle function.
 */
void ads_iface_espe_rx_complete(void)
{
	struct ads_iface_context_s *iface_ctx = ADS_IFACE_CTX();
	struct ads_iface_priv_s *priv = NULL;
	ads_iface_rx_cmplt_func_t rx_cmplt_func;
	uint8_t iface_id;

	for (iface_id = 0; iface_id < PS_IFACE_ID_BUTT; iface_id++) {
		priv = ads_iface_priv(iface_id);
		if (priv->rx_cmplt_mark) {
			priv->rx_cmplt_mark = false;
			rx_cmplt_func = priv->rx_cmplt_func;
			if (rx_cmplt_func)
				rx_cmplt_func(priv->user_data);
		}
	}

	ads_iface_dpl_report_rx_pkt_rec(iface_ctx);

	ads_iface_hold_wakelock(&iface_ctx->lock);
}

/*
 * ads_iface_espe_rx_handle - Rx handle function.
 * @skb: sk buffer
 *
 * Return 0 on success, negative on failure.
 */
/*lint -save -e801*/
int ads_iface_espe_rx_handle(struct sk_buff *skb)
{
	struct ads_iface_context_s *iface_ctx = ADS_IFACE_CTX();
	struct ads_iface_priv_s *priv = NULL;
	struct rx_cb_map_s *map_info = (struct rx_cb_map_s *)skb->cb;

	iface_ctx->espe_stats.rx_total_packets++;

	ads_iface_dpl_rx_ip_pkt_info_rec(iface_ctx, skb);

	if (unlikely(map_info->ipf_result.bits.dl_special_flag)) {
		ads_iface_special_data_handle(iface_ctx, skb);
		goto out_succ;
	}

	if (map_info->ipf_result.u32 & ADS_IFACE_IPF_FILTER_ERR_MASK)
		iface_ctx->espe_stats.rx_filter_errors++;

	if (iface_ctx->rx_discard_flag) {
		iface_ctx->espe_stats.rx_discard_errors++;
		goto out_drop_free;
	}

	priv = ads_iface_priv((uint8_t)map_info->userfield0);
	if (priv == NULL) {
		iface_ctx->espe_stats.rx_iface_errors++;
		goto out_drop_free;
	}

	ads_iface_data_ingress(priv, skb);

out_succ:
	iface_ctx->espe_stats.rx_packets++;
	return 0;

out_drop_free:
	ads_iface_kfree_skb(skb, 0);
	iface_ctx->espe_stats.rx_dropped++;
	return -EFAULT;
}
/*lint -restore +e801*/

/*
 * ads_iface_espe_lev_report - Water level handle function of epse bd fifo.
 * @level: water levle
 */
int ads_iface_espe_lev_report(enum wan_waterlevel_e level)
{
	return 0;
}

