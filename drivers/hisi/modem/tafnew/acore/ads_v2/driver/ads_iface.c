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
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include "securec.h"
#include "ads_iface.h"
#include "ads_iface_handle.h"
#include "ads_iface_debug.h"



struct ads_iface_context_s ads_iface_context;

/*
 * ads_iface_platform_probe - Driver probe function.
 * @pdev: platform device
 *
 * Return 0 on success, negative on failure.
 */
STATIC int ads_iface_platform_probe(struct platform_device *pdev)
{
	struct ads_iface_context_s *iface_ctx = ADS_IFACE_CTX();

	dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));//lint !e506 !e648 !e778 !e598
	iface_ctx->dev = &pdev->dev;

	return 0;
}

/*
 * ads_iface_platform_remove - Driver remove function.
 * @pdev: platform device
 *
 * Return 0 on success, negative on failure.
 */
STATIC int ads_iface_platform_remove(struct platform_device *pdev)
{
	return 0;
}

STATIC const struct of_device_id ads_iface_match_table[] = {
	{.compatible = "hisilicon,hisi-ads", },
	{ },
};

STATIC struct platform_driver ads_iface_platform_driver = {
	.probe  = ads_iface_platform_probe,
	.remove = ads_iface_platform_remove,
	.driver = {
		.name = "hisi-ads",
		.of_match_table = of_match_ptr(ads_iface_match_table),
	},
};

/*
 * ads_iface_wakelock_init - Initialize wake lock for data transfer.
 * @lock: The structure of wakeup source and configuration.
 *
 * The lock is held after espe-rd-irq exit.
 */
STATIC void ads_iface_wakelock_init(struct ads_iface_wakelock_s *lock)
{
	lock->timeout = 0;
	wakeup_source_init(&lock->ws, "espe_wake");
}

/*
 * ads_iface_espe_init - Initialize epse for data transfer.
 *
 * Register espe ops fro wan device.
 *
 * Returns: on success, negative on failure
 */
STATIC int ads_iface_espe_init(void)
{
	struct wan_callback_s wan_ops;

	wan_ops.wan_rx = ads_iface_espe_rx_handle;
	wan_ops.lev_report = ads_iface_espe_lev_report;
	wan_ops.rx_complete = ads_iface_espe_rx_complete;

	return mdrv_wan_callback_register(&wan_ops);
}

/*
 * ads_iface_priv - Get iface private info.
 * @iface_id: iface id
 *
 * Return pointer to iface private info.
 */
struct ads_iface_priv_s *ads_iface_priv(uint8_t iface_id)
{
	if (iface_id >= PS_IFACE_ID_BUTT)
		return NULL;

	return &ads_iface_context.priv_array[iface_id];
}

/*
 * ads_iface_create - Initialize iface context.
 *
 * Return 0 on success, negative on failure.
 */
/*lint -save -e801*/
int ads_iface_create(void)
{
	struct ads_iface_context_s *iface_ctx = NULL;
	struct ads_iface_priv_s *priv = NULL;
	int rc = 0;
	uint8_t iface_id;

	iface_ctx = ADS_IFACE_CTX();
	memset_s(iface_ctx, sizeof(*iface_ctx), 0, sizeof(*iface_ctx));

	for (iface_id = 0; iface_id < PS_IFACE_ID_BUTT; iface_id++) {
		priv = ads_iface_priv(iface_id);
		priv->iface_id = iface_id;
		priv->trans_mode = ADS_TRANS_MODE_BUTT;
		set_bit(ADS_IPV4_ADDR, &priv->ip_family_mask);
		set_bit(ADS_IPV6_ADDR, &priv->ip_family_mask);

		priv->ps_info_array[ADS_IPV4_ADDR].addr_family = ADS_IPV4_ADDR;
		priv->ps_info_array[ADS_IPV6_ADDR].addr_family = ADS_IPV6_ADDR;
		priv->ps_info_array[ADS_RAW_ADDR].addr_family = ADS_RAW_ADDR;

		priv->ps_info_array[ADS_IPV4_ADDR].check_tcp_sync
						= ads_iface_check_ipv4_sync;
		priv->ps_info_array[ADS_IPV6_ADDR].check_tcp_sync
						= ads_iface_check_ipv6_sync;
		priv->ps_info_array[ADS_RAW_ADDR].check_tcp_sync = NULL;
	}

	ads_iface_wakelock_init(&iface_ctx->lock);

	rc = ads_iface_espe_init();
	if (rc) {
		ADS_LOGE("register espe ops failed.");
		goto out_err;
	}

	rc = platform_driver_register(&ads_iface_platform_driver);
	if (rc) {
		ADS_LOGE("register platform driver failed.");
		goto out_err;
	}

out_err:
	return rc;
}
/*lint -restore +e801*/

