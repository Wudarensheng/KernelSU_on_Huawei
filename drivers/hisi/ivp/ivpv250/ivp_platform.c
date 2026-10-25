/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2018-2019. All rights reserved.
 * Description: This file implements ivp initialization and control functions
 * Author: chenweiyu
 * Create: 2018-02-18
 */

#include "ivp_platform.h"
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/dma-mapping.h>
#include "ivp_log.h"
#include "ivp_reg.h"
#include "ivp_rdr.h"
#include "ivp_manager.h"
#include "ivp_atf_subsys.h"
#include "ivp_sec.h"

#define REMAP_ADD                        0xE9100000
#define DEAD_FLAG                        0xDEADBEEF
#define SIZE_16K                         (16 * 1024)

static void *g_iram;

static u32 noc_ivp_reg_read(const struct ivp_device *ivp_devp, unsigned int off)
{
	char __iomem *reg = ivp_devp->io_res.noc_ivp_base_addr + off;
	return readl(reg);
}

static void noc_ivp_reg_write(const struct ivp_device *ivp_devp, unsigned int off, u32 val)
{
	char __iomem *reg = ivp_devp->io_res.noc_ivp_base_addr + off;
	writel(val, reg);
}

static int ivp_get_memory_section(struct platform_device *plat_dev,
	struct ivp_device *ivp_devp)
{
	int ret;
	int i;
	unsigned int size = 0;
	dma_addr_t dma_addr = 0;

	ivp_devp->vaddr_memory = NULL;
	ret = of_property_read_u32(plat_dev->dev.of_node, OF_IVP_DYNAMIC_MEM, &size);
	if ((ret != 0) || (size == 0)) {
		ivp_err("failed get dynamic mem, ret:%d size:%d", ret, size);
		return -EINVAL;
	}
	ivp_devp->dynamic_mem_size = size;
	ivp_devp->ivp_meminddr_len = (int)size;

	ret = of_property_read_u32(plat_dev->dev.of_node,
		OF_IVP_DYNAMIC_MEM_SEC_SIZE, &size);
	if ((ret != 0) || (size == 0)) {
		ivp_err("failed to get dynamic section, ret:%d size:%d", ret, size);
		return -EINVAL;
	}
	ivp_devp->dynamic_mem_section_size = size;
	if ((ivp_devp->dynamic_mem_section_size * (ivp_devp->sect_count - DDR_SECTION_INDEX)) !=
		ivp_devp->dynamic_mem_size) {
		ivp_err("dynamic mem size or section size is error");
		return -EINVAL;
	}

	/*lint -save -e598 -e648*/
	dma_set_mask_and_coherent(&ivp_devp->ivp_pdev->dev, DMA_BIT_MASK(64)); /* bit mask is 64 */
	/*lint -restore*/
	ivp_devp->vaddr_memory = dma_alloc_coherent(&ivp_devp->ivp_pdev->dev,
		ivp_devp->dynamic_mem_size, &dma_addr, GFP_KERNEL);
	if (!ivp_devp->vaddr_memory) {
		ivp_err("get vaddr_memory failed");
		return -ENOMEM;
	}

	for (i = DDR_SECTION_INDEX; i < ivp_devp->sect_count; i++) {
		if (i == DDR_SECTION_INDEX) {
			ivp_devp->sects[i].acpu_addr = dma_addr >> IVP_MMAP_SHIFT;
		} else {
			ivp_devp->sects[i].acpu_addr =
				((ivp_devp->sects[i - 1].acpu_addr << IVP_MMAP_SHIFT) +
				ivp_devp->sects[i - 1].len) >> IVP_MMAP_SHIFT;
			ivp_devp->sects[i].ivp_addr = ivp_devp->sects[i - 1].ivp_addr +
				ivp_devp->sects[i - 1].len;
		}
		ivp_devp->sects[i].len = ivp_devp->dynamic_mem_section_size;
		ivp_dbg("ivp sections 0x%pK", ivp_devp->sects[i].acpu_addr);
	}

