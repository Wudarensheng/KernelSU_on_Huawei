/*
 * hisi_frame_rate_ctrl.c
 *
 * The driver of dynamic frame rate
 *
 * Copyright (c) 2019-2019 Huawei Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */
#include "hisi_frame_rate_ctrl.h"
#include "hisi_mipi_dsi.h"
#ifdef CONFIG_HUAWEI_DUBAI
#include <huawei_platform/log/hwlog_kernel.h>
#endif

#define PORCH_RATIO_UPPER_LIMIT (90)
#define PORCH_RATIO_LOWER_LIMIT (10)

/* notify hwc to change core clk */
static void hwc_uevent_frame_update_notify(struct hisi_fb_data_type *hisifd,
	int frm_rate, uint32_t porch_ratio, uint32_t notify_type)
{
	char *envp[2];
	char buf[64] = {0};

	uint32_t value = notify_type << 16 | porch_ratio << 8 | (uint32_t)frm_rate;
	snprintf(buf, sizeof(buf) - 1, "dfrNotify=%u", value);

	envp[0] = buf;
	envp[1] = NULL;

	kobject_uevent_env(&(hisifd->fbi->dev->kobj), KOBJ_CHANGE, envp);

	HISI_FB_INFO("fb%d, %s, %d, %u, %u\n", hisifd->index, buf, frm_rate, porch_ratio, notify_type);
}

void hisi_dfr_notice_handle_func(struct work_struct *work)
{
	struct hisi_fb_data_type *hisifd = NULL;
	struct frame_rate_ctrl *frm_rate_ctrl = NULL;

	hisifd = container_of(work, struct hisi_fb_data_type, dfr_notice_work);
	if (hisifd == NULL) {
		HISI_FB_ERR("hisifd is NULL point!\n");
		return;
	}

	frm_rate_ctrl = &(hisifd->panel_info.frm_rate_ctrl);

	hwc_uevent_frame_update_notify(hisifd, frm_rate_ctrl->target_frame_rate,
		frm_rate_ctrl->porch_ratio, frm_rate_ctrl->notify_type);
}

static void hisi_dfr_end(struct hisi_fb_data_type *hisifd)
{
	unsigned long flags = 0;
	struct frame_rate_ctrl *frm_rate_ctrl = &(hisifd->panel_info.frm_rate_ctrl);

	spin_lock_irqsave(&hisifd->mipi_resource_lock, flags);
	hisifd->panel_info.fps = frm_rate_ctrl->target_frame_rate;
	frm_rate_ctrl->current_frame_rate = frm_rate_ctrl->target_frame_rate;
	frm_rate_ctrl->current_dsi_bit_clk = frm_rate_ctrl->target_dsi_bit_clk;

	frm_rate_ctrl->status = FRM_UPDT_DONE;
	spin_unlock_irqrestore(&hisifd->mipi_resource_lock, flags);
}

static void hisi_dfr_delay_work(struct work_struct *work)
{
	uint32_t delay_range = 200; // us
	struct hisi_fb_data_type *hisifd = NULL;

	hisifd = container_of(work, struct hisi_fb_data_type, dfr_delay_work);
	if (hisifd == NULL) {
		HISI_FB_ERR("hisifd is NULL point!\n");
		return;
	}

	usleep_range(hisifd->panel_info.dfr_delay_time, hisifd->panel_info.dfr_delay_time + delay_range);

	hisi_dfr_end(hisifd);

	HISI_FB_INFO("delay %d us to release dfr status", hisifd->panel_info.dfr_delay_time);
}

struct mipi_panel_info *get_mipi_ctrl(struct hisi_fb_data_type *hisifd)
{
	struct mipi_panel_info *mipi = NULL;

	if (hisifd->panel_info.frm_rate_ctrl.target_frame_rate == FPS_60HZ) {
		mipi = &(hisifd->panel_info.mipi);
		HISI_FB_DEBUG("get pinfo->mipi\n");
	} else {
		mipi = &(hisifd->panel_info.mipi_updt);
		HISI_FB_DEBUG("get pinfo->mipi_updt\n");
	}

