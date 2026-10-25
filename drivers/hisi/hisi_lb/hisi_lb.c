/*
 * hisi_lb.c
 *
 * lb drv
 *
 * Copyright (c) 2018-2019 Huawei Technologies Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "hisi_lb.h"

#include <asm/compiler.h>
#include <asm/cacheflush.h>
#include <asm/memory.h>
#include <asm/tlbflush.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/memblock.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sizes.h>
#include <linux/smp.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/spinlock.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/page-flags.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/genalloc.h>
#include <linux/uaccess.h>
#include <linux/hisi/rdr_hisi_platform.h>
#include <dsm/dsm_pub.h>

#include "hisi_lb_priv.h"

#define HISI_LB_FID_VALUE          0xc500bc00u
#define HISI_LB_QUOTA_VALUE        0xa13584
#define HISI_LB_PLC_VALUE          0x4135a4
#define HISI_LB_EFUSE              0x435912f

#define GID_STATIC    BIT(0)
#define GID_DYNAMIC   BIT(1)
#define GID_NON_SEC   BIT(2)
#define GID_SEC       BIT(3)
#define GID_VM_STATE  BIT(4)
#define GID_BYPASS    BIT(5)

#define QUOTA_L_SHIFT 16
#define QUOTA_L_MASK  ((1U << QUOTA_L_SHIFT) - 1)
#define QUOTA_H_MASK  (~QUOTA_L_MASK)

#define DFX_CMD_TYPE_MASK BIT(12)
#define DFX_CMD_MID_SHIFT 4
#define DFX_CMD_MID_MASK  (0xff << DFX_CMD_MID_SHIFT)

#define LB_LARGIEST_COUNT         0x7000
#define LB_PAGE_FLAG              0x1
#define LB_PRIV_GID_WIDE          4
#define LB_PRIV_GID_MASK          (BIT(LB_PRIV_GID_WIDE) - 1)
#define FINISH_MASK               0x1
#define CMO_EVENT_INT_CLR_FLAG    BIT(19)
#define CMO_CFG_INT_VALID_BIT_EN  0x1f000
#define CMD_PARAM_SHIFT           16
#define ROUTE_INT_MASK            0xffff

#define MAX_NSEC_GID_NUM          8
#define GID_EN_SHIFT              11

#define GID_WAY_EN     0x3fff
#define GID_WAY_SRCH   0x3fff
#define LB_LINE_SHIFT  7

#define DFX_ECCERR_SINGLE_MASK     BIT(0)
#define DFX_ECCERR_MULTI_MASK      BIT(1)
#define DSM_LB_ECC_SINGLE_BIT      925205200
#define DSM_LB_ECC_MUTIL_BIT       925205201

#define SINGLE_BIT_EVENT_MAX   10
#define MUTIL_BIT_EVENT_MAX    10
#define INT_MASK               0xffffffff

#define CMO_OPERATE_MASK  0xf
#define CMO_BITMAP_MASK   0xffff
#define CMO_64B_PA_SHIFT  6
#define CMO_128B_PA_SHIFT 7
#define CMO_4K_PA_SHIFT   12
#define DFX_CMD_PA_SHIFT  2

struct lb_device *lbdev;
static struct dsm_client *ai_client;
static int single_bit;
static int mutil_bit;
static u32 cmo_dummy_pa;

static noinline u64 kernel_notify_atf(u64 fid, u32 spid, u32 quota, u32 plc)
{
	register u64 x0 asm("x0") = fid;
	register u64 x1 asm("x1") = spid;
	register u64 x2 asm("x2") = quota;
	register u64 x3 asm("x3") = plc;

	asm volatile (
		__asmeq("%0", "x0")
		__asmeq("%1", "x1")
		__asmeq("%2", "x2")
		__asmeq("%3", "x3")

		"smc    #0\n"
		: "+r" (x0)
		: "r" (x1), "r" (x2), "r"(x3));

	return x0;
}

static int power_control(u32 flag)
{
	int ret;

	ret = kernel_notify_atf(HISI_LB_FID_VALUE, flag,
			HISI_LB_QUOTA_VALUE, HISI_LB_PLC_VALUE);
	if (ret) {
		lb_print(LB_ERROR, "%s fail flag: 0x%x, ret: 0x%x\n",
			__func__, flag, ret);
		WARN_ON(1);
	}

	return ret;
}


static unsigned int get_lb_efuse(void)
{
	unsigned int ret;

	ret = kernel_notify_atf(HISI_LB_FID_VALUE, HISI_LB_EFUSE,
		HISI_LB_QUOTA_VALUE, HISI_LB_PLC_VALUE);

	return ret;
}

static inline int lb_pfn_valid_within(phys_addr_t phys)
{
	/* In the future, need handle invalid pfn */
	return 1;
}

void lb_cmo_sync_cmd_by_event(void)
{
	u32 status;
	int cpu_id;
	ktime_t timeout;
	cmd snyc;

	preempt_disable();
	cpu_id = smp_processor_id();
	snyc.value = 0;
	snyc.param.sync.opt = 1;
	snyc.param.sync.cmdt = 1;
	snyc.param.sync.rslt = cpu_id;
	snyc.param.sync.seq = 1;
	writeq(snyc.value, (lbdev->base + CMO_CMD));

	/* ensure cmo cmd complete */
	mb();

	timeout = ktime_add_us(ktime_get(), SC_POLL_TIMEOUT_US);
	while (!(readl(lbdev->base + SLC_ADDR4(CMO_STAT, cpu_id))
		& FINISH_MASK)) {
		if (ktime_compare(ktime_get(), timeout) > 0) {
			lb_print(LB_ERROR, "time out\n");
			break;
		}
		wfe();
	}

	writel(CMO_EVENT_INT_CLR_FLAG,
		(lbdev->base + SLC_ADDR4(CMO_CLEAR, cpu_id)));

	status = readl(lbdev->base + SLC_ADDR4(CMO_STAT, cpu_id));
	WARN_ON(status & FINISH_MASK);

	preempt_enable();
}