	return ret;
}

static void ivp_free_memory_section(struct ivp_device *ivp_devp)
{
	dma_addr_t dma_addr;

	dma_addr = ivp_devp->sects[DDR_SECTION_INDEX].acpu_addr << IVP_MMAP_SHIFT;
	if (ivp_devp->vaddr_memory) {
		dma_free_coherent(&ivp_devp->ivp_pdev->dev,
			ivp_devp->dynamic_mem_size, ivp_devp->vaddr_memory, dma_addr);
		ivp_devp->vaddr_memory = NULL;
	}
}

static inline void ivp_hw_remap_ivp2ddr(struct ivp_device *ivp_devp,
	unsigned int ivp_addr, unsigned int len, unsigned long ddr_addr)
{
	unsigned int ivp_addr_base = ((ivp_addr / SIZE_1MB) << IVP_REMP_ADDR_SHIFT) +
		(ivp_addr / SIZE_1MB);
	unsigned int ddr_addr_base = ((ddr_addr / SIZE_1MB) << IVP_REMP_ADDR_SHIFT) +
		(ddr_addr / SIZE_1MB);
	unsigned int remap_len = (len << IVP_REMP_LEN_SHIFT) + len;

	ivp_info("ivp remap ddr Enter");
	ivp_reg_write(ivp_devp, ADDR_IVP_CFG_SEC_REG_START_REMAP_ADDR, ivp_addr_base);
	ivp_reg_write(ivp_devp, ADDR_IVP_CFG_SEC_REG_REMAP_LENGTH, remap_len);
	ivp_reg_write(ivp_devp, ADDR_IVP_CFG_SEC_REG_DDR_REMAP_ADDR, (u32)ddr_addr_base);
}

static inline int ivp_remap_addr_ivp2ddr(struct ivp_device *ivp_devp,
	unsigned int ivp_addr, int len, unsigned long ddr_addr)
{
	ivp_dbg("ivp_addr:0x%x, len:0x%x, ddr_addr:0x%lx",
		ivp_addr, len, ddr_addr);
	if ((ivp_addr & MASK_1MB) != 0 ||
		(ddr_addr & MASK_1MB) != 0 ||
		len >= MAX_DDR_LEN * SIZE_1MB) {
		ivp_err("not aligned");
		return -EINVAL;
	}
	len = (len + SIZE_1MB - 1) / SIZE_1MB - 1;
	ivp_hw_remap_ivp2ddr(ivp_devp, ivp_addr, (u32)len, ddr_addr);

	return 0;
}

static int ivp_get_secure_attribute(struct platform_device *plat_dev,
	struct ivp_device *ivp_devp)
{
	int ret;
	unsigned int sec_support = 0;

	ret = of_property_read_u32(plat_dev->dev.of_node,
		OF_IVP_SEC_SUPPORT, &sec_support);
	if (ret) {
		ivp_err("get ivp sec support flag fail, ret:%d", ret);
		return -EINVAL;
	}
	ivp_devp->ivp_sec_support = sec_support;
	ivp_info("get ivp sec support flag :%d", sec_support);

	return ret;
}

static void ivp_secure_config(void)
{
	ivpatf_change_slv_secmod(IVP_SEC);
	ivpatf_change_mst_secmod(IVP_SEC);
}