	return mipi;
}

static uint32_t get_compression_ratio(struct hisi_fb_data_type *hisifd)
{
	uint32_t ifbc_type = hisifd->panel_info.ifbc_type;
	int mipi_idx = is_dual_mipi_panel(hisifd) ? 1 : 0;

	if (ifbc_type >= IFBC_TYPE_MAX) {
		HISI_FB_ERR("ifbc_type is invalid");
		return 0;
	}

	return g_mipi_ifbc_division[mipi_idx][ifbc_type].xres_div;
}

static uint32_t get_porch_ratio(struct hisi_fb_data_type *hisifd)
{
	uint32_t porch_ratio = 10; // default porch ratio 10%
	uint32_t cmp_ratio;
	uint32_t active_dpi_hsize;
	uint32_t bpp = 24;
	uint32_t real_area;
	uint32_t total_area;
	struct mipi_panel_info *mipi = get_mipi_ctrl(hisifd);

	if (hisifd->panel_info.frm_rate_ctrl.target_frame_rate == FPS_60HZ &&
		(hisifd->panel_info.dfr_method == DFR_METHOD_HPORCH))
		return porch_ratio;

	cmp_ratio = get_compression_ratio(hisifd);
	if (cmp_ratio == 0)
		return porch_ratio;

	/* (xres/dsc*bpp/8+6)/lane_num, 8 - byte/bit, 6 - headerSize */
	active_dpi_hsize = (hisifd->panel_info.xres / cmp_ratio * bpp / 8 + 6) /
		(hisifd->panel_info.mipi.lane_nums + 1);

	real_area = active_dpi_hsize * hisifd->panel_info.yres;
	total_area = mipi->hline_time * (mipi->vsa + mipi->vbp + mipi->vfp + hisifd->panel_info.yres);
	porch_ratio = (total_area - real_area) * 100 / total_area; // %

	if (porch_ratio < PORCH_RATIO_LOWER_LIMIT || porch_ratio > PORCH_RATIO_UPPER_LIMIT)
		HISI_FB_WARNING("porch_ratio %u could be wrong");

	HISI_FB_INFO("c_ratio=%u, dpiSize=%u, result%u", cmp_ratio, active_dpi_hsize, porch_ratio);
	return porch_ratio;
}

static void get_dsi_phy_ctrl(struct hisi_fb_data_type *hisifd,
	struct mipi_dsi_phy_ctrl *phy_ctrl)
{
	if (hisifd->panel_info.mipi.phy_mode == DPHY_MODE)
		get_dsi_dphy_ctrl(hisifd, phy_ctrl);
	else
		get_dsi_cphy_ctrl(hisifd, phy_ctrl);
}

static bool need_skip_dirty_region_frame(struct hisi_fb_data_type *hisifd)
{
	uint32_t vrt_lines;
	uint32_t vactive_lines;

	vrt_lines = inp32(hisifd->mipi_dsi0_base + MIPI_LDI_VRT_CTRL2) & 0x1FFF;
	vactive_lines = inp32(hisifd->mipi_dsi0_base + MIPIDSI_VID_VACTIVE_LINES_OFFSET);

	if (vrt_lines < (hisifd->panel_info.yres - 1) || vactive_lines < hisifd->panel_info.yres) {
		HISI_FB_INFO("skip, vrt_lines = 0x%x, vactive_lines = 0x%x, yres = 0x%x",
			vrt_lines, vactive_lines, hisifd->panel_info.yres);
		return true;
	}


	return false;
}

static bool need_send_dfr_cmds_in_vactive(struct hisi_fb_data_type *hisifd)
{
	bool ret = false;

	if (is_mipi_cmd_panel(hisifd) &&
		hisifd->panel_info.send_dfr_cmd_in_vactive &&
		hisifd->panel_info.frm_rate_ctrl.target_frame_rate == FPS_60HZ)
		ret = true;

	return ret;
}

