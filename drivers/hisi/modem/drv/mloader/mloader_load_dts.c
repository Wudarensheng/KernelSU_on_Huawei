#include <linux/file.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/zlib.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <bsp_ddr.h>
#include <bsp_sysctrl.h>
#include <bsp_sec.h>
#include <bsp_efuse.h>
#include <bsp_mloader.h>
#include "mloader_load_image.h"
#include "mloader_comm.h"
#include <hi_common.h>
#include <hi_mloader.h>
#include <securec.h>
#include <product_config_drv.h>
#include <bsp_sec_call.h>

#ifdef CONFIG_MODEM_DTB_LOAD_IN_KERNEL

#define THIS_MODU mod_mloader

int mloader_get_dtb_entry(unsigned int modemid, unsigned int num, modem_dt_entry_t *dt_entry_ptr,
                          modem_dt_entry_t *dt_entry_ccore, u32 core_id)
{
    int ret;
    u32 i;
    u8 sec_id[3] = {0};

    sec_id[0] = modemid_k_bits(modemid);
    sec_id[1] = modemid_h_bits(modemid);
    sec_id[2] = modemid_m_bits(modemid);

    /* 获取与modemid匹配的acore/ccore dt_entry 指针,复用dtctool，modem config.dts中将boardid配置为对应modem_id值 */
    for (i = 0; i < num; i++) {
        if ((dt_entry_ptr->boardid[0] == sec_id[0]) && (dt_entry_ptr->boardid[1] == sec_id[1]) &&
            (dt_entry_ptr->boardid[2] == sec_id[2]) && ((u32)(dt_entry_ptr->boardid[3]) == core_id)) {
            mloader_print_info("[%d],modemid(0x%x, 0x%x, 0x%x, 0x%x)\n", i, dt_entry_ptr->boardid[0],
                               dt_entry_ptr->boardid[1], dt_entry_ptr->boardid[2], dt_entry_ptr->boardid[3]);

            ret = memcpy_s((void *)dt_entry_ccore, sizeof(*dt_entry_ccore), (void *)dt_entry_ptr,
                           sizeof(*dt_entry_ptr));
            if (ret) {
                mloader_print_err("<%s> memcpy_s error, ret = %d\n", __FUNCTION__, ret);
            }
            break;
        }
        dt_entry_ptr++;
    }

    if (i == num) {
        return -ENOENT;
    }

    return 0;
}

int mloader_is_dtb_load_to_ccore(u32 core_id)
{
    if ((core_id == BOARDID_NRCORE_IDX) || (core_id == BOARDID_L2CORE_IDX)) {
        return 1;
    }
    return 0;
}

int mloader_load_dtb_to_ccore(char *file_name, u32 index, u32 offset, struct modem_dt_entry *dt_entry_ptr,
                              mloader_img_icc_info_s *image_info)
{
    void *vaddr = NULL;
    mloader_addr_s *mloader_addr = NULL;
    mloader_img_s *mloader_images = NULL;
    int readed_bytes;
    int ret;

    if (dt_entry_ptr == NULL || image_info == NULL) {
        mloader_print_err("param not right, please check.\n");
        return -1;
    }
    mloader_addr = bsp_mloader_get_addr();
    if (mloader_addr == NULL) {
        mloader_print_err("mloader addr is null in load dtb to ccore, please check.\n");
        return -1;
    }
    mloader_images = bsp_mloader_get_images_st();
    if (mloader_images == NULL) {
        mloader_print_err("mloader images is null in load dtb to ccore, please check.\n");
        return -1;
    }
    vaddr = mloader_addr->mloader_secboot_virt_addr[index];
    if (image_info->image_offset == 0) {
        /* read vrl to ccore */
        readed_bytes = mloader_read_file(file_name, offset + dt_entry_ptr->vrl_offset, dt_entry_ptr->vrl_size,
                                         (char *)vaddr);
        if (readed_bytes < 0 || (readed_bytes != dt_entry_ptr->vrl_size)) {
            mloader_print_err("fail to read %s(%d) vrl, readed_bytes 0x%x\n", file_name, image_info->core_id,
                              readed_bytes);
            ret = readed_bytes;
            return ret;
        }
        mloader_print_err("load vrl %s(%d) success\n", file_name, image_info->core_id);
        return 0;
    } else {
        /* read dtb to ccore */
        readed_bytes = mloader_read_file(file_name, offset + dt_entry_ptr->dtb_offset, dt_entry_ptr->dtb_size,
                                         (char *)vaddr);
        if (readed_bytes != dt_entry_ptr->dtb_size) {
            mloader_print_err("read_file %s(%d) err: readed_bytes 0x%x\n", file_name, image_info->core_id,
                              readed_bytes);
            ret = -EIO;
            return ret;
        }
        mloader_print_err("load image %s(%d) success\n", file_name, image_info->core_id);
    }
#ifdef CONFIG_COMPRESS_DTB_IMAGE
    return dt_entry_ptr->dtb_size;
#else
    return 0;
#endif
}

