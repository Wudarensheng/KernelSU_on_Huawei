/*
 * Hisilicon IPP Bl31 Driver
 *
 * Copyright (c) 2017 Hisilicon Technologies CO., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <asm/uaccess.h>
#include <asm/compiler.h>
#include <securec.h>

#define HIPP_FID_VALUE          (0xC500AC00u)
enum HIPP_SMC_TYPE_E {
	HIPP_BL31_SMMUENABLE    = 0,
	HIPP_BL31_SMMUDISABLE   = 1,
	HIPP_BL31_SMMUSMRX      = 2,
	MAX_HIPP_BL31
};

noinline int atfd_hipp_smc(u64 _funcid, u64 _arg0, u64 _arg1, u64 _arg2)
{
	register u64 funcid asm("x0") = _funcid;
	register u64 arg0 asm("x1") = _arg0;
	register u64 arg1 asm("x2") = _arg1;
	register u64 arg2 asm("x3") = _arg2;
	asm volatile(
		__asmeq("%0", "x0")
		__asmeq("%1", "x1")
		__asmeq("%2", "x2")
		__asmeq("%3", "x3")
		"smc    #0\n"
		: "+r"(funcid)
		: "r"(arg0), "r"(arg1), "r"(arg2));
	return (int)funcid;
}

int atfhipp_smmu_enable(unsigned int mode)
{
	return atfd_hipp_smc(HIPP_FID_VALUE, HIPP_BL31_SMMUENABLE, mode, 0);
}

int atfhipp_smmu_disable(void)
{
	return atfd_hipp_smc(HIPP_FID_VALUE, HIPP_BL31_SMMUDISABLE, 0, 0);
}

int atfhipp_smmu_smrx(unsigned int sid, unsigned int mode)
{
	return atfd_hipp_smc(HIPP_FID_VALUE, HIPP_BL31_SMMUSMRX, sid, mode);
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hisilicon IPP Bl31 Driver");
MODULE_AUTHOR("isp");