void dfr_status_convert_on_isr_vstart(struct hisi_fb_data_type *hisifd)
{
	struct frame_rate_ctrl *frm_rate_ctrl = NULL;

	if (hisifd == NULL ||
		hisifd->index != PRIMARY_PANEL_IDX ||
		!hisifd->panel_info.dfr_support)
		return;

	frm_rate_ctrl = &(hisifd->panel_info.frm_rate_ctrl);

	if (frm_rate_ctrl->status == FRM_UPDT_CONFIG_DONE) {
		if (need_skip_dirty_region_frame(hisifd))
			return;

		frm_rate_ctrl->status = FRM_UPDT_NEED_DOING;

		if (need_send_dfr_cmds_in_vactive(hisifd))
			hisifd->fps_upt_isr_handler(hisifd); /* sending fps change cmds to ddic */

	}
}

bool need_update_frame_rate(struct hisi_fb_data_type *hisifd)
{
	if (hisifd == NULL)
		return false;

	if (hisifd->panel_info.dfr_support &&
		hisifd->panel_info.fps_updt != hisifd->panel_info.fps &&
		hisifd->panel_info.frm_rate_ctrl.status == FRM_UPDT_DONE) {
		HISI_FB_INFO("fps_updt %d, fps %d", hisifd->panel_info.fps_updt, hisifd->panel_info.fps);
		return true;
	}
	return false;
}

bool need_config_frame_rate_timing(struct hisi_fb_data_type *hisifd)
{
	if (hisifd == NULL)
		return false;

	if (hisifd->panel_info.dfr_support &&
		hisifd->panel_info.frm_rate_ctrl.status == FRM_UPDT_NEED_DOING) {
		HISI_FB_INFO("status = %d", hisifd->panel_info.frm_rate_ctrl.status);
		return true;
	}
	return false;
}

static void frm_rate_update_dsi_timing_config(struct hisi_fb_data_type *hisifd)
{
	struct mipi_panel_info *mipi = NULL;
	struct frame_rate_ctrl *frm_rate_ctrl = NULL;
	struct mipi_dsi_phy_ctrl *dsi_phy_ctrl = NULL;

	frm_rate_ctrl = &(hisifd->panel_info.frm_rate_ctrl);
	dsi_phy_ctrl = &(hisifd->panel_info.dsi_phy_ctrl);
	if (frm_rate_ctrl == NULL) {
		HISI_FB_ERR("frm_rate_ctrl is NULL\n");
		return;
	}
	mipi = get_mipi_ctrl(hisifd);

	frm_rate_ctrl->timing.dpi_hsize = mipi->dpi_hsize;
	frm_rate_ctrl->timing.hsa = mipi->hsa;
	frm_rate_ctrl->timing.hbp = mipi->hbp;
	frm_rate_ctrl->timing.hline_time = mipi->hline_time;
	frm_rate_ctrl->timing.width = mipi->width;

	frm_rate_ctrl->timing.vsa = mipi->vsa;
	frm_rate_ctrl->timing.vbp = mipi->vbp;
	frm_rate_ctrl->timing.vfp = mipi->vfp;
	frm_rate_ctrl->timing.vactive_line = mipi->vactive_line;

	frm_rate_ctrl->target_dsi_bit_clk = mipi->dsi_bit_clk_upt;

	HISI_FB_INFO("get paramters:dpi_hsize = %d,hsa = %d,hbp = %d,hline_time = %d,"\
		"vsa = %d,vbp = %d,vfp = %d,vactive_line = %d,target_dsi_bit_clk = %d",\
		mipi->dpi_hsize, mipi->hsa, mipi->hbp, mipi->hline_time,\
		mipi->vsa, mipi->vbp, mipi->vfp, mipi->vactive_line, mipi->dsi_bit_clk_upt);
}