int mloader_load_and_verify_dtb(char *image_name, u32 core_id, ccpu_img_info_s *image, u32 index)
{
    int ret;
    int readed_bytes;
    u32 modem_id = 0;
    u32 offset = 0;
    u32 soc_type;
    struct modem_dt_table dt_table;
    struct modem_dt_table *dtb_header = &dt_table;
    struct modem_dt_entry *dt_entry = NULL;
    struct modem_dt_entry dt_entry_ptr;
    mloader_addr_s *mloader_addr = NULL;

#ifndef CONFIG_LOAD_SEC_IMAGE
    void *vaddr = NULL;
    void *vaddr_load = NULL;
    unsigned long paddr;
#endif
    char file_name[MLOADER_FILE_NAME_LEN] = {0};

    if (image_name == NULL) {
        mloader_print_err("image_name is NULL\n");
        return -ENOMEM;
    }
    mloader_addr = bsp_mloader_get_addr();

    ret = mloader_get_file_name(file_name, image_name, 0);
    if (ret) {
        mloader_print_err("can't find image:%s\n", image_name);
        return ret;
    }

    /* get the dtb head */
    offset = SECBOOT_VRL_TABLE_SIZE;
    readed_bytes = mloader_read_file(file_name, offset, (u32)sizeof(struct modem_dt_table), (char *)(dtb_header));
    if (readed_bytes != (int)sizeof(struct modem_dt_table)) {
        mloader_print_err("fail to read the head of modem dtb image, readed_bytes(0x%x) != size(0x%lx).\n",
                          readed_bytes, sizeof(struct modem_dt_table));
        return readed_bytes;
    }

    /* get the entry */
    dt_entry = (struct modem_dt_entry *)kzalloc((size_t)((sizeof(struct modem_dt_entry) * (dtb_header->num_entries))),
                                                GFP_KERNEL);
    if (dt_entry == NULL) {
        mloader_print_err("dtb header malloc fail! size is 0x%x\n",
                          (u32)sizeof(struct modem_dt_entry) * (dtb_header->num_entries));
        return -ENOMEM;
    }

    offset += sizeof(struct modem_dt_table);
    readed_bytes = mloader_read_file(file_name, offset,
                                     (u32)(sizeof(struct modem_dt_entry) * (dtb_header->num_entries)),
                                     (char *)dt_entry);
    if (readed_bytes != (int)(sizeof(struct modem_dt_entry) * (dtb_header->num_entries))) {
        mloader_print_err("fail to read the head of modem dtb entry, readed_bytes(0x%x) != size(0x%lx).\n",
                          readed_bytes, (sizeof(struct modem_dt_entry) * (dtb_header->num_entries)));
        ret = readed_bytes;
        goto err_out;
    }

    offset -= sizeof(struct modem_dt_table);

    /* 需要mask掉射频扣板ID号或modemid的bit[9:0] */
    modem_id = bsp_get_version_info()->product_id_udp_masked;
    mloader_print_err("modem_id 0x%x\n", modem_id);
    ret = memset_s((void *)&dt_entry_ptr, sizeof(dt_entry_ptr), 0, sizeof(modem_dt_entry_t));
    if (ret) {
        mloader_print_err("<%s> memset_s error, ret = %d\n", __FUNCTION__, ret);
    }

    ret = mloader_get_dtb_entry(modem_id, dtb_header->num_entries, dt_entry, &dt_entry_ptr, core_id);
    if (ret) {
        mloader_print_err("fail to get dtb entry\n");
        goto err_out;
    }

    if (!strcmp(file_name, MLOADER_DTS_IMG)) {
        soc_type = MODEM_DTB;
    }
/*
 * CONFIG_TZDRIVER & CONFIG_LOAD_SEC_IMAGE is for tzdrier(old secos, for 765)
 * CONFIG_TRUSTZONE_HM & CONFIG_LOAD_SEC_IMAGE is for tzdrier_hm(new hm secos)
 */
#if (((defined CONFIG_TZDRIVER) && (defined CONFIG_LOAD_SEC_IMAGE)) || \
     ((defined CONFIG_TRUSTZONE_HM) && (defined CONFIG_LOAD_SEC_IMAGE)))
    /* 安全版本且使能了签名 */
    if (dt_entry_ptr.vrl_size != 0) {
        /* load vrl data to sec os */
        if (dt_entry_ptr.vrl_size > MLOADER_SECBOOT_BUFLEN) {
            mloader_print_err("modem dtb vrl_size too large %d\n", dt_entry_ptr.vrl_size);
            ret = -ENOMEM;
            goto err_out;
        }
        mloader_print_err("modem dtb vrl_offset %d, vrl_size %d\n", dt_entry_ptr.vrl_offset, dt_entry_ptr.vrl_size);
        /* for lr dtb */
        readed_bytes = mloader_read_file(file_name, offset + dt_entry_ptr.vrl_offset, dt_entry_ptr.vrl_size,
                                         (char *)(mloader_addr->mloader_secboot_virt_addr_for_ccpu));
        if (readed_bytes < 0 || readed_bytes != dt_entry_ptr.vrl_size) {
            mloader_print_err("fail to read the dtb vrl\n");
            ret = readed_bytes;
            goto err_out;
        }
        ret = mloader_trans_vrl_to_os(soc_type, (void *)(mloader_addr->mloader_secboot_virt_addr_for_ccpu),
                                      SECBOOT_VRL_TABLE_SIZE);

        if (ret) {
            mloader_print_err("trans_vrl_to_os error, ret 0x%x!\n", ret);
            goto err_out;
        }
        mloader_print_err("trans vrl to secos success\n");
    }

    if (image == NULL) {
        mloader_print_err("modem dtb image is NULL, ret = %d.\n", ret);
        ret = -1;
        goto err_out;
    }
    /* load image data to sec os */
    ret = load_data_to_secos(file_name, offset + dt_entry_ptr.dtb_offset, dt_entry_ptr.dtb_size, soc_type,
                             image->dtb.run_addr, image->dtb.ddr_size, index);

    if (ret) {
        mloader_print_err("load image %s to secos failed, ret = 0x%x\n", file_name, ret);
        goto err_out;
    }
    mloader_print_err("load image %s to secos success\n", file_name);

    ret = mloader_verify_soc_image(soc_type, image->dtb.run_addr);
    if (ret) {
        mloader_print_err("fail to verify modem(%d) dtb image\n", soc_type);
        goto err_out;
    }
    mloader_print_err("verify modem(%d) dtb success\n", soc_type);