/*lint -e429*/
static int add_gid_vm(struct vm_info *vm, size_t sz)
{
	struct lb_area *vm_ar = NULL;

	vm_ar = kzalloc(sizeof(struct lb_area), GFP_KERNEL);
	if (!vm_ar) {
		lb_print(LB_ERROR, "%s kzalloc failed\n", __func__);
		goto err;
	}

	vm_ar->area = alloc_vm_area(sz, NULL);
	if (!vm_ar->area) {
		lb_print(LB_ERROR, "%s alloc vm area failed\n", __func__);
		goto free_vm_ar;
	}

	if (gen_pool_add(vm->pool, (uintptr_t)vm_ar->area->addr, sz, -1)) {
		lb_print(LB_ERROR, "%s add gen pool failed\n", __func__);
		goto free_area;
	}

	list_add_tail(&vm_ar->next, &vm->list);

	return 0;

free_area:
	free_vm_area(vm_ar->area);

free_vm_ar:
	kfree(vm_ar);

err:
	return -ENOMEM;
}
/*lint +e429*/

static int map_page_range(unsigned long va, size_t sz,
		pgprot_t prot, struct page **pages)
{
	int err;

	err = map_kernel_range_noflush(va, sz, prot, pages);
	flush_cache_vmap(va, va + sz);

	return err > 0 ? 0 : err;
}

static inline void free_pool(struct vm_info *vm,
		unsigned long va, size_t sz)
{
	gen_pool_free(vm->pool, va, sz);
}

static unsigned long alloc_pool(struct vm_info *vm, size_t sz)
{
	unsigned long va;

	/* try alloc pool, gen_pool_alloc have lock */
	va = gen_pool_alloc(vm->pool, sz);
	if (!va) {
		/* add gid vm need mutex lock */
		mutex_lock(&vm->mutex);

		va = gen_pool_alloc(vm->pool, sz);
		if (!va && !add_gid_vm(vm, SZ_64M))
			va = gen_pool_alloc(vm->pool, sz);

		mutex_unlock(&vm->mutex);
	}

	return va;
}

static void vm_unmap(struct vm_info *vm, const void *va, size_t sz)
{
	if (!vm) {
		lb_print(LB_ERROR, "vm not alloc\n");
		return;
	}

	unmap_kernel_range((uintptr_t)va, sz);
	free_pool(vm, (uintptr_t)va, sz);
}

static void *vm_map(struct vm_info *vm, struct page **pages,
		size_t cnt, pgprot_t prot)
{
	void *va = NULL;
	size_t sz = cnt << PAGE_SHIFT;

	if (!vm) {
		lb_print(LB_ERROR, "%s vm not alloc\n", __func__);
		return NULL;
	}

	va = (void *)alloc_pool(vm, sz);
	if (!va) {
		lb_print(LB_ERROR, "alloc pool failed\n");
		return NULL;
	}

	if (map_page_range((uintptr_t)va, sz, prot, pages)) {
		free_pool(vm, (uintptr_t)va, sz);
		lb_print(LB_ERROR, "map page range failed\n");
		return NULL;
	}

	return va;
}

static void vm_uninit(struct lb_device *lbd)
{
	int i;
	struct vm_info *vm = NULL;
	struct lb_area *vm_ar = NULL;
	struct lb_area *tmp_ar = NULL;

	for (i = 0; i < lbd->gdplc.nr; i++) {
		vm = lbd->gdplc.ptbl[i].vm;
		if (!vm)
			continue;

		list_for_each_entry_safe(vm_ar, tmp_ar, &vm->list, next) {
			list_del(&vm_ar->next);

			if (vm_ar->area)
				free_vm_area(vm_ar->area);

			kfree(vm_ar);
		}

		if (vm->pool)
			gen_pool_destroy(vm->pool);

		mutex_destroy(&vm->mutex);

		kfree(vm);
		lbd->gdplc.ptbl[i].vm = NULL;
	}
}

static int vm_init(struct lb_device *lbd)
{
	int i;
	struct vm_info *vm = NULL;

	for (i = 0; i < lbd->gdplc.nr; i++) {
		if (!(lbd->gdplc.ptbl[i].stat & GID_VM_STATE))
			continue;

		vm = kzalloc(sizeof(struct vm_info), GFP_KERNEL);
		if (!vm) {
			lb_print(LB_ERROR, "kzalloc failed\n");
			goto vm_free;
		}
		lbd->gdplc.ptbl[i].vm = vm;

		mutex_init(&(vm->mutex));
		INIT_LIST_HEAD(&(vm->list));

		vm->pool = gen_pool_create(PAGE_SHIFT, -1);
		if (!vm->pool) {
			lb_print(LB_ERROR, "gen pool create failed\n");
			goto vm_free;
		}

		if (add_gid_vm(vm, SZ_64M)) {
			lb_print(LB_ERROR, "add pool failed\n");
			goto vm_free;
		}
	}

	return 0;

vm_free:
	vm_uninit(lbd);

	return -ENOMEM;
}

static int send_cmd(cmd *cmo)
{
	writeq(cmo->value, (lbdev->base + CMO_CMD));
	return 0;
}

static int build_cmo_cmd(sync_type synct, ops_type ops,
		cmo_type by_x, u32 bitmap, u64 pa, size_t sz, u64 *value)
{
	int ret = 0;
	cmd cmo;

	cmo.value = 0;

	switch (by_x) {
	case CMO_BY_WAY:
		cmo.param.by_way.opt = ops & CMO_OPERATE_MASK;
		cmo.param.by_way.cmdt = by_x;
		cmo.param.by_way.way_bitmap = bitmap & CMO_BITMAP_MASK;
		break;

	case CMO_BY_GID:
		cmo.param.by_gid.opt = ops & CMO_OPERATE_MASK;
		cmo.param.by_gid.cmdt = by_x;
		cmo.param.by_gid.gid_bitmap = bitmap & CMO_BITMAP_MASK;
		break;

	case CMO_BY_64PA:
		cmo.param.by_64pa.opt = ops & CMO_OPERATE_MASK;
		cmo.param.by_64pa.cmdt = by_x;
		cmo.param.by_64pa.pa = pa >> CMO_64B_PA_SHIFT;
		if (!IS_ALIGNED(pa, SZ_64)) {
			lb_print(LB_ERROR, "pa is not align 64\n");
			ret = -EINVAL;
		}
		break;

	case CMO_BY_128PA:
		cmo.param.by_128pa.opt = ops & CMO_OPERATE_MASK;
		cmo.param.by_128pa.cmdt = by_x;
		cmo.param.by_128pa.pa = pa >> CMO_128B_PA_SHIFT;
		if (!IS_ALIGNED(pa, SZ_128)) {
			lb_print(LB_ERROR, "pa is not align 64\n");
			ret = -EINVAL;
		}
		break;
	case CMO_BY_4XKPA:
		cmo.param.by_4xkpa.opt = ops & CMO_OPERATE_MASK;
		cmo.param.by_4xkpa.cmdt = by_x;
		cmo.param.by_4xkpa.spa = (pa >> CMO_4K_PA_SHIFT);
		cmo.param.by_4xkpa.epa = ((pa + sz) >> CMO_4K_PA_SHIFT) - 1;
		if (!IS_ALIGNED(pa, PAGE_SIZE)
			|| !IS_ALIGNED(pa + sz, PAGE_SIZE)
			|| pa >= pa + sz) {
			WARN_ON(1);

			ret = -EINVAL;
		}
		break;

	default:
		lb_print(LB_ERROR, "invalid type %d\n", by_x);
		ret = -EINVAL;
		break;
	}

	*value = cmo.value;

	return ret;
}