static int mipi_dsi_frm_rate_para_config(struct hisi_fb_data_type *hisifd)
{
	struct frame_rate_ctrl *frm_rate_ctrl = NULL;

	frm_rate_ctrl = &(hisifd->panel_info.frm_rate_ctrl);

	frm_rate_update_dsi_timing_config(hisifd);

	HISI_FB_INFO("current_dsi_bit_clk = %d, target_dsi_bit_clk = %d\n", \
		frm_rate_ctrl->current_dsi_bit_clk, frm_rate_ctrl->target_dsi_bit_clk);

	if (frm_rate_ctrl->current_dsi_bit_clk != frm_rate_ctrl->target_dsi_bit_clk) {
		memset(&(frm_rate_ctrl->phy_ctrl), 0, sizeof(frm_rate_ctrl->phy_ctrl));
		get_dsi_phy_ctrl(hisifd, &(frm_rate_ctrl->phy_ctrl));
	}
	return 0;
}

static void frm_update_set_dsi_reg(struct hisi_fb_data_type *hisifd,
	char __iomem *mipi_dsi_base, struct frame_rate_ctrl *frm_rate_ctrl)
{
	uint32_t ldi_vrt_ctrl0;
	uint32_t vsync_delay_cnt;
	uint32_t tmp_hline_time;

	tmp_hline_time =
		get_mipi_timing_hline_time(hisifd, frm_rate_ctrl->timing.hline_time);

	set_reg(mipi_dsi_base + MIPIDSI_VID_HLINE_TIME_OFFSET, tmp_hline_time, 15, 0);

	set_reg(mipi_dsi_base + MIPIDSI_VID_VBP_LINES_OFFSET, frm_rate_ctrl->timing.vbp, 10, 0);

	if (frm_rate_ctrl->timing.vfp > V_FRONT_PORCH_MAX) {
		ldi_vrt_ctrl0 = V_FRONT_PORCH_MAX;
		vsync_delay_cnt = (frm_rate_ctrl->timing.vfp - V_FRONT_PORCH_MAX) * tmp_hline_time;
	} else {
		ldi_vrt_ctrl0 = frm_rate_ctrl->timing.vfp;
		vsync_delay_cnt = 0;
	}
	set_reg(mipi_dsi_base + MIPIDSI_VID_VFP_LINES_OFFSET, ldi_vrt_ctrl0, 10, 0);
	set_reg(mipi_dsi_base + MIPI_VSYNC_DELAY_TIME, vsync_delay_cnt, 32, 0);

	frm_rate_ctrl->current_hline_time = tmp_hline_time;
	frm_rate_ctrl->current_vfp = frm_rate_ctrl->timing.vfp;

	HISI_FB_INFO("set paramters:dpi_hsize = %d,hsa = %d,hbp = %d,hline_time = %d,"\
		"vsa = %d,vbp = %d,vfp = %d,vactive_line = %d", \
		frm_rate_ctrl->timing.dpi_hsize, frm_rate_ctrl->timing.hsa, \
		frm_rate_ctrl->timing.hbp, frm_rate_ctrl->timing.hline_time, \
		frm_rate_ctrl->timing.vsa, frm_rate_ctrl->timing.vbp, \
		frm_rate_ctrl->timing.vfp, frm_rate_ctrl->timing.vactive_line);
}