int ivp_poweron_pri(struct ivp_device *ivp_devp)
{
	int ret;

	if (!ivp_devp) {
		ivp_err("invalid input param ivp_devp");
		return -ENODEV;
	}
	/* 0.Enable the power */
	if ((ret = regulator_enable(ivp_devp->ivp_media2_regulator)) != 0) {
		ivp_err("ivp_media2_regulator enable failed %d", ret);
		return ret;
	}

	/* 1.Set Clock rate */
	if ((ret = clk_set_rate(ivp_devp->clk, (unsigned long)ivp_devp->clk_rate)) != 0) {
		ivp_err("set rate %#x fail, ret:%d", ivp_devp->clk_rate, ret);
		goto try_down_freq;
	}

	/* 2.Enable the clock */
	if ((ret = clk_prepare_enable(ivp_devp->clk)) != 0) {
		ivp_err("clk prepare enable failed, ret = %d", ret);
		goto try_down_freq;
	}
	goto normal_frq_success;

try_down_freq:
	ivp_info("try set core freq to: %ld", (unsigned long)ivp_devp->lowtemp_clk_rate);
	if ((ret = clk_set_rate(ivp_devp->clk, (unsigned long)ivp_devp->lowtemp_clk_rate)) != 0) {
		ivp_err("set low rate %#x fail, ret:%d", ivp_devp->lowtemp_clk_rate, ret);
		goto err_clk_set_rate;
	}

	if ((ret = clk_prepare_enable(ivp_devp->clk)) != 0) {
		ivp_err("low clk prepare enable failed, ret = %d", ret);
		goto err_clk_prepare_enable;
	}

normal_frq_success:
	ivp_info("set core success to: %ld", clk_get_rate(ivp_devp->clk));
	/* 3.Enable the power */
	if ((ret = regulator_enable(ivp_devp->regulator)) != 0) {
		ivp_err("regularot enable failed %d", ret);
		goto err_regulator_enable_ivp;
	}

	if (ivp_devp->ivp_secmode == SECURE_MODE)
		ivp_secure_config();
	ivp_set_qos_cfg(ivp_devp);

	return ret;

err_regulator_enable_ivp:
	clk_disable_unprepare(ivp_devp->clk);

err_clk_prepare_enable:
	if ((ret = clk_set_rate(ivp_devp->clk, (unsigned long)ivp_devp->lowtemp_clk_rate)) != 0)
		ivp_err("err set lowfrq rate %#x fail %d", ivp_devp->lowtemp_clk_rate, ret);

err_clk_set_rate:
	if ((ret = regulator_disable(ivp_devp->ivp_media2_regulator)) != 0)
		ivp_err("ivp_media2_regulator disable failed %d", ret);
	return -EINVAL;
}

int ivp_poweron_remap(struct ivp_device *ivp_devp)
{
	int ret;
	unsigned long sec_phy_remap_addr = 0;

	if (!ivp_devp) {
		ivp_err("invalid input param ivp_devp");
		return -EINVAL;
	}
	if (ivp_devp->ivp_secmode == SECURE_MODE) {
		ret = ivp_get_secbuff(&ivp_devp->ivp_pdev->dev,
			ivp_devp->ivp_sec_buff_fd, &sec_phy_remap_addr);
		if (ret) {
			ivp_err("ivp get secbuff failed!");
			return ret;
		}

		if ((sec_phy_remap_addr & MASK_1MB) != 0) {
			ivp_dbg("input sec buf add:0x%lx make it 1M algin 0x%lx",
				sec_phy_remap_addr,
				((sec_phy_remap_addr / SIZE_1MB) << SHIFT_1MB) + SIZE_1MB);

			sec_phy_remap_addr =
				((sec_phy_remap_addr / SIZE_1MB) << SHIFT_1MB) + SIZE_1MB;
		}
		ivp_devp->sec_dev->ivp_sec_phymem_addr = sec_phy_remap_addr;
		ret = ivpatf_poweron_remap_secmod(
			ivp_devp->sects[DDR_SECTION_INDEX].ivp_addr,
			IVP_SEC_BUFF_SIZE,
			sec_phy_remap_addr);
	} else {
		ret = ivp_remap_addr_ivp2ddr(ivp_devp,
			ivp_devp->sects[DDR_SECTION_INDEX].ivp_addr,
			ivp_devp->ivp_meminddr_len,
			ivp_devp->sects[DDR_SECTION_INDEX].acpu_addr << IVP_MMAP_SHIFT);
	}
	if (ret)
		ivp_err("remap addr failed %d", ret);

	return ret;
}