int lb_ops_cache(sync_type synct, ops_type ops, cmo_type by_x,
	u32 bitmap, u64 pa, size_t sz)
{
	int ret;
	cmd cmo;

	if ((ops != INV && ops != CLEAN && ops != CI) || by_x > CMO_BY_4XKPA) {
		lb_print(LB_ERROR, "%d %d invalid para\n", ops, by_x);
		return -EINVAL;
	}

	cmo.value = 0;

	ret = build_cmo_cmd(synct, ops, by_x, bitmap, pa, sz, &cmo.value);
	if (ret)
		return ret;

	(void)send_cmd(&cmo);

	return ret;
}


void lb_invalid_cache(u64 pa, size_t sz)
{
	if (!lbdev) {
		lb_print(LB_ERROR, "lbdev is null\n");
		return;
	}

	if (!lbdev->power_state) {
		lb_print(LB_ERROR, "lb is powered down\n");
		return;
	}

	if (lb_ops_cache(EVENT, INV, CMO_BY_4XKPA, 0, pa, sz))
		return;

	if (cmo_dummy_pa && lb_ops_cache(EVENT, CLEAN, CMO_BY_4XKPA, 0,
		cmo_dummy_pa, PAGE_SIZE))
		return;

	/* ensure cmo ops complete */
	mb();

	lb_cmo_sync_cmd_by_event();
}

void lb_clean_cache(u64 pa, size_t sz)
{
	if (!lbdev) {
		lb_print(LB_ERROR, "lbdev is null\n");
		return;
	}

	if (lb_ops_cache(EVENT, CLEAN, CMO_BY_4XKPA, 0, pa, sz))
		return;

	if (cmo_dummy_pa && lb_ops_cache(EVENT, CLEAN, CMO_BY_4XKPA, 0,
		cmo_dummy_pa, PAGE_SIZE))
		return;

	/* ensure cmo ops complete */
	mb();

	lb_cmo_sync_cmd_by_event();
}

/*
 * assert at page to virt
 * assert at kmap
 * it is ok
 */
void __lb_assert_page(struct page *pg)
{
	if (!lbdev || !pg)
		return;

	if (PageLB(pg))
		rdr_syserr_process_for_ap(MODID_AP_S_PANIC_LB, 0ULL, 0ULL);
}

/* assert at page to virt */
void __lb_assert_phys(phys_addr_t phys)
{
	struct page *p = NULL;

	if (!lbdev)
		return;

	/* on owner platform valid check is not ok */
	if (!lb_pfn_valid_within(phys))
		return;

	p = phys_to_page(phys);
	if (p && PageLB(p)) {
		lb_print(LB_ERROR, "page flag 0x%lx\n", p->flags);
		rdr_syserr_process_for_ap(MODID_AP_S_PANIC_LB, 0ULL, 0ULL);
	}
}

static inline unsigned int page_to_gid(struct page *page)
{
	return (page->private & LB_PRIV_GID_MASK);
}

/*
 * assert at set pte
 * assert for page's gid is diff pte gid
 */
void __lb_assert_pte(pte_t pte)
{
}

static bool policy_id_valid(unsigned int policy_id, unsigned int mask)
{
	if (!lbdev) {
		lb_print(LB_ERROR, "lbdev is null\n");
		return false;
	}

	if (!policy_id || policy_id >= (unsigned int)lbdev->gdplc.nr) {
		lb_print(LB_ERROR, "policy_id%u is invalid\n", policy_id);
		return false;
	}

	if (!(lbdev->gdplc.ptbl[policy_id].stat & mask)) {
		lb_print(LB_ERROR, "policy_id%u mask%u is invalid\n",
			policy_id, mask);
		return false;
	}

	return true;
}

static unsigned int alloc_gid(struct lb_plc_info *policy)
{
	unsigned int i;

	if (policy->stat & GID_STATIC) {
		WARN_ON(!policy->gid);
		return policy->gid;
	}

	for (i = 1; i < MAX_NSEC_GID_NUM; i++) {
		if (!(lbdev->gdplc.gidmap & BIT(i))) {
			policy->gid = i;
			lbdev->gdplc.gidmap |= BIT(i);
			return policy->gid;
		}
	}

	return 0;
}

static void free_gid(struct lb_plc_info *policy)
{
	if (policy->stat & GID_STATIC)
		return;

	lbdev->gdplc.gidmap &= ~BIT(policy->gid);
	policy->gid = 0;
}

static int set_gid(struct lb_plc_info *policy, unsigned int way_alloc,
		unsigned int quota_set, unsigned int plc_set)
{
	/* lb need powerup when lb is powered down */
	if (!lbdev->power_state)
		if (power_control(POWERUP))
			return -EINVAL;

	writel(way_alloc, (lbdev->base +
				GID_ADDR100(GID_WAY_ALLC, policy->gid)));
	writel(quota_set, (lbdev->base + GID_ADDR100(GID_QUOTA, policy->gid)));
	writel(plc_set, (lbdev->base + GID_ADDR100(GID_ALLC_PLC, policy->gid)));

	/* ensure set gid cfg complete */
	mb();

	policy->enabled = 1;
	lbdev->power_state |= BIT(policy->gid);

	return 0;
}

static int reset_gid(struct lb_plc_info *policy)
{
	int ret = 0;

	lb_print(LB_ERROR, "%s: gid = %u\n", __func__, policy->gid);

	/* not doubel free same gid */
	if (!(lbdev->power_state & BIT(policy->gid))) {
		lb_print(LB_ERROR, "double free gid %u %u\n",
			lbdev->power_state, policy->gid);
		return -EINVAL;
	}

	writel(0, (lbdev->base + GID_ADDR100(GID_QUOTA, policy->gid)));
	writel(0, (lbdev->base + GID_ADDR100(GID_WAY_ALLC, policy->gid)));
	writel(0, (lbdev->base + GID_ADDR100(GID_ALLC_PLC, policy->gid)));

	/* ensure set gid cfg complete */
	mb();

	policy->enabled = 0;
	lbdev->power_state &= ~BIT(policy->gid);

	/* lb need powerdown when all gid is disable */
	if (!lbdev->power_state)
		ret = power_control(POWERDOWN);

	return ret;
}