static void frm_update_set_phy_reg(struct hisi_fb_data_type *hisifd,
	char __iomem *mipi_dsi_base, const struct mipi_dsi_phy_ctrl *phy_ctrl)
{
	unsigned long dw_jiffies;
	uint32_t tmp;
	bool is_ready = false;
	uint32_t status_phy_lock = 0x1;

	mipi_config_phy_test_code(mipi_dsi_base, 0x00010049,
		(phy_ctrl->rg_pll_posdiv << 4) | phy_ctrl->rg_pll_prediv);

	mipi_config_phy_test_code(mipi_dsi_base, 0x0001004A, phy_ctrl->rg_pll_fbkdiv);
	HISI_FB_INFO("set mipi pll:rg_pll_prediv=%d, rg_pll_posdiv=%d, rg_pll_fbkdiv=%d\n",
			phy_ctrl->rg_pll_prediv, phy_ctrl->rg_pll_posdiv,
			phy_ctrl->rg_pll_fbkdiv);

	mipi_config_phy_test_code(mipi_dsi_base, 0x0001004B, 0x1);

	if (hisifd->panel_info.mipi.phy_mode == CPHY_MODE)
		mipi_config_cphy_spec1v0_parameter(mipi_dsi_base, &(hisifd->panel_info), phy_ctrl);
	else
		mipi_config_dphy_spec1v2_parameter(mipi_dsi_base, &(hisifd->panel_info), phy_ctrl);

	dw_jiffies = jiffies + HZ / 2;
	do {
		tmp = inp32(mipi_dsi_base + MIPIDSI_PHY_STATUS_OFFSET);
		if ((tmp & status_phy_lock) == status_phy_lock) {
			is_ready = true;
			break;
		}
	} while (time_after(dw_jiffies, jiffies));

	if (!is_ready) {
		HISI_FB_ERR("fb%d, phylock is not ready!MIPIDSI_PHY_STATUS_OFFSET=0x%x.\n",
			hisifd->index, tmp);
	}

	set_reg(mipi_dsi_base + MIPIDSI_PHY_IF_CFG_OFFSET, phy_ctrl->phy_stop_wait_time, 8, 8);

	outp32(mipi_dsi_base + MIPIDSI_PHY_TMR_LPCLK_CFG_OFFSET,
		(phy_ctrl->clk_lane_lp2hs_time + (phy_ctrl->clk_lane_hs2lp_time << 16)));

	outp32(mipi_dsi_base + MIPIDSI_PHY_TMR_CFG_OFFSET,
		(phy_ctrl->data_lane_lp2hs_time + (phy_ctrl->data_lane_hs2lp_time << 16)));
}

static bool wait_ldi_vstate_idle(struct hisi_fb_data_type *hisifd, struct timeval *tv0)
{
	bool ret = false;
	uint32_t ldi_vstate;
	uint64_t lane_byte_clk;
	uint32_t vfp_line;
	uint32_t hline_time;
	uint32_t vsync_delay_cnt;
	uint32_t vfp_time;
	struct timeval tv1;
	uint32_t timediff = 0;
	uint32_t state_idle = 0x1;
	struct frame_rate_ctrl *frm_rate_ctrl = NULL;

	frm_rate_ctrl = &(hisifd->panel_info.frm_rate_ctrl);

	lane_byte_clk = (hisifd->panel_info.mipi.phy_mode == DPHY_MODE) ?
			frm_rate_ctrl->phy_ctrl.lane_byte_clk : frm_rate_ctrl->phy_ctrl.lane_word_clk;

	hline_time = inp32(hisifd->mipi_dsi0_base + MIPIDSI_VID_HLINE_TIME_OFFSET) & 0x7FFF;
	vfp_line = inp32(hisifd->mipi_dsi0_base + MIPIDSI_VID_VFP_LINES_OFFSET) & 0x3FF;
	vfp_line += 10; /* vfp margin 10 lines */
	vsync_delay_cnt = inp32(hisifd->mipi_dsi0_base + MIPI_VSYNC_DELAY_TIME);

	vfp_time = (vfp_line * hline_time + vsync_delay_cnt) /
		((uint32_t)(lane_byte_clk / 1000000UL)); /* unit convert to us from s */

	while (timediff < vfp_time) {
		udelay(5);
		hisifb_get_timestamp(&tv1);
		timediff = hisifb_timestamp_diff(tv0, &tv1);
		ldi_vstate = inp32(hisifd->mipi_dsi0_base + MIPI_LDI_VSTATE) & 0xFFFF;
		if ((ldi_vstate & state_idle) == state_idle) {
			ret= true;
			break;
		}
	}

	HISI_FB_INFO("timediff = %d us, vfp_time = %d us, ldi_vstate = 0x%x\n",
		timediff, vfp_time, ldi_vstate);
	return ret;
}