int ivp_poweroff_pri(struct ivp_device *ivp_devp)
{
	int ret;
	int i = 0;
	unsigned int waiti;

	if (!ivp_devp) {
		ivp_err("invalid input param ivp_devp");
		return -EINVAL;
	}
	waiti = ivp_reg_read(ivp_devp, IVP_REG_OFF_PWAITMODE);

	while (((waiti & IVP_DSP_PWAITMODE) == 0) && (i < 3)) { /* 3 is loop max */
		udelay(100); /* 100 us */
		ivp_err("ivp core is not in wfi mode, 0x%x", waiti);
		waiti = ivp_reg_read(ivp_devp, IVP_REG_OFF_PWAITMODE);
		i++;
	}

	ret = regulator_disable(ivp_devp->regulator);
	if (ret)
		ivp_err("regulator power off failed %d", ret);

	clk_disable_unprepare(ivp_devp->clk);
	ret = clk_set_rate(ivp_devp->clk, (unsigned long)ivp_devp->lowfrq_pd_clk_rate);
	if (ret != 0)
		ivp_warn("set lfrq pd rate %#x fail, ret:%d", ivp_devp->lowfrq_pd_clk_rate, ret);

	ret = regulator_disable(ivp_devp->ivp_media2_regulator);
	if (ret)
		ivp_err("ivp_media2_regulator power off failed %d", ret);

	return ret;
}

static int ivp_setup_regulator(struct platform_device *plat_dev,
	struct ivp_device *ivp_devp)
{
	struct regulator *ivp_media2_regulator = NULL;
	struct regulator *regulator = NULL;

	regulator = devm_regulator_get(&plat_dev->dev, IVP_REGULATOR);
	if (IS_ERR(regulator)) {
		ivp_err("Get ivp regulator failed");
		return -ENODEV;
	} else {
		ivp_devp->regulator = regulator;
	}

	ivp_media2_regulator = devm_regulator_get(&plat_dev->dev, IVP_MEDIA_REGULATOR);
	if (IS_ERR(ivp_media2_regulator)) {
		ivp_err("Get ivp media regulator failed");
		return -ENODEV;
	} else {
		ivp_devp->ivp_media2_regulator = ivp_media2_regulator;
	}

	return 0;
}

static int ivp_setup_clk(struct platform_device *plat_dev,
	struct ivp_device *ivp_devp)
{
	int ret;

	ivp_devp->clk = devm_clk_get(&plat_dev->dev, OF_IVP_CLK_NAME);
	if (IS_ERR(ivp_devp->clk)) {
		ivp_err("get clk failed");
		return -ENODEV;
	}
	ret = of_property_read_u32(plat_dev->dev.of_node,
		OF_IVP_CLK_RATE_NAME, &ivp_devp->clk_rate);
	if (ret) {
		ivp_err("get clk rate failed, ret:%d", ret);
		return -ENOMEM;
	}
	ivp_info("get clk rate: %u", ivp_devp->clk_rate);

	if ((ret = of_property_read_u32(plat_dev->dev.of_node,
		OF_IVP_MIDDLE_CLK_RATE_NAME, &ivp_devp->middle_clk_rate)) != 0) {
		ivp_err("get middle freq rate failed, ret:%d", ret);
		return -ENOMEM;
	}
	ivp_info("get middle freq clk rate: %u", ivp_devp->middle_clk_rate);

	if ((ret = of_property_read_u32(plat_dev->dev.of_node,
		OF_IVP_LOW_CLK_RATE_NAME, &ivp_devp->low_clk_rate)) != 0) {
		ivp_err("get low freq rate failed, ret:%d", ret);
		return -ENOMEM;
	}
	ivp_info("get low freq clk rate: %u", ivp_devp->low_clk_rate);

	if ((ret = of_property_read_u32(plat_dev->dev.of_node,
		OF_IVP_ULTRA_LOW_CLK_RATE_NAME, &ivp_devp->ultra_low_clk_rate)) != 0) {
		ivp_err("get ultra low freq clk rate, ret:%d", ret);
		return -ENOMEM;
	}
	ivp_info("get ultra low freq clk rate: %u", ivp_devp->ultra_low_clk_rate);

