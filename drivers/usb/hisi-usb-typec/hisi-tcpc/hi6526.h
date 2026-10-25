#ifndef _HI6526_H_
#define _HI6526_H_

#include "include/std_tcpci_v10.h"

#define HISI_TCPC_VID 0x12d1
#define HISI_TCPC_PID 0x6526

/*
 * TCPC
 */
#define TCPCI_REG_VENDOR_ID			0x00
#define TCPCI_REG_PRODUCT_ID			0x02
#define TCPCI_REG_DEVICE_ID			0x04
#define TCPCI_REG_USBTYPEC_REV			0x06
#define TCPCI_REG_USBPD_REV_VER			0x08
#define TCPCI_REG_PD_INTERFACE_REV		0x0A
#define TCPCI_REG_ALERT				0x10
#define TCPCI_REG_ALERT_MASK			0x12

#define TCPCI_REG_POWER_STATUS_MASK		0x14
#define TCPCI_REG_FAULT_STATUS_MASK		0x15
#define TCPCI_REG_CONFIG_STANDARD_OUTPUT	0x18
#define TCPCI_REG_TCPC_CONTROL			0x19
#define TCPCI_REG_ROLE_CONTROL			0x1A
#define TCPCI_REG_FAULT_CONTROL			0x1B
#define TCPCI_REG_POWER_CONTROL			0x1C
#define TCPCI_REG_CC_STATUS			0x1D
#define TCPCI_REG_POWER_STATUS			0x1E
#define TCPCI_REG_FAULT_STATUS			0x1F
#define TCPCI_REG_COMMAND			0x23

#define TCPCI_REG_DEVICE_CAPABILITIES_1		0x24
#define TCPCI_REG_DEVICE_CAPABILITIES_2		0x26

#define TCPCI_REG_STANDARD_INPUT_CAPABILITIES	0x28
#define TCPCI_REG_STANDARD_OUTPUT_CAPABILITIES	0x29

#define  TCPCI_REG_MESSAGE_HEADER_INFO 		0x2E
#define  TCPCI_REG_RECEIVE_DETECT		0x2F
#define  TCPCI_REG_RECEIVE_BYTE_COUNT		0x30

#define  TCPCI_REG_RX_BUF_FRAME_TYPE		0x31
#define  TCPCI_REG_RX_BUF_HEADER_BYTE_0		0x32
#define  TCPCI_REG_RX_BUF_HEADER_BYTE_1		0x33

/*
 * vendor defined
 */
#define REG_PD_VDM_CFG_0			0x7A
#define PD_DA_FRS_ENABLE			(1 << 0)
#define REG_PD_VDM_ENABLE			0x7B
#define REG_PD_VDM_CFG_1			0x7C
#define PD_SNK_DISC_BY_CC			(1 << 4)
#define PD_RX_PHY_SOFT_RESET			(1 << 3)
#define PD_TX_PHY_SOFT_RESET			(1 << 2)
#define PD_FSM_RESET				(1 << 1)
#define TC_FSM_RESET				1
#define PD_TC_ALL_RESET				(PD_RX_PHY_SOFT_RESET | PD_TX_PHY_SOFT_RESET \
							| PD_FSM_RESET | TC_FSM_RESET)
#define REG_PD_DBG_RDATA_CFG		0x7D

#define PD_DBG_RDATA_MACHINE_STATUS	(0x80)
#define PD_DBG_RDATA_CC_STATUS		(0x82)
#define PD_DBG_RDATA_VBUS_STATUS	(0x83)
#define DA_VBUS_5V_EN_STATUS		(1 << 6)
#define PD_DBG_RDATA_RX_TX_STATUS	(0x91)

#define PD_DBG_MODULE_SEL(x)			(((x) & 0x7) << 4)
#define PD_DBG_RDATA_SEL(x)			((x) & 0xf)
#define REG_PD_DBG_RDATA			0x7E
#define PD_PHY_RX_STAT				(0x1 << 3)
#define REG_VDM_PAGE_SELECT 			0x7F

