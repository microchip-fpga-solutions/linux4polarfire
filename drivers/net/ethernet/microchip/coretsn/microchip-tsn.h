/* SPDX-License-Identifier: (GPL-2.0) */
/**
 * Microchip CoreTSN driver
 *
 * Copyright (C) 2025 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Pallela Venkat Karthik <pallela.karthik@microchip.com>
 *
 */

#ifndef __MICROCHIP_TSN_H__
#define __MICROCHIP_TSN_H__

#include <linux/module.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <asm/barrier.h>

/* CoreTSN device, driver strings */
#define MCHP_TSN_PDEV_DRV_NAME		"microchip-coretsn"
#define MCHP_TSN_CHARDEV_NAME		"mchpcoretsn"
#define MCHP_TSN_CHARDEV_CLASS_NAME	"mchpcoretsn-class"
#define MCHP_TSN_NUM_CHARDEVS		1
#define MCHP_TSN_RTL_V2 2
#define MCHP_TSN_RTL_V3 3

struct mchp_tsn_rtl_info {
	u8 rtl_ver;
	u8 num_queues;
	u8 num_streamid_per_q;
	u32 features;
};

/**
 * struct mchp_tsn_dev - private per device data
 * @pdev:		Pointer to platform device
 * @classdev:		Pointer to class device
 * @cdevname:		Name of character device
 * @cdev:		Character device
 * @class:		Pointer to class
 * @dev:		Device
 * @tsn_dev_mutex:		Mutex to lock multiple configurations at a time
 * @tsn_reg_base:	IO Mapped base address
 * @tsn_dev_id:		TSN device id
 * @core_clk_rate:	Operating clock rate of TSN Device
 * @qbv_gcl_mul:	Multiplier for Qbv scheduler time fields
 * @qbv_gcl_div:	Divider for Qbv scheduler time fields
 * @tsn_config_buff:	Buffer which holds command and response
 */
struct mchp_tsn_dev {
	struct platform_device *pdev;
	struct device *classdev;
	char cdevname[32];
	struct cdev cdev;
	struct class *class;
	dev_t dev;
	struct mutex tsn_dev_mutex;
	void __iomem *tsn_reg_base;
	u64 tsn_dev_id;
	u32 core_clk_rate;
	u32 qbv_gcl_mul;
	u32 qbv_gcl_div;
const struct mchp_tsn_rtl_info *rtl;
	u8 tsn_config_buff[];
};

/* Get or Set TSN configuration */

#define mchp_tsn_get(tsn_dev, reg, mask) \
	FIELD_GET(mask, readl_relaxed(tsn_dev->tsn_reg_base + reg))

#define mchp_tsn_set(tsn_dev, reg, data, mask) \
	mchp_tsn_write_register(tsn_dev, reg, FIELD_PREP(mask, data), mask)

/* TSN Register size */
#define TSN_REG_SIZE sizeof(u32)

/* QCI REGS and MASKS */
#define TSN_REG_PSC			0x020
#define TSN_REG_DST_MAC_MSB		0x008
#define TSN_REG_DST_MAC_LSB		0x00C
#define TSN_REG_SRC_MAC_MSB		0x010
#define TSN_REG_SRC_MAC_LSB		0x014
#define TSN_MASK_DA_CHECK		0x00000001
#define TSN_MASK_SA_CHECK		0x00000002
#define TSN_MASK_MAC_MSB		0xFFFFFFFF
#define TSN_MASK_MAC_LSB		0x0000FFFF

/* QBU REGS and MASKS */
#define TSN_REG_PREEMPT_CONTROL		0x000
#define TSN_MASK_PREEMPT_EN		0x00000001
#define TSN_MASK_PREEMPT_FRAG_SIZE	0x00000006