	if ((ret = of_property_read_u32(plat_dev->dev.of_node,
		OF_IVP_LOWFREQ_CLK_RATE_NAME, &ivp_devp->lowfrq_pd_clk_rate)) != 0) {
		ivp_err("get lowfreq pd clk rate failed, ret:%d", ret);
		return -ENOMEM;
	}
	ivp_info("get lowfrq pd clk rate: %u", ivp_devp->lowfrq_pd_clk_rate);

	if ((ret = of_property_read_u32(plat_dev->dev.of_node,
		OF_IVP_LOW_TEMP_RATE_NAME, &ivp_devp->lowtemp_clk_rate)) != 0) {
		ivp_err("get low temperature rate failed, ret:%d", ret);
		return -ENOMEM;
	}
	ivp_info("get low temperature clk rate: %u", ivp_devp->lowtemp_clk_rate);

	return ret;
}

int ivp_change_clk(struct ivp_device *ivp_devp, unsigned int level)
{
	int ret;

	if (!ivp_devp) {
		ivp_err("invalid input param ivp_devp");
		return -EINVAL;
	}
	ivp_info("try to change freq level from %d to %d", ivp_devp->clk_level, level);

	if (ivp_devp->clk_level == IVP_CLK_LEVEL_LOW) {
		ret = clk_set_rate(ivp_devp->clk, (unsigned long)ivp_devp->lowfrq_pd_clk_rate);
		if (ret != 0) {
			ivp_err("set clk rate %#x fail, ret:%d", ivp_devp->lowfrq_pd_clk_rate, ret);
			return ret;
		}
	}

	ivp_devp->clk_level = level;
	switch (ivp_devp->clk_level) {
	case IVP_CLK_LEVEL_ULTRA_LOW:
		ivp_info("ivp freq to ultra low level");
		ivp_devp->clk_usrsetrate = ivp_devp->ultra_low_clk_rate;
		break;

	case IVP_CLK_LEVEL_LOW:
		ivp_info("ivp freq to low level");
		ivp_devp->clk_usrsetrate = ivp_devp->low_clk_rate;
		break;

	case IVP_CLK_LEVEL_MEDIUM:
		ivp_info("ivp freq to media level");
		ivp_devp->clk_usrsetrate = ivp_devp->middle_clk_rate;
		break;

	case IVP_CLK_LEVEL_HIGH:
		ivp_info("ivp freq to high level");
		ivp_devp->clk_usrsetrate = ivp_devp->clk_rate;
		break;

	default:
		ivp_info("use default freq");
		ivp_devp->clk_usrsetrate = ivp_devp->clk_rate;
		break;
	}

	/* Set Clock rate */
	ivp_info("set clock rate");
	ret = clk_set_rate(ivp_devp->clk, (unsigned long)ivp_devp->clk_usrsetrate);
	if (ret == 0) {
		ivp_info("set core success to: %ld", clk_get_rate(ivp_devp->clk));
		return ret;
	}

	ivp_info("try set core freq to: %ld", (unsigned long)ivp_devp->low_clk_rate);
	ret = clk_set_rate(ivp_devp->clk, (unsigned long)ivp_devp->low_clk_rate);
	if (ret != 0)
		ivp_err("set low rate %#x fail, ret:%d", ivp_devp->low_clk_rate, ret);

	return ret;
}