/*
 * PAGE0
 */

#define REG_PD_CDR_CFG_0	(0x80 + 0x58)	/* PD模块BMC时钟恢复电路配置寄存器 */
#define MASK_SNK_VBUS_DETECT	(0x1 << 6)	/* MASK SINK UNATTACH By Vbus Absent */
#define REG_PD_CDR_CFG_1	(0x80 + 0x59)	/* PD模块BMC时钟恢复电路配置寄存器 */
#define REG_PD_DBG_CFG_0	(0x80 + 0x5A)	/* PD模块Debug用配置寄存器 */
#define REG_PD_DBG_CFG_1	(0x80 + 0x5B)	/* PD模块Debug用配置寄存器 */
#define MASK_WDT_RST_PD_BIT	(0x1 << 6)	/* PD module will not be rsted when wdt timeout */
#define REG_PD_DBG_RO_0		(0x80 + 0x5C)	/* PD模块Debug用回读寄存器 */
#define REG_PD_DBG_RO_1		(0x80 + 0x5D)	/* PD模块Debug用回读寄存器 */
#define REG_PD_DBG_RO_2		(0x80 + 0x5E)	/* PD模块Debug用回读寄存器 */
#define REG_PD_DBG_RO_3		(0x80 + 0x5F)	/* PD模块Debug用回读寄存器 */

#define REG_IRQ_FLAG				(0x80 + 0x62)	/* Read Only */
#define REG_SC_BUCK_EN		(0x80 + 0x7E)
#define BIT_SC_BUCK_EN		(1 << 0)

/*
 * PAGE1
 */
#define REG_IRQ_MASK		(0x180 + 0x48)
#define BIT_IRQ_MASK_GLB	(1 << 7) /* bit[7]：全局屏蔽寄存器（使用默认值，不对产品开放）1: 屏蔽所有中断上报 0：不屏蔽所有中断上报 */
#define BIT_IRQ_MASK_SRC	(1 << 6) /* bit[6]：中断屏蔽源头选择（使用默认值，不对产品开放）1：中断屏蔽位屏蔽中断源头 0：中断屏蔽位不屏蔽中断源头，仅屏蔽输出 */
#define BIT_IRQ_MASK_PD		(1 << 2) /* bit[2]：PD总中断屏蔽 1: 屏蔽中断上报 0：不屏蔽中断上报*/

/*
 * PAGE2
 */
#define REG_TCPC_CFG_REG_1			(0x280 + 0x0E)	/* TCPC_配置寄存器_1 */
#define PD_FRS_DETECT				(1 << 6)
#define REG_TCPC_CFG_REG_2			(0x280 + 0x0F)	/* TCPC_配置寄存器_2 */
#define REG_TCPC_CFG_REG_3			(0x280 + 0x10)	/* TCPC_配置寄存器_3 */
#define REG_TCPC_RO_REG_5			(0x280 + 0x11)	/* TCPC_只读寄存器_5 */

#define CHIP_VERSION_V610	0xF3F6

int hisi_tcpc_block_read(u32 reg, int len, void *dst);
int hisi_tcpc_block_write(u32 reg, int len, void *src);
s32 hisi_tcpc_i2c_read8(struct i2c_client *client, u32 reg);
int hisi_tcpc_i2c_write8(struct i2c_client *client, u32 reg, u8 value);
s32 hisi_tcpc_i2c_read16(struct i2c_client *client, u32 reg);
int hisi_tcpc_i2c_write16(struct i2c_client *client, u32 reg, u16 value);
bool hisi_tcpc_get_vusb_uv_det_sts(void);
unsigned int hisi_tcpc_get_chip_version(void);

extern struct i2c_client *hi6526_i2c_client;
extern int hi6526_irq_gpio;

#endif