/* vactive end interrupt call this to set frm rate para */
int mipi_dsi_frm_rate_para_set_reg(struct hisi_fb_data_type *hisifd)
{
	uint8_t esd_enable;
	struct timeval tv0;
	struct hisi_panel_info *pinfo = NULL;
	struct frame_rate_ctrl *frm_rate_ctrl = NULL;

	if (hisifd == NULL) {
		HISI_FB_ERR("hisifd is NULL\n");
		return 0;
	}

	pinfo = &(hisifd->panel_info);
	if (!hisifd->panel_info.dfr_support)
		return 0;

	esd_enable = pinfo->esd_enable;
	if (is_mipi_video_panel(hisifd)) {
		pinfo->esd_enable = 0;
		disable_ldi(hisifd);
	}
	frm_rate_ctrl = &(hisifd->panel_info.frm_rate_ctrl);
	hisifb_get_timestamp(&tv0);

	spin_lock(&hisifd->mipi_resource_lock);

	/* if frm update has higher priority than online play,
	change status to FRM_UPDT_DOING at vactive start int */
	if (frm_rate_ctrl->status != FRM_UPDT_NEED_DOING) {
		HISI_FB_ERR("wrong status %d", frm_rate_ctrl->status);
		spin_unlock(&hisifd->mipi_resource_lock);
		goto exit;
	}
	frm_rate_ctrl->status = FRM_UPDT_DOING;

	spin_unlock(&hisifd->mipi_resource_lock);

	HISI_FB_DEBUG("current_dsi_bit_clk = %d, target_dsi_bit_clk = %d\n", \
		frm_rate_ctrl->current_dsi_bit_clk, frm_rate_ctrl->target_dsi_bit_clk);

	if (hisifd->underflow_flag > 0) {
		frm_rate_ctrl->status = FRM_UPDT_CONFIG_DONE;
		HISI_FB_INFO("skip this frame, flag %d\n", hisifd->underflow_flag);
		goto exit;
	}

	if (frm_rate_ctrl->current_dsi_bit_clk == frm_rate_ctrl->target_dsi_bit_clk) {
		if (!need_send_dfr_cmds_in_vactive(hisifd))
			hisifd->fps_upt_isr_handler(hisifd); /* sending fps change cmds to ddic */

		frm_update_set_dsi_reg(hisifd, hisifd->mipi_dsi0_base, frm_rate_ctrl);
	} else {
		if (!wait_ldi_vstate_idle(hisifd, &tv0)) {
			if (is_mipi_video_panel(hisifd)) {
				pinfo->esd_enable = esd_enable;
				enable_ldi(hisifd);
			}
			frm_rate_ctrl->status = FRM_UPDT_CONFIG_DONE;
			HISI_FB_ERR("wait vstatus idle timeout");
			return 0;
		}

		if (!need_send_dfr_cmds_in_vactive(hisifd))
			hisifd->fps_upt_isr_handler(hisifd); /* sending fps change cmds to ddic */

		frm_update_set_dsi_reg(hisifd, hisifd->mipi_dsi0_base, frm_rate_ctrl);
		frm_update_set_phy_reg(hisifd, hisifd->mipi_dsi0_base, &frm_rate_ctrl->phy_ctrl);
	}

	if (frm_rate_ctrl->target_frame_rate == FPS_60HZ) {
		frm_rate_ctrl->notify_type = TYPE_NOTIFY_PERF_ONLY;
		frm_rate_ctrl->porch_ratio = get_porch_ratio(hisifd);
		queue_work(hisifd->dfr_notice_wq, &hisifd->dfr_notice_work);
	}
	HISI_FB_INFO("fps successfully changed from %d to %d\n",
		pinfo->fps, frm_rate_ctrl->target_frame_rate);
#ifdef CONFIG_HUAWEI_DUBAI
	// report when lcd fresh rate change
	HWDUBAI_LOGE("DUBAI_TAG_EPS_LCD_FREQ", "sourcerate=%d targetrate=%d", pinfo->fps, frm_rate_ctrl->target_frame_rate);
#endif

	if (frm_rate_ctrl->target_frame_rate == FPS_60HZ &&
		hisifd->panel_info.dfr_delay_time > 0)
		queue_work(hisifd->dfr_delay_wq, &hisifd->dfr_delay_work);
	else
		hisi_dfr_end(hisifd);

exit:
	if (is_mipi_video_panel(hisifd)) {
		pinfo->esd_enable = esd_enable;
		enable_ldi(hisifd);
	}
	return 0;
}