int ivp_init_pri(struct platform_device *pdev, struct ivp_device *ivp_devp)
{
	int ret;

	if (!pdev || !ivp_devp) {
		ivp_err("invalid input param pdev or ivp_devp");
		return -EINVAL;
	}
	ret = ivp_setup_regulator(pdev, ivp_devp);
	if (ret) {
		ivp_err("setup regulator failed, ret:%d", ret);
		return ret;
	}

	ret = ivp_setup_clk(pdev, ivp_devp);
	if (ret) {
		ivp_err("setup clk failed, ret:%d", ret);
		return ret;
	}

	ret = ivp_get_memory_section(pdev, ivp_devp);
	if (ret) {
		ivp_err("get memory section failed, ret:%d", ret);
		return ret;
	}

	ret = ivp_get_secure_attribute(pdev, ivp_devp);
	if (ret) {
		ivp_err("get_secure_attribute failed, ret:%d", ret);
		goto ivp_free_memory;
	}
	/* create kthread should call at last */
	ret = ivp_rdr_init(ivp_devp);
	if (ret) {
		ivp_err("rdr init failed, ret:%d", ret);
		goto ivp_free_memory;
	}

	return ret;

ivp_free_memory:
	ivp_free_memory_section(ivp_devp);
	return ret;
}

void ivp_deinit_pri(struct ivp_device *ivp_devp)
{
	if (!ivp_devp) {
		ivp_err("invalid input param ivp_devp");
		return;
	}
	ivp_free_memory_section(ivp_devp);
	ivp_rdr_deinit();
}

int ivp_init_resethandler(struct ivp_device *ivp_devp)
{
	if (!ivp_devp) {
		ivp_err("invalid input param ivp_devp");
		return -EINVAL;
	}
	if (ivp_devp->ivp_secmode == SECURE_MODE) {
		atomic_set(&ivp_devp->sec_dev->ivp_image_success, 0);
	} else {
		/* init code to remap address */
		g_iram = ioremap(REMAP_ADD, SIZE_16K);
		if (!g_iram) {
			ivp_err("Can't map ivp base address");
			return -ENOMEM;
		}

		iowrite32(DEAD_FLAG, g_iram);
	}
	return 0;
}

int ivp_check_resethandler(struct ivp_device *ivp_devp)
{
	int inited = 0;
	u32 flag = 0;

	if (!ivp_devp) {
		ivp_err("invalid input param ivp_devp");
		return -EINVAL;
	}
	/* check init code in remap address */
	if (ivp_devp->ivp_secmode == SECURE_MODE) {
		if (atomic_read(&ivp_devp->sec_dev->ivp_image_success) == 1)
			inited = 1;
	} else {
		if (g_iram)
			flag = ioread32(g_iram);
		if (flag != DEAD_FLAG)
			inited = 1;
	}

	return inited;
}

void ivp_deinit_resethandler(struct ivp_device *ivp_devp)
{
	if (!ivp_devp) {
		ivp_err("invalid input param ivp_devp");
		return;
	}
	if (ivp_devp->ivp_secmode == SECURE_MODE) {
		atomic_set(&ivp_devp->sec_dev->ivp_image_success, 0);
	} else {
		/* deinit remap address */
		if (g_iram) {
			iounmap(g_iram);
			g_iram = NULL;
		}
	}
}

int ivp_sec_loadimage(struct ivp_device *ivp_devp)
{
	if (!ivp_devp) {
		ivp_err("invalid input param ivp_devp");
		return -EINVAL;
	}
	return ivp_sec_load(ivp_devp);
}

void ivp_dev_hwa_enable(struct ivp_device *ivp_devp)
{
	if (!ivp_devp) {
		ivp_err("invalid input param ivp_devp");
		return;
	}
	/* enable apb gate clock, watdog, timer */
	ivp_info("ivp will enable hwa");
	ivp_reg_write(ivp_devp, IVP_REG_OFF_APB_GATE_CLOCK, IVP_APB_GATE_CLOCK_VAL);
	ivp_reg_write(ivp_devp, IVP_REG_OFF_TIMER_WDG_RST_DIS, IVP_TIMER_WDG_RST_DIS_VAL);
}

void ivp_hw_enable_reset(struct ivp_device *ivp_devp)
{
	if (!ivp_devp) {
		ivp_err("invalid input param ivp_devp");
		return;
	}
	ivp_reg_write(ivp_devp, IVP_REG_OFF_DSP_CORE_RESET_EN, RST_IVP32_PROCESSOR_EN);
	ivp_reg_write(ivp_devp, IVP_REG_OFF_DSP_CORE_RESET_EN, RST_IVP32_DEBUG_EN);
	ivp_reg_write(ivp_devp, IVP_REG_OFF_DSP_CORE_RESET_EN, RST_IVP32_JTAG_EN);
}