#else
    if (core_id == BOARDID_CCORE_IDX) {
        paddr = image->dtb.run_addr;
        vaddr = ioremap_wc(paddr, (unsigned long)(image->dtb.ddr_size));
        mloader_print_err("dtb img, paddr:0x%lx,vaddr:0x%lx,dtb_size:0x%x. ddr_size:0x%x\n", paddr, vaddr,
                          dt_entry_ptr.dtb_size, image->dtb.ddr_size);
    }

    if (vaddr == NULL) {
        mloader_print_err("ioremap_wc error\n");
        ret = -ENOMEM;
        goto err_out;
    }
    if (dt_entry_ptr.dtb_size > image->dtb.ddr_size) {
        mloader_print_err("modem dtb dtb_size too large %d than ddr_size %d\n", dt_entry_ptr.dtb_size,
                          image->dtb.ddr_size);
        ret = -ENOMEM;
        goto err_unmap;
    }

    vaddr_load = vaddr;
#ifdef CONFIG_COMPRESS_DTB_IMAGE
    vaddr_load = ((char *)vaddr_load - dt_entry_ptr.dtb_size) + image->dtb.ddr_size;
#endif
    readed_bytes = mloader_read_file(file_name, offset + dt_entry_ptr.dtb_offset, dt_entry_ptr.dtb_size,
                                     (char *)vaddr_load);
    if (readed_bytes != dt_entry_ptr.dtb_size) {
        mloader_print_err("read_file %s err: readed_bytes 0x%x\n", file_name, readed_bytes);
        ret = -EIO;
        goto err_unmap;
    }
    mloader_print_err("load image %s success\n", file_name);