static int set_page(struct lb_plc_info *policy,
		struct page *page_alloc, size_t count)
{
	void *vaddr = NULL;
	struct page *pg = page_alloc;
	struct page **pages = NULL;
	struct page **tmp = NULL;
	int i;


	/*
	 * invalid acpu cache by normal va
	 * it is necessary by normal va
	 * cache |--va--| |--va with gid--|
	 * lb             |-------lb------|
	 * ddr   |------|
	 */
	__dma_unmap_area(page_to_virt(pg), count << PAGE_SHIFT,
			DMA_FROM_DEVICE);

	pages = kcalloc(count, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		lb_print(LB_ERROR, "zalloc page\n");
		goto change_mapping;
	}

	tmp = pages;
	for (i = 0; i < (int)count; i++)
		*(tmp++) = pg++;

	vaddr = vm_map(policy->vm, pages, count,
			pgprot_lb(PAGE_KERNEL, policy->gid));
	if (!vaddr) {
		lb_print(LB_ERROR, "%s:vm map failed\n", __func__);
		goto free_page;
	}

	kfree(pages);

	/* set the page privite */
	pg = page_alloc;

	for (i = 0; i < (int)count; i++) {
		set_page_private(pg, (uintptr_t)vaddr | policy->gid);
		SetPagePrivate(pg);
		SetPageLB(pg);
		pg++;
		vaddr += PAGE_SIZE;
	}

	return 0;

free_page:
	kfree(pages);

change_mapping:

	return -ENOMEM;
}

static void reset_page(struct lb_plc_info *policy,
		struct page *page, size_t count)
{
	int i;
	void *vaddr = NULL;
	struct page *pg = page;
	phys_addr_t phys = page_to_phys(pg);
	unsigned long size = count << PAGE_SHIFT;

	WARN_ON(!PageLB(pg));
	WARN(policy->gid != lb_page_to_gid(pg),
		"pgid %u ggid %u\n",
		policy->gid, lb_page_to_gid(pg));


	/*
	 * invalid acpu cache by lb va
	 * it is necessary by lb va
	 * cache |--va--| |--va with gid--|
	 * lb             |-------lb------|
	 * ddr   |------|
	 */
	vaddr = lb_page_to_virt(page);
	__dma_unmap_area(vaddr, size, DMA_FROM_DEVICE);

	vm_unmap(policy->vm, vaddr, size);

	for (i = 0; i < (int)count; i++) {
		set_page_private(pg, 0);
		ClearPagePrivate(pg);
		ClearPageLB(pg);
		pg++;
	}

	/* invalid system cache */
	lb_invalid_cache(phys, size);
}

unsigned int search_quota(struct lb_plc_info *policy, unsigned int quota)
{
	return 0;
}

static unsigned int alloc_quota(struct lb_plc_info *policy, unsigned int quota)
{
	return policy->quota;
}

static void free_quota(struct lb_plc_info *policy, unsigned int quota)
{
	/*
	 * Current quota static alloc
	 * In the future, need dynamic free quota
	 */
}

static inline unsigned int __is_lb_available(void)
{
	return (unsigned int)(lbdev && lbdev->is_available);
}

unsigned int is_lb_available(void)
{
	return __is_lb_available();
}

void *lb_page_to_virt(struct page *page)
{
	unsigned long virt;

	if (!__is_lb_available() || !page)
		return NULL;

	WARN(!PageLB(page), "fail pagelb");
	WARN(!PagePrivate(page), "fail pageprivate");
	virt = page->private & (~LB_PRIV_GID_MASK);

	return (void *)virt;
}
EXPORT_SYMBOL(lb_page_to_virt);

u64 lb_pid_to_gidphys(u32 pid)
{
	if (!__is_lb_available())
		return 0;

	if (!policy_id_valid(pid, GID_NON_SEC)) {
		lb_print(LB_ERROR, "0x%x: pid invalid\n", pid);
		return 0;
	}

	return PTE_LB(lbdev->gdplc.ptbl[pid].gid);
}
EXPORT_SYMBOL(lb_pid_to_gidphys);

unsigned int lb_page_to_gid(struct page *page)
{
	unsigned int gid_idx;

	if (!__is_lb_available() || !page || !PageLB(page))
		return 0;

	WARN(!PageLB(page), "fail pagelb");
	WARN(!PagePrivate(page), "fail pageprivate");
	gid_idx = page->private & LB_PRIV_GID_MASK;

	return gid_idx;
}
EXPORT_SYMBOL(lb_page_to_gid);

/*
 * this API is called by HIFI, Tiny, GPU
 * size is not used
 */
int lb_request_quota(unsigned int pid)
{
	int ret = 0;
	unsigned long flags = 0;
	struct lb_plc_info *policy = NULL;

	lb_print(LB_INFO, "into%s\n", __func__);

	if (!__is_lb_available())
		return 0;

	if (!policy_id_valid(pid, GID_NON_SEC | GID_SEC)) {
		lb_print(LB_ERROR, "0x%x: pid invalid\n", pid);
		ret = -ENOMEM;
		goto err;
	}

	spin_lock_irqsave(&lbdev->gdplc.lock, flags);
	policy = &lbdev->gdplc.ptbl[pid];
	if (likely(policy->stat & GID_SEC))
		ret = set_gid(policy, 0, 0, BIT(GID_EN_SHIFT));
	else if (policy->enabled)
		writel(policy->r_quota, lbdev->base +
					GID_ADDR100(GID_QUOTA, policy->gid));

	spin_unlock_irqrestore(&lbdev->gdplc.lock, flags);

err:
	lb_print(LB_INFO, "out%s\n", __func__);
	return ret;
}
EXPORT_SYMBOL(lb_request_quota);