/* frame rate update entrance */
int mipi_dsi_frm_rate_ctrl(struct hisi_fb_data_type *hisifd, int frm_rate)
{
	int ret;
	struct frame_rate_ctrl *frm_rate_ctrl = NULL;

	if (hisifd == NULL) {
		HISI_FB_ERR("hisifd is NULL\n");
		return 0;
	}
	if (hisifd->index != PRIMARY_PANEL_IDX) {
		HISI_FB_INFO("hisifd->index is not PRIMARY_PANEL_IDX!\n");
		return 0;
	}

	if (!hisifd->panel_power_on) {
		HISI_FB_INFO("fb%d, panel is power off\n", hisifd->index);
		return 0;
	}

	if (!hisifd->panel_info.dfr_support) {
		HISI_FB_INFO("dfr is not support");
		return 0;
	}

	if (g_dss_dfr_debug & BIT(0)) {
		HISI_FB_INFO("g_dss_dfr_debug = 0x%x, return", g_dss_dfr_debug);
		return 0;
	}

	frm_rate_ctrl = &(hisifd->panel_info.frm_rate_ctrl);

	spin_lock(&hisifd->mipi_resource_lock);
	if (frm_rate_ctrl->status != FRM_UPDT_DONE) {
		HISI_FB_INFO("last frame change has not been completed, status = %d",
			frm_rate_ctrl->status);
		spin_unlock(&hisifd->mipi_resource_lock);
		return 0;
	}
	if (frm_rate == frm_rate_ctrl->current_frame_rate) {
		HISI_FB_INFO("frm_rate %d does not change", frm_rate);
		spin_unlock(&hisifd->mipi_resource_lock);
		return 0;
	}
	frm_rate_ctrl->status = FRM_UPDT_CONFIG;
	frm_rate_ctrl->target_frame_rate = frm_rate;
	spin_unlock(&hisifd->mipi_resource_lock);

	if (frm_rate_ctrl->target_frame_rate == FPS_60HZ) {
		frm_rate_ctrl->notify_type = TYPE_NOTIFY_EFFECT_ONLY;
	} else if (frm_rate_ctrl->target_frame_rate == FPS_90HZ){
		frm_rate_ctrl->notify_type = TYPE_NOTIFY_PERF_EFFECT;
	}
	frm_rate_ctrl->porch_ratio = get_porch_ratio(hisifd);
	queue_work(hisifd->dfr_notice_wq, &hisifd->dfr_notice_work);

	ret = mipi_dsi_frm_rate_para_config(hisifd);

	spin_lock(&hisifd->mipi_resource_lock);
	frm_rate_ctrl->status = FRM_UPDT_CONFIG_DONE;
	spin_unlock(&hisifd->mipi_resource_lock);

	HISI_FB_INFO("frm_rate_para_config has been calculated\n");
	return ret;
}