#ifdef CONFIG_COMPRESS_DTB_IMAGE
    ret = mloader_decompress(image->dtb.ddr_size, vaddr_load, vaddr, dt_entry_ptr.dtb_size);
    if (ret) {
        mloader_print_err("decompress image %s failed\n", file_name);
        goto err_unmap;
    }
    mloader_print_err("decompress image %s success\n", file_name);
#endif

err_unmap:
    if (core_id == BOARDID_CCORE_IDX) {
        iounmap(vaddr);
    }

#endif

err_out:
    kfree(dt_entry);
    return ret;
}

int mloader_load_and_verify_dtb_data_from_modem(mloader_img_icc_info_s *image_info, u32 index)
{
    int ret;
    int readed_bytes;
    u32 modem_id = 0;
    u32 offset = 0;
    struct modem_dt_table dt_table;
    struct modem_dt_table *dtb_header = &dt_table;
    struct modem_dt_entry *dt_entry = NULL;
    struct modem_dt_entry dt_entry_ptr;
    mloader_addr_s *mloader_addr = NULL;

#ifndef CONFIG_LOAD_SEC_IMAGE
    void *vaddr = NULL;
    void *vaddr_load = NULL;
#endif
    char file_name[MLOADER_FILE_NAME_LEN] = {0};

    if (image_info->name == NULL) {
        mloader_print_err("image_name is NULL\n");
        return -ENOMEM;
    }
    mloader_addr = bsp_mloader_get_addr();

    ret = mloader_get_file_name(file_name, image_info->name, 0);
    if (ret) {
        mloader_print_err("can't find image:%s\n", image_info->name);
        return ret;
    }

    /* get the dtb head */
    offset = SECBOOT_VRL_TABLE_SIZE;
    readed_bytes = mloader_read_file(file_name, offset, (u32)sizeof(struct modem_dt_table), (char *)(dtb_header));
    if (readed_bytes != (int)sizeof(struct modem_dt_table)) {
        mloader_print_err("fail to read the head of modem dtb image, readed_bytes(0x%x) != size(0x%lx).\n",
                          readed_bytes, sizeof(struct modem_dt_table));
        return readed_bytes;
    }

    /* get the entry */
    dt_entry = (struct modem_dt_entry *)kzalloc((size_t)((sizeof(struct modem_dt_entry) * (dtb_header->num_entries))),
                                                GFP_KERNEL);
    if (dt_entry == NULL) {
        mloader_print_err("dtb header malloc fail! size is 0x%x\n",
                          (u32)sizeof(struct modem_dt_entry) * (dtb_header->num_entries));
        return -ENOMEM;
    }

    offset += sizeof(struct modem_dt_table);
    readed_bytes = mloader_read_file(file_name, offset,
                                     (u32)(sizeof(struct modem_dt_entry) * (dtb_header->num_entries)),
                                     (char *)dt_entry);
    if (readed_bytes != (int)(sizeof(struct modem_dt_entry) * (dtb_header->num_entries))) {
        mloader_print_err("fail to read the head of modem dtb entry, readed_bytes(0x%x) != size(0x%lx).\n",
                          readed_bytes, (sizeof(struct modem_dt_entry) * (dtb_header->num_entries)));
        ret = readed_bytes;
        goto err_out;
    }

    offset -= sizeof(struct modem_dt_table);

    /* 需要mask掉射频扣板ID号或modemid的bit[9:0] */
    modem_id = bsp_get_version_info()->product_id_udp_masked;
    mloader_print_err("modem_id 0x%x\n", modem_id);
    ret = memset_s((void *)&dt_entry_ptr, sizeof(dt_entry_ptr), 0, sizeof(modem_dt_entry_t));
    if (ret) {
        mloader_print_err("<%s> memset_s error, ret = %d\n", __FUNCTION__, ret);
    }

    ret = mloader_get_dtb_entry(modem_id, dtb_header->num_entries, dt_entry, &dt_entry_ptr, image_info->core_id);
    if (ret) {
        mloader_print_err("fail to get dtb entry\n");
        goto err_out;
    }

#if (((defined CONFIG_TZDRIVER) && (defined CONFIG_LOAD_SEC_IMAGE)) || \
     ((defined CONFIG_TRUSTZONE_HM) && (defined CONFIG_LOAD_SEC_IMAGE)))
    /* 安全版本且使能了签名 */
    if (dt_entry_ptr.vrl_size != 0) {
        /* load vrl data to sec os */
        if (dt_entry_ptr.vrl_size > MLOADER_SECBOOT_BUFLEN) {
            mloader_print_err("modem dtb vrl_size too large %d\n", dt_entry_ptr.vrl_size);
            ret = -ENOMEM;
            goto err_out;
        }
        mloader_print_err("modem dtb vrl_offset %d, vrl_size %d\n", dt_entry_ptr.vrl_offset, dt_entry_ptr.vrl_size);
        /* for nr、l2hac dtb */
        ret = mloader_is_dtb_load_to_ccore(image_info->core_id);
        if (ret) {
            if (image_info == NULL) {
                mloader_print_err("modem dtb image_info is NULL, ret = %d.\n", ret);
                ret = -1;
                goto err_out;
            }
            ret = mloader_load_dtb_to_ccore(file_name, index, offset, &dt_entry_ptr, image_info);
            goto err_out;
        }
    } else {
        if (image_info->image_offset == 0) {
            mloader_print_err("image %s vrl size is 0, no need to load.\n", file_name);
            ret = 0;
            goto err_out;
        } else {
            ret = mloader_is_dtb_load_to_ccore(image_info->core_id);
            if (ret) {
                if (image_info == NULL) {
                    mloader_print_err("modem dtb image_info is NULL, ret = %d.\n", ret);
                    ret = -1;
                    goto err_out;
                }
                ret = mloader_load_dtb_to_ccore(file_name, index, offset, &dt_entry_ptr, image_info);
                goto err_out;
            }
        }
    }