void ivp_set_qos_cfg(struct ivp_device *ivp_devp)
{
	if (!ivp_devp) {
		ivp_err("invalid input param ivp_devp");
		return;
	}
	ivp_reg_write(ivp_devp, IVP_REG_OFF_IVP_SYSTEM_QOS_CFG, IVP_SYS_QOS_CFG_VALUE);
	noc_ivp_reg_write(ivp_devp, IVP_CORE_RD_QOS_MODE, NOC_IVP_QOS_CFG_VALUE);
	noc_ivp_reg_write(ivp_devp, IVP_CORE_WR_QOS_MODE, NOC_IVP_QOS_CFG_VALUE);
	noc_ivp_reg_write(ivp_devp, IVP_IDMA_RD_QOS_MODE, NOC_IVP_QOS_CFG_VALUE);
	noc_ivp_reg_write(ivp_devp, IVP_IDMA_WR_QOS_MODE, NOC_IVP_QOS_CFG_VALUE);
	if (ivp_read_qos_cfg(ivp_devp) != 0)
		ivp_err("ivp_read_qos_cfg failed, may get a perf problem");
}

int ivp_read_qos_cfg(struct ivp_device *ivp_devp)
{
	int ret = 0;

	if (!ivp_devp) {
		ivp_err("invalid input param ivp_devp");
		return -EINVAL;
	}
	if (ivp_reg_read(ivp_devp, IVP_REG_OFF_IVP_SYSTEM_QOS_CFG) !=
		IVP_SYS_QOS_CFG_VALUE) {
		ivp_warn("IVP_REG_OFF_IVP_SYSTEM_QOS_CFG read back value:0x%x",
			ivp_reg_read(ivp_devp, IVP_REG_OFF_IVP_SYSTEM_QOS_CFG));
		ret += READ_BACK_IVP_SYS_QOS_CFG_ERROR;
	}

	if (noc_ivp_reg_read(ivp_devp, IVP_CORE_RD_QOS_MODE) !=
		NOC_IVP_QOS_CFG_VALUE) {
		ivp_warn("IVP_CORE_RD_QOS_MODE read back value:0x%x",
			noc_ivp_reg_read(ivp_devp, IVP_CORE_RD_QOS_MODE));
		ret += READ_BACK_IVP_CORE_RD_QOS_MODE_ERROR;
	}

	if (noc_ivp_reg_read(ivp_devp, IVP_CORE_WR_QOS_MODE) !=
		NOC_IVP_QOS_CFG_VALUE) {
		ivp_warn("IVP_CORE_WR_QOS_MODE read back value:0x%x",
			noc_ivp_reg_read(ivp_devp, IVP_CORE_WR_QOS_MODE));
		ret += READ_BACK_IVP_CORE_WR_QOS_MODE_ERROR;
	}

	if (noc_ivp_reg_read(ivp_devp, IVP_IDMA_RD_QOS_MODE) !=
		NOC_IVP_QOS_CFG_VALUE) {
		ivp_warn("IVP_IDMA_RD_QOS_MODE read back value:0x%x",
			noc_ivp_reg_read(ivp_devp, IVP_IDMA_RD_QOS_MODE));
		ret += READ_BACK_IVP_IDMA_RD_QOS_MODE_ERROR;
	}

	if (noc_ivp_reg_read(ivp_devp, IVP_IDMA_WR_QOS_MODE) !=
		NOC_IVP_QOS_CFG_VALUE) {
		ivp_warn("IVP_IDMA_WR_QOS_MODE read back value:0x%x",
			noc_ivp_reg_read(ivp_devp, IVP_IDMA_WR_QOS_MODE));
		ret += READ_BACK_IVP_IDMA_WR_QOS_MODE_ERROR;
	}

	return ret;
}