int lb_release_quota(unsigned int pid)
{
	int ret = 0;
	unsigned long flags = 0;
	struct lb_plc_info *policy = NULL;

	lb_print(LB_INFO, "into%s\n", __func__);

	if (!__is_lb_available())
		return 0;

	if (!policy_id_valid(pid, GID_NON_SEC | GID_SEC)) {
		lb_print(LB_ERROR, "%u: pid invalid\n", pid);
		ret = -EINVAL;
		goto err;
	}

	spin_lock_irqsave(&lbdev->gdplc.lock, flags);
	policy = &lbdev->gdplc.ptbl[pid];
	if (likely(policy->stat & GID_SEC)) {
		ret = reset_gid(policy);
	} else if (policy->enabled) {
		policy->r_quota = readl(lbdev->base +
				GID_ADDR100(GID_QUOTA, policy->gid));
		writel(0, (lbdev->base + GID_ADDR100(GID_QUOTA, policy->gid)));
	}

	spin_unlock_irqrestore(&lbdev->gdplc.lock, flags);

err:
	return ret;
}
EXPORT_SYMBOL(lb_release_quota);

/* when call it set pte for cpu/iommu/gpu */
unsigned long lb_pte_attr(phys_addr_t phy_addr)
{
	unsigned long gid_idx;
	struct page *pg = NULL;

	if (!__is_lb_available())
		return 0;

	/*
	 * pfn illegal judgment affects efficiency
	 * the current only gpu use lb_pte_attr
	 * gpu input a valid addree
	 */
	pg = phys_to_page(phy_addr);
	if (!PageLB(pg))
		return 0;

	gid_idx = lb_page_to_gid(pg);

	return pgprot_val(PAGE_LB_CACHEABLE(gid_idx));
}
EXPORT_SYMBOL(lb_pte_attr);

/* when call it @ dma XXX function */
int lb_prot_build(struct page *pages, pgprot_t *pageprot)
{
	unsigned long gid_idx;

	if (!__is_lb_available() || !pages || !pageprot || !PageLB(pages))
		return 0;

	gid_idx = lb_page_to_gid(pages);
	*pageprot = pgprot_lb(*pageprot, gid_idx);

	return 0;
}
EXPORT_SYMBOL(lb_prot_build);

/*
 * when call it @ ion
 * @ map to cpu user/kernel space
 */
int lb_pid_prot_build(unsigned int pid, pgprot_t *pageprot)
{
	struct lb_plc_info *policy = NULL;

	if (!__is_lb_available())
		return 0;

	if (!pageprot)
		return -EINVAL;

	if (!policy_id_valid(pid, GID_NON_SEC)) {
		lb_print(LB_ERROR, "%s:policy_id invalid\n", __func__);
		return -EINVAL;
	}

	policy = &lbdev->gdplc.ptbl[pid];
	*pageprot = pgprot_lb(*pageprot, policy->gid);

	return 0;
}
EXPORT_SYMBOL(lb_pid_prot_build);

int lb_pages_attach(unsigned int pid, struct page *pg, size_t count)
{
	int ret;
	unsigned long flags = 0;
	gid quota_set;
	gid allc_set;
	unsigned int quota = 0;
	struct lb_plc_info *policy = NULL;

	lb_print(LB_INFO, "into%s\n", __func__);

	if (!__is_lb_available())
		return 0;

	if (!pg) {
		lb_print(LB_ERROR, "%s:pages is  invalid\n", __func__);
		goto err;
	}

	if (!policy_id_valid(pid, GID_NON_SEC)) {
		lb_print(LB_ERROR, "%s:policy_id invalid\n", __func__);
		goto err;
	}

	/* protect gid map and quota */
	spin_lock_irqsave(&lbdev->gdplc.lock, flags);
	policy = &lbdev->gdplc.ptbl[pid];
	if (!policy->page_count) {
		policy->gid = alloc_gid(policy);
		quota = alloc_quota(policy,
			count << (PAGE_SHIFT - LB_LINE_SHIFT));

		DEFINE_INIT_QUOTA(quota_set, (quota & QUOTA_L_MASK),
			(quota & QUOTA_H_MASK) >> QUOTA_L_SHIFT);
		DEFINE_INIT_ALLOC(allc_set, GID_WAY_EN, GID_WAY_SRCH);

		if (set_gid(policy, allc_set.value, quota_set.value,
			policy->plc))
			goto err_set_gid;
	}
	policy->page_count += count;
	spin_unlock_irqrestore(&lbdev->gdplc.lock, flags);

	/* set pages */
	ret = set_page(policy, pg, count);
	if (ret) {
		lb_print(LB_ERROR, "%s: set page failed\n", __func__);
		goto err_set_page;
	}

	lb_print(LB_INFO, "out%s\n", __func__);

	return 0;

err_set_page:
	spin_lock_irqsave(&lbdev->gdplc.lock, flags);

	if (WARN_ON(policy->page_count < count))
		policy->page_count = 0;
	else
		policy->page_count -= count;

	if (!policy->page_count) {
		reset_gid(policy);
		free_quota(policy, quota);
		free_gid(policy);
	}

	spin_unlock_irqrestore(&lbdev->gdplc.lock, flags);

err:
	return -EINVAL;

err_set_gid:
	free_quota(policy, quota);
	free_gid(policy);
	spin_unlock_irqrestore(&lbdev->gdplc.lock, flags);
	return -EINVAL;
}
EXPORT_SYMBOL(lb_pages_attach);

int lb_pages_detach(unsigned int pid, struct page *pages, size_t count)
{
	struct lb_plc_info *policy = NULL;
	unsigned long flags = 0;

	lb_print(LB_INFO, "into%s\n", __func__);

	if (!__is_lb_available())
		return 0;

	if (!pages) {
		lb_print(LB_ERROR, "%s:pages is  invalid\n", __func__);
		return -EINVAL;
	}

	if (!policy_id_valid(pid, GID_NON_SEC)) {
		lb_print(LB_ERROR, "%s:policy_id invalid\n", __func__);
		return -EINVAL;
	}

	policy = &lbdev->gdplc.ptbl[pid];
	reset_page(policy, pages, count);

	spin_lock_irqsave(&lbdev->gdplc.lock, flags);

	if (WARN_ON(policy->page_count < count))
		policy->page_count = 0;
	else
		policy->page_count -= count;

	if (!policy->page_count) {
		reset_gid(policy);
		free_quota(policy, 0);
		free_gid(policy);
	}

	spin_unlock_irqrestore(&lbdev->gdplc.lock, flags);

	lb_print(LB_INFO, "out%s\n", __func__);

	return 0;
}
EXPORT_SYMBOL(lb_pages_detach);