/* QBV REGS and MASKS */
#define TSN_REG_PCR			0x024
#define TSN_REG_PQE			0x028
#define TSN_REG_PQ0VR			0x02C
#define TSN_REG_SCTR			0x060
#define TSN_REG_SGSR			0x080
#define TSN_REG_SCLLR			0x084
#define TSN_REG_SGCL0ER			0x08C
#define TSN_REG_BTLR			0x070
#define TSN_REG_BTHR0			0x074
#define TSN_REG_BTHR1			0x078
#define TSN_REG_TIME_ADJUST		0x118
#define TSN_MASK_CONFIG_EN		0x00000004
#define TSN_MASK_INIT_GATE_STATE	0x000000FF
#define TSN_MASK_PRIO_ENABLE		0x00000001
#define TSN_MASK_GATE_ENABLE		0x00000008
#define TSN_MASK_PRIOQ_ENABLE           0x000000FF
#define TSN_MASK_CYCLE_TIME		0xFFFFFFFF
#define TSN_MASK_BASETIME_HIGH		0xFFFFFFFF
#define TSN_MASK_BASETIME_LOW		0xFFFFFFFF
#define TSN_MASK_BASETIME_ADJUST	0x0000003F
#define TSN_MASK_CONTROL_LIST_LENGTH	0x0000003F
#define TSN_MASK_PRIOQ_PRIO		0x00000007
#define TSN_MASK_TIME_INTERVAL		0x00FFFFFF
#define TSN_MASK_GATESTATE		0xFF000000
#define MAX_TSN_GCL_LEN			32

/* Destination RX Port REGS and MASKS */
#define TSN_REG_DRXPOID			0x110
#define TSN_MASK_PORT_ID_RX		0x0000FFFF
#define TSN_MASK_PORT_ID_RX_CHECK	0x00000004

/* PTP Transmit Queue REGS and MASKS */
#define TSN_REG_PTP_TX_PRIOQ		0x10C
#define TSN_MASK_PTP_TX_PRIOQ		0x00000007

/* Packet Length Deduct REGS and MASKS */
#define TSN_REG_LDB			0x114
#define TSN_MASK_LDB			0x000007FF

#define TSN_MASK_DEV_ID			0x0000FFFF

/* Stream ID register offsets and masks (v3) */
#define STREAM_ID_1_0			0x164
#define STREAM_ID_1_1			0x168
#define STREAM_ID_1_MASK		0x16C
#define TSN_MAC_ADDR			2
#define TSN_MASK_LSB			0xFFFFFFFF
#define TSN_MASK_MSB			0x0000FFFF
#define TSN_MASK_VID			0x0FFF0000
#define TSN_MASK_PCP			0x70000000
#define TSN_MASK_FRER_EN		0x80000000
#define TSN_MASK_EN			0x00000007
#define TSN_RX_STREAMID_TIMEOUT		0x0240
#define TSN_MASK_RX_STREAMID_TIMEOUT	0xFFFFFFFF

#define TSN_REG_FRER_PSFP_DEFAULT_PORT	0x0160
#define FRER_ENABLE_VALUE		0x00000001
#define TSN_MASK_FRER_BLOCK_EN		0x00000001

/* CBS REGS and MASKS */
#define TSN_REG_CBS_CONTROL		0x224
#define TSN_MASK_CBS_EN_Q0		0x00000001
#define TSN_MASK_CBS_EN_Q1		0x00000002
#define TSN_REG_CREDIT_QUEUE0		0x228
#define TSN_REG_CREDIT_QUEUE1		0x22C
#define TSN_MASK_CREDIT_INCR		0x0000FFFF
#define TSN_MASK_CREDIT_DECR		0xFFFF0000
#define TSN_REG_CREDIT_QUEUE_INC	0x4 /* Gap Between Address of CREDIT registers */
#define TSN_REG_CREDIT_MIN_Q0		0x230
#define TSN_REG_CREDIT_MAX_Q0		0x234
#define TSN_REG_CREDIT_MIN_Q1		0x238
#define TSN_REG_CREDIT_MAX_Q1		0x23C
#define TSN_MASK_CREDIT_MIN		0xFFFFFFFF
#define TSN_MASK_CREDIT_MAX		0xFFFFFFFF