#else
    if ((image_info->core_id == BOARDID_NRCORE_IDX) || (image_info->core_id == BOARDID_L2CORE_IDX)) {
        vaddr = mloader_addr->mloader_secboot_virt_addr[index];
    } else {
        mloader_print_err("core_id(%d) not right.\n", image_info->core_id);
        ret = -1;
        goto err_out;
    }

    if (vaddr == NULL) {
        mloader_print_err("ioremap_wc error\n");
        ret = -ENOMEM;
        goto err_out;
    }
    if (dt_entry_ptr.dtb_size > image_info->ddr_size) {
        mloader_print_err("modem dtb dtb_size too large %d than ddr_size %d\n", dt_entry_ptr.dtb_size,
                          image_info->ddr_size);
        ret = -ENOMEM;
        goto err_unmap;
    }

    vaddr_load = vaddr;
#ifdef CONFIG_COMPRESS_DTB_IMAGE
    vaddr_load = ((char *)vaddr_load - dt_entry_ptr.dtb_size) + image_info->ddr_size;
#endif
    readed_bytes = mloader_read_file(file_name, offset + dt_entry_ptr.dtb_offset, dt_entry_ptr.dtb_size,
                                     (char *)vaddr_load);
    if (readed_bytes != dt_entry_ptr.dtb_size) {
        mloader_print_err("read_file %s err: readed_bytes 0x%x\n", file_name, readed_bytes);
        ret = -EIO;
        goto err_unmap;
    }
    mloader_print_err("load image %s success\n", file_name);
#ifdef CONFIG_COMPRESS_DTB_IMAGE
    ret = mloader_decompress(image_info->ddr_size, vaddr_load, vaddr, dt_entry_ptr.dtb_size);
    if (ret) {
        mloader_print_err("decompress image %s failed\n", file_name);
        goto err_unmap;
    }
    mloader_print_err("decompress image %s success\n", file_name);
#endif

err_unmap:
    if (image_info->core_id == BOARDID_CCORE_IDX) {
        iounmap(vaddr);
    }

#endif

err_out:
    kfree(dt_entry);
    return ret;
}


int mloader_load_and_verify_dtb_data(ccpu_img_info_s *image)
{
    int ret;

    ret = mloader_load_and_verify_dtb(image->dtb.name, image->dtb.core_id, image, 0);
    if (ret) {
        return ret;
    }
    return ret;
}

#else

int mloader_load_and_verify_dtb_data_from_modem(mloader_img_icc_info_s *image_info, u32 index)
{
    return 0;
}

int mloader_load_and_verify_dtb_data(ccpu_img_info_s *image)
{
    return 0;
}
#endif