int lb_sg_attach(unsigned int pid, struct scatterlist *sgl, unsigned int nents)
{
	int i, j;
	struct scatterlist *sg = NULL;

	lb_print(LB_INFO, "into %s pid %u\n", __func__, pid);

	/* for gpu pid is normal call path */
	if (!__is_lb_available() || !pid)
		return 0;

	if (!sgl)
		return -EINVAL;

	for_each_sg(sgl, sg, (int)nents, i) {
		if (!sg || lb_pages_attach(pid, phys_to_page(sg_phys(sg)),
			sg->length >> PAGE_SHIFT))
			goto err;
	}

	lb_print(LB_INFO, "out %s\n", __func__);

	return 0;

err:
	for_each_sg(sgl, sg, i, j) {
		if (!sg || lb_pages_detach(pid, phys_to_page(sg_phys(sg)),
			sg->length >> PAGE_SHIFT))
			lb_print(LB_ERROR, "%s page detach failed\n", __func__);
	}

	return -EINVAL;
}
EXPORT_SYMBOL(lb_sg_attach);

int lb_sg_detach(unsigned int pid, struct scatterlist *sgl, unsigned int nents)
{
	int i;
	struct scatterlist *sg = NULL;

	lb_print(LB_INFO, "into %s\n", __func__);

	/* for gpu pid is normal call path */
	if (!__is_lb_available() || !pid)
		return 0;

	if (!sgl && sg_nents(sgl) != nents)
		return -EINVAL;

	for_each_sg(sgl, sg, (int)nents, i)
		if (lb_pages_detach(pid, phys_to_page(sg_phys(sg)),
			sg->length >> PAGE_SHIFT))
			lb_print(LB_ERROR, "%s page detach failed\n", __func__);

	lb_print(LB_INFO, "out %s\n", __func__);

	return 0;
}
EXPORT_SYMBOL(lb_sg_detach);

struct page *lb_alloc_pages(unsigned int pid, gfp_t gfp_mask,
		unsigned int order)
{
	struct page *lb_page = NULL;

	/*
	 * alloc pages
	 * pid = 0 for GPU
	 */
	lb_page = alloc_pages(gfp_mask, order);
	if (!lb_page) {
		lb_print(LB_ERROR, "%u:alloc pages failed\n", pid);
		goto err;
	}

	if (!__is_lb_available() || !pid)
		return lb_page;

	if (lb_pages_attach(pid, lb_page, 1ULL << order)) {
		lb_print(LB_ERROR, "%u:lb pages attach failed\n", pid);
		goto err;
	}

	return lb_page;

err:
	if (lb_page)
		__free_pages(lb_page, order);
	return NULL;
}
EXPORT_SYMBOL(lb_alloc_pages);

int lb_free_pages(unsigned int pid, struct page *pages, unsigned int order)
{
	if (!__is_lb_available() || !pid)
		goto succ;

	if (lb_pages_detach(pid, pages, 1UL << order)) {
		lb_print(LB_ERROR, "%s:lb pages detach failed\n", __func__);
		goto err_nofree;
	}

succ:
	if (pages)
		__free_pages(pages, order);
	return 0;

err_nofree:
	return -EINVAL;
}
EXPORT_SYMBOL(lb_free_pages);


static int gid_to_pid(unsigned int id)
{
	int i;

	for (i = 0; i < lbdev->gdplc.nr; i++) {
		if (lbdev->gdplc.ptbl[i].gid == id)
			return i;
	}

	return 0;
}

static void parse_dfx_err_info(uint32_t slice_idx, uint32_t err_inf1,
	uint32_t err_inf2)
{
	unsigned int master_id;
	unsigned int id;
	unsigned int pid;

	lb_print(LB_ERROR, "SLC_DFX_ERR_1 slice%u:0x%x\n", slice_idx, err_inf1);
	lb_print(LB_ERROR, "SLC_DFX_ERR_2 slice%u:0x%x\n", slice_idx, err_inf2);

	lb_print(LB_ERROR, "dfx_fail_cmd_pa:0x%x\n",
		err_inf1 << DFX_CMD_PA_SHIFT);

	if (err_inf2 & DFX_CMD_TYPE_MASK)
		lb_print(LB_ERROR, "dfx_fail_cmd_type is write\n");
	else
		lb_print(LB_ERROR, "dfx_fail_cmd_type is read\n");

	master_id = (err_inf2 & DFX_CMD_MID_MASK) >> DFX_CMD_MID_SHIFT;
	lb_print(LB_ERROR, "Master id is 0x%x\n", master_id);

	id = err_inf2 & 0xf;
	lb_print(LB_ERROR, "dfx_fail_cmd_gid:%u\n", id);

	pid = gid_to_pid(id);
	if (!pid)
		return;

	if (lbdev->gdplc.ptbl[pid].stat & GID_NON_SEC) {
		lb_print(LB_ERROR, "GID_WAY_ALLC-gid%u:0x%x\n", id,
			readl(lbdev->base + GID_ADDR100(GID_WAY_ALLC, id)));
		lb_print(LB_ERROR, "GID_QUOTA-gid%u:0x%x\n", id,
			readl(lbdev->base + GID_ADDR100(GID_QUOTA, id)));
		lb_print(LB_ERROR, "GID_ALLC_PLC-gid%u:0x%x\n", id,
			readl(lbdev->base + GID_ADDR100(GID_ALLC_PLC, id)));
	}
}

static void lb_ecc_info(uint32_t info)
{
	char client_name[] = "dsm_ai";

	if (ai_client == NULL) {
		ai_client = dsm_find_client((char *)client_name);
		if (ai_client == NULL)
			return;
	}

	if ((info & DFX_ECCERR_MULTI_MASK) &&
		(mutil_bit < MUTIL_BIT_EVENT_MAX)) {
		if (!dsm_client_ocuppy(ai_client)) {
			dsm_client_record(ai_client, "lb ecc mutil bit\n");
			dsm_client_notify(ai_client, DSM_LB_ECC_MUTIL_BIT);
			mutil_bit++;
		}
	} else if ((info & DFX_ECCERR_SINGLE_MASK) &&
		(single_bit < SINGLE_BIT_EVENT_MAX)) {
		if (!dsm_client_ocuppy(ai_client)) {
			dsm_client_record(ai_client, "lb ecc 1bit\n");
			dsm_client_notify(ai_client, DSM_LB_ECC_SINGLE_BIT);
			single_bit++;
		}
	}
}