/*
 * ads_iface_bring_up - Bring up ps iface.
 * @iface_id: iface id
 * @config: iface configuration
 *
 * Return 0 on success, negative on failure.
 */
int ads_iface_bring_up(uint8_t iface_id, struct ads_iface_config_s *config)
{
	struct ads_iface_priv_s *priv = ads_iface_priv(iface_id);
	struct ads_iface_ps_info_s *ps_info = NULL;

	if (unlikely(priv == NULL)) {
		ADS_LOGE("iface not found: %d.", iface_id);
		return -ENODEV;
	}

	if (!ADS_ADDR_FAMILY_VALID(config->addr_family)) {
		ADS_LOGE("addr family invalid: %d.", config->addr_family);
		return -EINVAL;
	}

	ps_info = &priv->ps_info_array[config->addr_family];
	ps_info->ipf_info.pdu_session_id = config->pdu_session_id;
	ps_info->ipf_info.fc_head = config->fc_head;
	ps_info->ipf_info.modem_id = config->modem_id;
	ps_info->ipf_info.parse_en = (ADS_RAW_ADDR == config->addr_family) ? 0 : 1;

	set_bit(config->addr_family, &priv->addr_family_mask);

	return 0;
}

/*
 * ads_iface_bring_down - Bring down ps iface.
 * @iface_id: iface id
 * @config: iface configuration
 *
 * Return 0 on success, negative on failure.
 */
int ads_iface_bring_down(uint8_t iface_id, struct ads_iface_config_s *config)
{
	struct ads_iface_priv_s *priv = ads_iface_priv(iface_id);
	struct ads_iface_ps_info_s *ps_info = NULL;

	if (unlikely(priv == NULL)) {
		ADS_LOGE("iface not found: %d.", iface_id);
		return -ENODEV;
	}

	if (!ADS_ADDR_FAMILY_VALID(config->addr_family)) {
		ADS_LOGE("addr family invalid: %d.", config->addr_family);
		return -EINVAL;
	}

	ps_info = &priv->ps_info_array[config->addr_family];
	ps_info->ipf_info.pdu_session_id = config->pdu_session_id;
	ps_info->ipf_info.fc_head = config->fc_head;
	ps_info->ipf_info.modem_id = config->modem_id;
	ps_info->ipf_info.parse_en = 0;

	clear_bit(config->addr_family, &priv->addr_family_mask);

	if (priv->addr_family_mask == 0) {
		priv->rx_cmplt_func = NULL;
		priv->rx_func = NULL;
		priv->user_data = 0;
	}

	return 0;
}

/*
 * ads_iface_tx_extern - Transmit packet from ps iface.
 * @iface_id: iface id
 * @skb: sk buffer
 * @flag: maa own flag
 *
 * Return 0 on success, negative on failure.
 */
STATIC int ads_iface_tx_extern(uint8_t iface_id,
			       struct sk_buff *skb, uint32_t flag)
{
	struct ads_iface_priv_s *priv = ads_iface_priv(iface_id);

	if (unlikely(priv == NULL)) {
		ADS_LOGE("iface not found: %d.", iface_id);
		ads_iface_kfree_skb(skb, flag);
		return -ENODEV;
	}

	return ads_iface_data_egress(priv, skb, flag);
}

/*
 * ads_iface_tx - Transmit sys own packet from ps iface.
 * @iface_id: iface id
 * @skb: sk buffer
 *
 * Return 0 on success, negative on failure.
 */
int ads_iface_tx(uint8_t iface_id, struct sk_buff *skb)
{
	return ads_iface_tx_extern(iface_id, skb, 0);
}


#if (FEATURE_PC5_DATA_CHANNEL == FEATURE_ON)
/*
 * ads_iface_tx_ltev - Transmit ltev packet from ps iface.
 * @iface_id: iface id
 * @skb: sk buffer
 *
 * Return 0 on success, negative on failure.
 */
int ads_iface_tx_ltev(uint8_t iface_id, struct sk_buff *skb)
{
	return ads_iface_tx_extern(iface_id, skb, ADS_DATA_FLAG_LTEV);
}
#endif

