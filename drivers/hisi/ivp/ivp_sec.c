/*
 * * Copyright (c) Huawei Technologies Co., Ltd. 2012-2020. All rights reserved.
 * * Description: this file implements ivp sec function and
 * * use macro LINUX_VERSION_CODE
 * * to distinguish different kernel version
 * * Create: 2019-02-18
 */
#include "ivp_sec.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/ion.h>
#include <linux/mutex.h>
#include <linux/hisi/hisi_ion.h>
#include <linux/hisi-iommu.h>
#include <linux/hisi/hisi_load_image.h>
#include <linux/kthread.h>
#include <linux/cpumask.h>
#include <linux/sched.h>
#include <linux/jiffies.h>
#include <linux/completion.h>
#include <linux/syscalls.h>
#include <teek_client_id.h>
#include "securec.h"
#include "ivp_log.h"
#include "ivp_manager.h"
#include "ivp_platform.h"
#define SEC_NUM_SHIFT  16
#define CPU_NO_MIN     4
#define CPU_NO_MAX     8

struct mutex ivp_sec_mem_mutex;

#ifdef SEC_IVP_ENABLE
static int ivp_sec_load_and_verify_image(struct ivp_device *pdev)
{
#if defined(CONFIG_HISI_SECBOOT_IMG) || defined(CONFIG_HISI_SECBOOT_IMG_V2)
	int ret;
	struct load_image_info loadinfo;
	struct ivp_sec_device *dev = pdev->sec_dev;
	ivp_info("begin to load sec ivp image\n!");

	loadinfo.image_addr     = pdev->sec_dev->ivp_sec_phymem_addr;
	loadinfo.ecoretype      = IVP;
	loadinfo.image_size     = IVP_SEC_BUFF_SIZE;
	loadinfo.partion_name   = "ivp";

	ivp_info("begin to load sec ivp image\n!");
	ivp_dbg("addr:0x%lx, image_size:0x%x", loadinfo.image_addr, loadinfo.image_size);
	ret = bsp_load_and_verify_image(&loadinfo);
	if (ret < 0) {
		ivp_err("Failed : bsp_load_and_verify_image.%d\n", ret);
		return ret;
	}

	atomic_set(&dev->ivp_image_success, 1);
	ivp_info("ivp_sec_loadimage success!");
	return EOK;
#else
	ivp_err("not support sec ivp!");
	return -EINVAL;
#endif
}
#else
static int ivp_sec_load_and_verify_image(struct ivp_device *pdev)
{
	return EOK;
}
#endif

int ivp_trans_sharefd_to_phyaddr(struct ivp_ipc_device *ipc_dev, unsigned int *buff,
	unsigned int size __attribute__((unused)))
{
	int ret;
	unsigned int i;
	unsigned int share_fd = 0;
	unsigned int fd_num = 0;
	unsigned int sec_fd_num = 0;
	unsigned int nosec_fd_num = 0;
	unsigned long ion_phy_addr = 0x0;
	if(!ipc_dev || !buff) {
		ivp_err("invalid input param ipc_dev/buff");
		return -EFAULT;
	}

	mutex_lock(&ipc_dev->ivp_ion_mutex);
	/* the second field is sharefd number according to the algo arg struct */
	buff++;
	fd_num = *buff++;
	sec_fd_num = fd_num & 0xFFFF;
	nosec_fd_num = (fd_num >> SEC_NUM_SHIFT) & 0xFFFF;
	/* fd_num indicate the followed shared_fd number, it should not exceed the
	 buffer size(32), buff size = one cmd + one fdnum + fdnum*shard_fd + .. */
	if (((sec_fd_num + nosec_fd_num) > MAX_FD_NUM)) {
		ivp_err("ion buff number maybe wrong, num=%d\n", fd_num);
		mutex_unlock(&ipc_dev->ivp_ion_mutex);
		return -EFAULT;
	}
	/* trans sec buff phyaddr, phyaddr = phyaddr_begin+offset */
	share_fd = *buff++;
	if (sec_fd_num != 0) {
		ret = ivp_get_secbuff(&ipc_dev->ipc_pdev->dev, share_fd, &ion_phy_addr);
		if (ret < 0) {
			ivp_err("ion_phys failed, result=%d\n", ret);
			mutex_unlock(&ipc_dev->ivp_ion_mutex);
			return -EFAULT;
		}
	}
	for (i = 0; i < sec_fd_num; i++)
		*buff++ += ion_phy_addr;

	/* trans nosec buff phyaddr */
	for (i = 0; i < nosec_fd_num; i++) {
		share_fd = *buff;
		ret = ivp_get_secbuff(&ipc_dev->ipc_pdev->dev, share_fd, &ion_phy_addr);
		if (ret != EOK) {
			ivp_err("ion_phys failed, ret:%d", ret);
			mutex_unlock(&ipc_dev->ivp_ion_mutex);
			return -EFAULT;
		}
		*buff++ = ion_phy_addr;
	}
	mutex_unlock(&ipc_dev->ivp_ion_mutex);
	return ret;
}