static irqreturn_t lb_dfx_handle(int irq, void *lbd)
{
	int i;
	struct lb_device *d = lbd;
	uint32_t err_inf1, err_inf2, ecc_inf;

	lb_print(LB_ERROR, "into %s\n", __func__);

	/* disable int */
	writel(0, (d->base + CMO_CFG_INT_EN));
	for (i = 0; i < SLC_IDX; i += SLC_STEP) {
		writel(0, (d->base + SLC_ADDR2000(SLC02_INT_EN, i)));
		writel(0, (d->base + SLC_ADDR2000(SLC13_INT_EN, (i + 1))));
	}

	/* read the int status */
	lb_print(LB_ERROR, "glb interrupt 0x%x",
		readl(d->base + CFG_INT_STATUS));

	lb_print(LB_ERROR, "CMO interrupt 0x%x",
		readl(d->base + CMO_CFG_INT_INI));
	for (i = 0; i < SLC_IDX; i += SLC_STEP) {
		lb_print(LB_ERROR, "slc interrupt 0x%x",
			readl(d->base + SLC_ADDR2000(SLC02_INT_INI, i)));
		lb_print(LB_ERROR, "slc interrupt 0x%x",
			readl(d->base + SLC_ADDR2000(SLC13_INT_INI, (i + 1))));
	}

	/* read ecc info */
	for (i = 0; i < SLC_IDX; i++) {
		ecc_inf = readl(d->base + SLC_ADDR2000(SLC_DFX_ECCERR, i));
		lb_print(LB_ERROR, "ecc info: 0x%x", ecc_inf);
		lb_ecc_info(ecc_inf);
	}

	/* clear the int */
	for (i = 0; i < SLC_IDX; i += SLC_STEP) {
		writel(INT_MASK,
			(d->base + SLC_ADDR2000(SLC02_INT_CLEAR, i)));
		writel(INT_MASK,
			(d->base + SLC_ADDR2000(SLC13_INT_CLEAR, (i + 1))));
	}
	writel(INT_MASK, (d->base + CMO_CFG_INT_CLR));

	/* enable int */
	for (i = 0; i < SLC_IDX; i += SLC_STEP) {
		writel(INT_MASK, (d->base + SLC_ADDR2000(SLC02_INT_EN, i)));
		writel(INT_MASK,
			(d->base + SLC_ADDR2000(SLC13_INT_EN, (i + 1))));
	}

	writel(INT_MASK, (d->base + CMO_CFG_INT_EN));

	for (i = 0; i < SLC_IDX; i++) {
		err_inf1 = readl(d->base + SLC_ADDR2000(SLC_DFX_ERR_INF1, i));
		err_inf2 = readl(d->base + SLC_ADDR2000(SLC_DFX_ERR_INF2, i));
		if (err_inf1 || err_inf2)
			parse_dfx_err_info(i, err_inf1, err_inf2);
	}

	WARN_ON(1);


	lb_print(LB_ERROR, "out %s\n", __func__);

	return 0;
}

static irqreturn_t lb_cmd_handle(int irq, void *lbd)
{
	struct lb_device *d = lbd;

	lb_print(LB_INFO, "into %s\n", __func__);
	if (!d) {
		lb_print(LB_ERROR, "lbd is null\n");
		return -EINVAL;
	}

	lb_print(LB_INFO, "out %s\n", __func__);
	return 0;
}

static int get_policy_pdata(struct platform_device *pdev, struct lb_device *lbd)
{
	int i = 0;
	u32 prot;
	int ret;
	const char *plc_name = NULL;

	struct device_node *np = NULL;
	struct lb_plc_info *p_data = NULL;
	const struct device_node *dt_node = pdev->dev.of_node;

	lb_print(LB_INFO, "into %s\n", __func__);

	ret = of_property_read_u32(dt_node, "cmo-dummy-pa", &prot);
	if (ret >= 0)
		cmo_dummy_pa = prot;

	/* if policy id is add, so plc-num also need be add */
	ret = of_property_read_u32(dt_node, "plc-num", &prot);
	if (ret < 0) {
		lb_print(LB_ERROR, "can not get plc number\n");
		return -EINVAL;
	}
	lbd->gdplc.nr = prot;

	p_data = devm_kzalloc(&pdev->dev, sizeof(*p_data) * prot,
				  GFP_KERNEL);
	if (!p_data) {
		lb_print(LB_ERROR, "get dfx irq failed\n");
		return -ENOMEM;
	}
	lbd->gdplc.ptbl = p_data;

	for_each_child_of_node(dt_node, np) {
		ret = of_property_read_string(np, "lb-name", &plc_name);
		if (ret < 0)
			continue;
		lbd->gdplc.ptbl[i].name = plc_name;

		ret = of_property_read_u32(np, "lb-pid", &prot);
		if (ret < 0)
			continue;

		if (i != prot)
			rdr_syserr_process_for_ap(MODID_AP_S_PANIC_LB,
					0ULL, 0ULL);

		lbd->gdplc.ptbl[i].pid = prot;

		ret = of_property_read_u32(np, "lb-prio", &prot);
		if (ret < 0)
			continue;

		lbd->gdplc.ptbl[i].prio = prot;

		ret = of_property_read_u32(np, "lb-gid", &prot);
		if (ret < 0)
			continue;

		lbd->gdplc.ptbl[i].gid = prot;

		ret = of_property_read_u32(np, "lb-stat", &prot);
		if (ret < 0)
			continue;

		lbd->gdplc.ptbl[i].stat = prot;

		ret = of_property_read_u32(np, "lb-quota", &prot);
		if (ret < 0)
			continue;

		lbd->gdplc.ptbl[i].quota = prot;

		ret = of_property_read_u32(np, "lb-alloc", &prot);
		if (ret < 0)
			continue;

		lbd->gdplc.ptbl[i].alloc = prot;

		ret = of_property_read_u32(np, "lb-plc", &prot);
		if (ret < 0)
			continue;

		lbd->gdplc.ptbl[i].plc = prot;

		i++;
	}

	if (i != lbd->gdplc.nr)
		rdr_syserr_process_for_ap(MODID_AP_S_PANIC_LB, 0ULL, 0ULL);

	lb_print(LB_INFO, "out(%s)\n", __func__);

	return ret;
}