/*
 * ads_iface_register_rx_handler - Register ps iface data rx handler.
 * @iface_id: iface id
 * @rx_handler: rx callback
 *
 * Return 0 on success, negative on failure.
 */
int ads_iface_register_rx_handler(uint8_t iface_id,
				  struct ads_iface_rx_handler_s *rx_handler)
{
	struct ads_iface_priv_s *priv = ads_iface_priv(iface_id);

	if (unlikely(priv == NULL)) {
		ADS_LOGE("iface not found: %d.", iface_id);
		return -ENODEV;
	}

	if (unlikely(rx_handler == NULL)) {
		ADS_LOGE("rx_handler is null.");
		return -EINVAL;
	}

	priv->rx_dma_support = rx_handler->rx_dma_support;
	priv->user_data = rx_handler->user_data;
	priv->rx_func = rx_handler->rx_func;
	priv->rx_cmplt_func = rx_handler->rx_cmplt_func;

	return 0;
}

/*
 * ads_iface_deregister_rx_handler - Register ps iface data rx handler.
 * @iface_id: iface id
 *
 * Return 0 on success, negative on failure.
 */
int ads_iface_deregister_rx_handler(uint8_t iface_id)
{
	struct ads_iface_priv_s *priv = ads_iface_priv(iface_id);

	if (unlikely(priv == NULL)) {
		ADS_LOGE("iface not found: %d.", iface_id);
		return -ENODEV;
	}

	priv->rx_cmplt_func = NULL;
	priv->rx_func = NULL;
	priv->user_data = 0;
	priv->rx_dma_support = false;

	return 0;
}

/*
 * ads_iface_enable_wakelock - Enable wake lock.
 *
 * Config timeout for wake lock.
 *
 * Returns: on success, negative on failure
 */
int ads_iface_enable_wakelock(uint32_t timeout)
{
	struct ads_iface_context_s *iface_ctx = ADS_IFACE_CTX();

	ADS_LOGH("enable wakelock %d msec.", timeout);

	if (timeout)
		iface_ctx->lock.timeout = timeout;

	return 0;
}

/*
 * ads_iface_register_hook - Register hook callback.
 * @hook_ops: hook operations
 *
 * Return 0 on success, negative on failure.
 */
int ads_iface_register_hook(struct ads_iface_hook_ops_s *hook_ops)
{
	struct ads_iface_context_s *iface_ctx = ADS_IFACE_CTX();

	iface_ctx->hook_ops = *hook_ops;

	return 0;
}

/*
 * ads_iface_get_data_stats - Get data stats of ps iface.
 * @iface_id: iface id
 *
 * Return pointer to data stats structure.
 */
struct ads_data_stats_s *ads_iface_get_data_stats(uint8_t iface_id)
{
	struct ads_iface_priv_s *priv = ads_iface_priv(iface_id);

	return &priv->data_stats;
}

/*
 * ads_iface_get_espe_stats - Get data stats of epse.
 *
 * Return pointer to espe stats structure.
 */
struct ads_espe_stats_s *ads_iface_get_espe_stats(void)
{
	struct ads_iface_context_s *iface_ctx = ADS_IFACE_CTX();

	return &iface_ctx->espe_stats;
}

/*
 * ads_iface_get_dsflow_stats - Get data flow stats.
 *
 * Return pointer to dsflow stats structure.
 */
struct ads_dsflow_stats_s *ads_iface_get_dsflow_stats(void)
{
	struct ads_iface_context_s *iface_ctx = ADS_IFACE_CTX();

	return &iface_ctx->dsflow_stats;
}

/*
 * ads_iface_get_dsflow_stats - Clear data flow stats.
 */
void ads_iface_clear_dsflow_stats(void)
{
	struct ads_iface_context_s *iface_ctx = ADS_IFACE_CTX();

	iface_ctx->dsflow_stats.rx_bytes = 0;
	iface_ctx->dsflow_stats.tx_bytes = 0;
}

/*
 * ads_iface_set_rx_discard_flag - Set rx discard data flag.
 * @rx_discard_flag: rx discard data flag
 */
void ads_iface_set_rx_discard_flag(uint32_t rx_discard_flag)
{
	struct ads_iface_context_s *iface_ctx = ADS_IFACE_CTX();

	iface_ctx->rx_discard_flag = rx_discard_flag;

	ADS_LOGH("rx_discard_flag is %d", iface_ctx->rx_discard_flag);
}