int ivp_get_secbuff(
	struct device *dev,
	int sec_buf_fd,
	unsigned long *sec_buf_phy_addr)
{
	struct dma_buf *buf = NULL;
	struct dma_buf_attachment *attach = NULL;
	struct sg_table *sgt = NULL;
	struct scatterlist *sgl = NULL;
	int ret = -ENODEV;

	if (!dev || !sec_buf_phy_addr) {
		ivp_err("invalid input param");
		return -EINVAL;
	}

	if (sec_buf_fd < 0) {
		ivp_err("invalid sec buffer fd");
		return -EINVAL;
	}

	mutex_lock(&ivp_sec_mem_mutex);

	buf = dma_buf_get(sec_buf_fd);
	if (IS_ERR(buf)) {
		ivp_err("dma_buf_get for fd%d ", sec_buf_fd);
		goto err_dma_buf_get;
	}

	attach = dma_buf_attach(buf, dev);
	if (IS_ERR(attach)) {
		ivp_err("dma_buf_attach failed");
		goto err_dma_buf_attach;
	}

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ivp_err("dma_buf_map_attachment failed");
		goto err_dma_buf_map_attachment;
	}

	sgl = sgt->sgl;
	if (!sgl) {
		ivp_err("invalid sgl");
		goto err_sgl;
	}

	/* Get physical addresses from scatter list */
	*sec_buf_phy_addr = sg_phys(sgl);/* [false alarm]:it's not the bounds of allocated memory */
	ret = EOK;

err_sgl:
	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
err_dma_buf_map_attachment:
	dma_buf_detach(buf, attach);
err_dma_buf_attach:
	dma_buf_put(buf);
err_dma_buf_get:
	mutex_unlock(&ivp_sec_mem_mutex);
	return ret;
}

static int ivp_secwork_fn(void *data)
{
	struct ivp_device *pdev = (struct ivp_device *)data;
	struct ivp_sec_device *dev = pdev->sec_dev;
	struct cpumask cpu_mask;
	int cpu_no;

	ivp_info("+\n");

	set_user_nice(current, -10); // -10 is current num
	cpumask_clear(&cpu_mask);

	for (cpu_no = CPU_NO_MIN; cpu_no < CPU_NO_MAX; cpu_no++)
		cpumask_set_cpu(cpu_no, &cpu_mask);

	if (sched_setaffinity(current->pid, &cpu_mask) < 0)
		ivp_err("Couldn't set affinity to cpu");

	while (1) {
		if (kthread_should_stop())
			break;

		wait_event(dev->secivp_wait, dev->secivp_wake);

		ivp_info("kthread load");
		mutex_lock(&pdev->ivp_power_up_off_mutex);
		ivp_sec_load_and_verify_image(pdev);
		mutex_unlock(&pdev->ivp_power_up_off_mutex);

		dev->secivp_wake = false;
		complete(&dev->load_completion);
	}
	ivp_info("-\n");

	return EOK;
}

int ivp_create_secimage_thread(struct ivp_device *ivp_devp)
{
	struct ivp_sec_device *dev = NULL;

	if (!ivp_devp) {
		ivp_err("invalid input parm ivp_devp");
		return -EINVAL;
	}
	dev = ivp_devp->sec_dev;

	init_waitqueue_head(&dev->secivp_wait);

	/* create thread */
	dev->secivp_kthread = kthread_create(
		ivp_secwork_fn,
		ivp_devp,
		"secivpwork");
	if (IS_ERR(dev->secivp_kthread)) {
		ivp_err("Failed : kthread_create.%ld\n",
			PTR_ERR(dev->secivp_kthread));
		return -1;
	}

	atomic_set(&dev->ivp_image_success, 0);

	dev->secivp_wake = false;
	mutex_init(&ivp_sec_mem_mutex);
	wake_up_process(dev->secivp_kthread);

	return EOK;
}

int ivp_destroy_secimage_thread(struct ivp_device *ivp_devp)
{
	struct ivp_sec_device *dev = NULL;
	if (!ivp_devp) {
		ivp_err("invalid input parm ivp_devp");
		return -EINVAL;
	}

	dev = ivp_devp->sec_dev;

	if (!dev->secivp_kthread) {
		ivp_err("Failed : secivp_kthread.%pK\n", dev->secivp_kthread);
		return -ENXIO;
	}

	kthread_stop(dev->secivp_kthread);
	mutex_destroy(&ivp_sec_mem_mutex);

	return EOK;
}

int ivp_sec_load(struct ivp_device *ivp_devp)
{
	struct ivp_sec_device *dev = NULL;

	if (!ivp_devp) {
		ivp_err("invalid input parm ivp_devp");
		return -EINVAL;
	}

	dev = ivp_devp->sec_dev;

	if (!dev->secivp_kthread) {
		ivp_err("Failed : secivp_kthread.%pK\n", dev->secivp_kthread);
		return -ENXIO;
	}
	ivp_info("begin to load\n");
	atomic_set(&dev->ivp_image_success, 0);
	init_completion(&dev->load_completion);
	dev->secivp_wake = true;
	wake_up(&dev->secivp_wait);

	if (!wait_for_completion_timeout(
	&dev->load_completion, msecs_to_jiffies(2000))) {
		ivp_err("Failed : timeout!\n");
		return -ETIME;
	}

	if (atomic_read(&dev->ivp_image_success) == 1) {
		ivp_info("load success\n");
		return EOK;
	}
	ivp_err("Failed : load fail\n");
	return -ENOSYS;
}