static int reset_ip(struct lb_device *lbd)
{
	int i;
	int ret;

	/* disable int */
	writel(0, (lbd->base + CMO_CFG_INT_EN));
	for (i = 0; i < SLC_IDX; i += SLC_STEP) {
		writel(0, (lbd->base + SLC_ADDR2000(SLC02_INT_EN, i)));
		writel(0, (lbd->base + SLC_ADDR2000(SLC13_INT_EN, (i + 1))));
	}

	/* clear the int */
	for (i = 0; i < SLC_IDX; i += SLC_STEP) {
		writel(INT_MASK,
			(lbd->base + SLC_ADDR2000(SLC02_INT_CLEAR, i)));
		writel(INT_MASK,
			(lbd->base + SLC_ADDR2000(SLC13_INT_CLEAR, (i + 1))));
	}

	writel(INT_MASK, (lbd->base + CMO_CFG_INT_CLR));

	ret = devm_request_threaded_irq(lbd->dev, lbd->dfx_irq, lb_dfx_handle,
		NULL, IRQF_TRIGGER_RISING, "dfx-intr", lbd);
	if (ret) {
		lb_print(LB_ERROR, "failed to enable dfx irq\n");
		return -EINVAL;
	}
	ret = devm_request_threaded_irq(lbd->dev, lbd->cmd_irq, lb_cmd_handle,
		NULL, IRQF_TRIGGER_RISING, "cmd-intr", lbd);
	if (ret) {
		lb_print(LB_ERROR, "failed to enable cmd irq\n");
		return -EINVAL;
	}

	/* enable int */
	writel(CMO_CFG_INT_VALID_BIT_EN, (lbd->base + CMO_CFG_INT_EN));
	for (i = 0; i < SLC_IDX; i += SLC_STEP) {
		writel(INT_MASK, (lbd->base + SLC_ADDR2000(SLC02_INT_EN, i)));
		writel(INT_MASK,
			(lbd->base + SLC_ADDR2000(SLC13_INT_EN, (i + 1))));
	}

	return 0;
}

static int soc_init(struct platform_device *pdev, struct lb_device *lbd)
{
	int irq;

	lb_print(LB_INFO, "into %s\n", __func__);

	/* register intr */
	irq = platform_get_irq_byname(pdev, "dfx-intr");
	if (irq < 0) {
		lb_print(LB_ERROR, "get dfx irq failed\n");
		return -EINVAL;
	}
	lbd->dfx_irq = irq;

	irq = platform_get_irq_byname(pdev, "cmd-intr");
	if (irq < 0) {
		lb_print(LB_ERROR, "get cmd-intr failed\n");
		return -EINVAL;
	}
	lbd->cmd_irq = irq;

	/* enable intr reg */
	if (reset_ip(lbd)) {
		lb_print(LB_ERROR, "reset ip failed\n");
		return -EINVAL;
	}

	lb_print(LB_INFO, "out %s\n", __func__);

	return 0;
}

static void gidmap_init(struct lb_device *lbd)
{
	int i;

	for (i = 0; i < lbd->gdplc.nr; i++) {
		if (!(lbd->gdplc.ptbl[i].stat & GID_STATIC))
			continue;

		lbd->gdplc.gidmap |= BIT(lbd->gdplc.ptbl[i].gid);
	}
}

static int gid_init(struct platform_device *pdev, struct lb_device *lbd)
{
	int ret;

	lb_print(LB_INFO, "into %s\n", __func__);

	spin_lock_init(&lbd->gdplc.lock);

	/* get the gid set from dts */
	ret = get_policy_pdata(pdev, lbd);
	if (ret) {
		lb_print(LB_ERROR, "get git set from dts failed\n");
		return ret;
	}

	gidmap_init(lbd);

	if (vm_init(lbd)) {
		lb_print(LB_ERROR, "vm init failed\n");
		return -EINVAL;
	}

	lb_print(LB_INFO, "out %s\n", __func__);

	return 0;
}

static int power_init(struct platform_device *pdev, struct lb_device *lbd)
{
	/* lb power init */
	return 0;
}

static int lb_probe(struct platform_device *pdev)
{
	struct resource *res = NULL;
	struct lb_device *lb_dev = NULL;
	struct device *dev = &pdev->dev;

	lb_print(LB_INFO, "into %s\n", __func__);

	lb_dev = devm_kzalloc(dev, sizeof(*lb_dev), GFP_KERNEL);
	if (!lb_dev) {
		lb_print(LB_ERROR, "failed to allocate lb_dev\n");
		return -ENOMEM;
	}
	lb_dev->dev = dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		lb_print(LB_ERROR, "get resource is failed\n");
		return -EINVAL;/*lint !e429 */
	}

	lb_dev->base = devm_ioremap_resource(dev, res);
	if (!lb_dev->base) {
		lb_print(LB_ERROR, "io remap failed\n");
		return -EINVAL;/*lint !e429 */
	}

	lb_dev->is_available = !get_lb_efuse();
	if (!lb_dev->is_available) {
		lb_print(LB_ERROR, "lb unusable\n");
		lbdev = lb_dev;
		return 0;
	}

	if (soc_init(pdev, lb_dev)) {
		lb_print(LB_ERROR, "lb init failed\n");
		return -EINVAL;
	}

	if (gid_init(pdev, lb_dev)) {
		lb_print(LB_ERROR, "gid init failed\n");
		return -EINVAL;
	}

	if (power_init(pdev, lb_dev)) {
		lb_print(LB_ERROR, "power init failed\n");
		return -EINVAL;
	}

	if (lb_pmu_init(pdev, lb_dev)) {
		lb_print(LB_ERROR, "pmu init failed\n");
		return -EINVAL;
	}

	/* register to loacal device */
	lbdev = lb_dev;

	/*
	 * need lb power down
	 * because lb powerup when atf init
	 */
	if (power_control(POWERDOWN)) {
		lb_print(LB_ERROR, "powerdown failed\n");
		return -EINVAL;
	}

	lb_print(LB_INFO, "out %s\n", __func__);

	return 0;
}

static int lb_remove(struct platform_device *pdev)
{
	lbdev = NULL;
	return 0;
}

static const struct of_device_id lb_match_table[] = {
	{.compatible = "hisilicon,hisi-lb" },
	{},
};

static struct platform_driver lb_driver = {
	.probe = lb_probe,
	.remove = lb_remove,
	.driver = {
		.name = "lb-hisi",
		.owner = THIS_MODULE,
		.of_match_table = lb_match_table,
	},
};

static int lb_init_ns(void)
{
	return platform_driver_register(&lb_driver);
}
subsys_initcall(lb_init_ns);
module_param_named(debug_level, lb_d_lvl, uint, 0644);