void dfr_power_on_notification(struct hisi_fb_data_type *hisifd)
{
	struct frame_rate_ctrl *frm_rate_ctrl = NULL;

	if (hisifd == NULL) {
		HISI_FB_ERR("hisifd is NULL\n");
		return;
	}

	if (hisifd->index != PRIMARY_PANEL_IDX) {
		HISI_FB_DEBUG("fb%d is not primary panel\n", hisifd->index);
		return;
	}

	frm_rate_ctrl = &(hisifd->panel_info.frm_rate_ctrl);

	if (hisifd->panel_info.dfr_support == 0 || frm_rate_ctrl->registered == 0)
		return;

	if (hisifd->panel_info.dfr_method == DFR_METHOD_VPORCH) {
		frm_rate_ctrl->notify_type = TYPE_NOTIFY_PERF_ONLY;
		frm_rate_ctrl->porch_ratio = get_porch_ratio(hisifd);
		queue_work(hisifd->dfr_notice_wq, &hisifd->dfr_notice_work);
	}

	HISI_FB_INFO("notify hwc, dfr_method 0x%x", hisifd->panel_info.dfr_method);
}

/* panel off, set default frame rate */
void mipi_dsi_frm_rate_ctrl_init(struct hisi_fb_data_type *hisifd)
{
	struct frame_rate_ctrl *frm_rate_ctrl = NULL;

	if (hisifd == NULL) {
		HISI_FB_ERR("hisifd is NULL\n");
		return;
	}
	if (hisifd->index != PRIMARY_PANEL_IDX) {
		HISI_FB_DEBUG("fb%d is not primary panel\n", hisifd->index);
		return;
	}
	frm_rate_ctrl = &(hisifd->panel_info.frm_rate_ctrl);

	frm_rate_ctrl->target_frame_rate = FPS_60HZ;
	frm_rate_ctrl->current_frame_rate = FPS_60HZ;
	frm_rate_ctrl->status = FRM_UPDT_DONE;
	hisifd->panel_info.fps = FPS_60HZ;
	frm_rate_ctrl->current_hline_time = hisifd->panel_info.mipi.hline_time;
	frm_rate_ctrl->current_vfp = hisifd->panel_info.mipi.vfp;
	frm_rate_ctrl->current_dsi_bit_clk = hisifd->panel_info.mipi.dsi_bit_clk;
	frm_rate_ctrl->target_dsi_bit_clk = frm_rate_ctrl->current_dsi_bit_clk;
	memset(&(frm_rate_ctrl->phy_ctrl), 0, sizeof(frm_rate_ctrl->phy_ctrl));

	if (hisifd->panel_info.frm_rate_ctrl.registered == 0) {
		hisifd->panel_info.frm_rate_ctrl.registered = 1;
		hisifd->panel_info.fps_updt = FPS_60HZ;
		hisifd->dfr_notice_wq = create_singlethread_workqueue("dfr_notice_work");
		if (hisifd->dfr_notice_wq == NULL) {
			HISI_FB_ERR("fb%d, create dfr notice workqueue failed!\n", hisifd->index);
			return;
		}
		INIT_WORK(&hisifd->dfr_notice_work, hisi_dfr_notice_handle_func);

		hisifd->dfr_delay_wq = create_singlethread_workqueue("dfr_delay_work");
		if (hisifd->dfr_delay_wq == NULL) {
			HISI_FB_ERR("fb%d, create dfr delay workqueue failed!\n", hisifd->index);
			return;
		}
		INIT_WORK(&hisifd->dfr_delay_work, hisi_dfr_delay_work);

		HISI_FB_INFO("init succ");
		return;
	}

	get_dsi_phy_ctrl(hisifd, &(frm_rate_ctrl->phy_ctrl));

	HISI_FB_INFO("fps_updt:%d, fps:%d, target_frame_rate:%d, current_frame_rate:%d",
		hisifd->panel_info.fps_updt, hisifd->panel_info.fps,
		frm_rate_ctrl->target_frame_rate, frm_rate_ctrl->current_frame_rate);
}