#define STREAM_ID_PKT_SENT_BASE		0x0640
#define STREAM_ID_PKT_DROP_BASE		0x0600
#define PFSP_STREAM_ID_PKT_RCVD_BASE	0x06A0
#define PFSP_STREAM_ID_PKT_DROP_BASE	0x06A4
#define TSN_RX_PORT0_PRMPT_PKTS_DROP	0x0680
#define TSN_RX_PORT0_PRMPT_PKTS_RCVD	0x0684
#define TSN_RX_PORT0_EXP_PKTS_DROP	0x0688
#define TSN_RX_PORT0_EXP_PKTS_RCVD	0x068C
#define TSN_RX_PORT1_PRMPT_PKTS_DROP	0x0690
#define TSN_RX_PORT1_PRMPT_PKTS_RCVD	0x0694
#define TSN_RX_PORT1_EXP_PKTS_DROP	0x0698
#define TSN_RX_PORT1_EXP_PKTS_RCVD	0x069C
#define TSN_TX_PORT0_EXP_PKTS		0x0720
#define TSN_TX_PORT0_PRMPT_PKTS		0x0724
#define TSN_TX_PORT1_EXP_PKTS		0x0728
#define TSN_TX_PORT1_PRMPT_PKTS		0x072C
#define TSN_MASK_PKT_DROP		0xFFFFFFFF
#define TSN_MASK_PKT_SENT		0xFFFFFFFF
#define TSN_MASK_PKT_RCVD		0xFFFFFFFF

#define TSN_MASK_PSFP_BLOCK_EN		0x00000002
#define PFSP_ENABLE_VALUE		0x00000002

#define TSN_PFSP_MASK_LSB		0xFFFFFFFF
#define TSN_PFSP_MASK_MSB		0x0000FFFF
#define TSN_PSFP_MASK_VID		0x0FFF0000
#define TSN_PSFP_MASK_SA_EN		0x00000001
#define TSN_PSFP_MASK_VID_EN		0x00000002
#define TSN_PSFP_MASK_MAX_SDU_SIZE	0x0000FFFF
#define TSN_PSFP_MASK_FM_CBS		0x000007FF
#define TSN_PSFP_MASK_FM_EBS		0x003FF800
#define TSN_PSFP_MASK_FM_CBS_LSB	0x0000FFFF
#define TSN_PSFP_MASK_FM_EBS_LSB	0xFFFF0000
#define TSN_PSFP_MASK_FM_CBS_MSB	0x0000FFFF
#define TSN_PSFP_MASK_FM_EBS_MSB	0xFFFF0000
#define TSN_PSFP_MASK_FM_CIR		0xFFFFFFFF
#define TSN_PSFP_MASK_FM_EIR		0xFFFFFFFF
#define TSN_PSFP_MASK_FILTER_EN		0x00000001
#define TSN_PSFP_MASK_FM_EN		0x00000002
#define TSN_PSFP_MASK_DROP_ON_YELLOW	0x00000004
#define TSN_PSFP_MAX_SDU_SIZE_EXCEED	0x00000008
#define TSN_PSFP_MAX_CFG_UPDATE		0x00000010
#define TSN_PFSP_PORT0			0x0400
#define TSN_PFSP_MASK_MASK_REG		0x00000003
#define TSN_PFSP_MASK_EN_DIS		0x0000001F
#define TSN_PFSP_PORT_STREAMID		0x0300
#define TSN_PFSP_PORT_STREAM_ID_1_1	0x0304
#define TSN_PFSP_PORT_STREAM_ID_1_MASK	0x0308
#define TSN_PFSP_PORT_MAX_SDU_SIZE	0x030C
#define TSN_PFSP_PORT_STREAM_ID_1_FM_0	0x0310
#define TSN_PFSP_PORT_STREAM_ID_1_FM_CIR 0x0314
#define TSN_PFSP_PORT_STREAM_ID_1_FM_EIR 0x0318
#define TSN_PFSP_PORT_0_STREAM_ID_EN	0x031C
#define TSN_PFSP_STREAM_REG_SIZE	0x20
#define TSN_PFSP_PORT_STREAM_ID_1_FM_MSB 0x0500
#define TSN_PFSP_STREAM_FM_MSB_REG_SIZE	0x04

#endif /* __MICROCHIP_TSN_H__ */
