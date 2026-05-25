/* SPDX-License-Identifier: (GPL-2.0) */
/*
 * Microchip coreTSE (Triple Speed Ethernet) MAC driver
 *
 * Copyright (C) 2025 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Praveen Kumar Vattipalli <praveen.kumar@microchip.com>
 */
#ifndef _CORETSE_H
#define _CORETSE_H
#include <linux/clk.h>
#include <linux/phylink.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/net_tstamp.h>
#include <linux/interrupt.h>
#include <linux/phy/phy.h>
#include "mchp_core1588_ptp.h"
/* MAC registers */
#define CORETSE_CONFIG1                 0x00
#define CORETSE_CONFIG2                 0x04
#define CORETSE_IFG                     0x08
#define CORETSE_HALF_DUPLEX             0x0C
#define CORETSE_MAX_FRAME_LEN           0x10
#define CORETSE_CTRL_FRAME_EXT		0x14
#define CORETSE_CTRL_FRAME		0x18
#define CORETSE_TEST                    0x1C
#define CORETSE_MII_CONFIG              0x20
#define CORETSE_MII_COMMAND             0x24
#define CORETSE_MII_ADDRESS             0x28
#define CORETSE_MII_CTRL                0x2C
#define CORETSE_MII_STATUS              0x30
#define CORETSE_MII_IND                 0x34
#define CORETSE_IF_CTRL                 0x38
#define CORETSE_IF_STATUS               0x3C
#define CORETSE_STATION_ADDR0           0x40
#define CORETSE_STATION_ADDR1		0x44
#define CORETSE_FIFO_CONFIG0            0x48
#define CORETSE_FIFO_CONFIG1		0x4C
#define CORETSE_FIFO_CONFIG2		0x50
#define CORETSE_FIFO_CONFIG3		0x54
#define CORETSE_FIFO_CONFIG4		0x58
#define CORETSE_FIFO_CONFIG5		0x5C
#define CORETSE_FIFO_RAM_ACCESS0        0x60
#define CORETSE_FIFO_RAM_ACCESS1	0x64
#define CORETSE_FIFO_RAM_ACCESS2	0x68
#define CORETSE_FIFO_RAM_ACCESS3	0x6C
#define CORETSE_FIFO_RAM_ACCESS4	0x70
#define CORETSE_FIFO_RAM_ACCESS5	0x74
#define CORETSE_FIFO_RAM_ACCESS6	0x78
#define CORETSE_FIFO_RAM_ACCESS7	0x7C
#define CORETSE_FPC			0x1C0u
#define CORETSE_MISCC			0x1D4u
/* CFG1 register fields */
#define CORETSE_CFG1_RST                BIT(31)
#define CORETSE_CFG1_RX_ENA             BIT(2)
#define CORETSE_CFG1_TX_ENA             BIT(0)
/* CFG2 register fields */
#define CORETSE_CFG2_PREAM_LEN_BIT	12
#define CORETSE_CFG2_PREAM_LEN_MSK	0xf
#define CORETSE_CFG2_PREAM_LEN_DEFAULT	0x7
#define CORETSE_CFG2_MODE_BIT           8
#define CORETSE_CFG2_MODE_MSK		0x3
#define CORETSE_CFG2_MODE_BYTE          0x2
#define CORETSE_CFG2_MODE_MII           0x1
#define CORETSE_CFG2_HUGE_FRAME_EN	BIT(5)
#define CORETSE_CFG2_LEN_CHECK		BIT(4)
#define CORETSE_CFG2_PAD_CRC            BIT(2)
#define CORETSE_CFG2_CRC_EN		BIT(1)
#define CORETSE_CFG2_FULL_DUP           BIT(0)
/* MII_COMMAND register fields */
#define CORETSE_MII_CMD_READ            0x1
/* MII_ADDRESS register fields */
#define CORETSE_MII_ADR_PHY_BIT         8
#define CORETSE_MII_ADR_REG_BIT         0
/* MII_INDICATORS register fields */
#define CORETSE_MII_IND_NVAL            0x4
#define CORETSE_MII_IND_BUSY            0x1
#define CORETSE_MGMT_CLOCK_SEL          0x7
/* Interface Control register fields */
#define CORETSE_INTF_RESET              BIT(31)
#define CORETSE_INTF_SPEED_100          BIT(16)
/* FIFO_CFG0 register fields */
#define CORETSE_FIFO_CFG0_FTFENRPLY     BIT(20)
#define CORETSE_FIFO_CFG0_STFENRPLY     BIT(19)
#define CORETSE_FIFO_CFG0_FRFENRPLY     BIT(18)
#define CORETSE_FIFO_CFG0_SRFENRPLY     BIT(17)
#define CORETSE_FIFO_CFG0_WTMENRPLY     BIT(16)
#define CORETSE_FIFO_CFG0_ALL_RPLY	(CORETSE_FIFO_CFG0_FTFENRPLY | \
					 CORETSE_FIFO_CFG0_STFENRPLY | \
					 CORETSE_FIFO_CFG0_FRFENRPLY | \
					 CORETSE_FIFO_CFG0_WTMENRPLY)
#define CORETSE_FIFO_CFG0_FTFENREQ      BIT(12)
#define CORETSE_FIFO_CFG0_STFENREQ      BIT(11)
#define CORETSE_FIFO_CFG0_FRFENREQ      BIT(10)
#define CORETSE_FIFO_CFG0_SRFENREQ      BIT(9)
#define CORETSE_FIFO_CFG0_WTMENREQ      BIT(8)
#define CORETSE_FIFO_CFG0_ALL_REQ	(CORETSE_FIFO_CFG0_FTFENREQ | \
					 CORETSE_FIFO_CFG0_STFENREQ | \
					 CORETSE_FIFO_CFG0_FRFENREQ | \
					 CORETSE_FIFO_CFG0_SRFENREQ | \
					 CORETSE_FIFO_CFG0_WTMENREQ)
#define CORETSE_FIFO_CFG0_HSTRSTFT      BIT(4)
#define CORETSE_FIFO_CFG0_HSTRSTST      BIT(3)
#define CORETSE_FIFO_CFG0_HSTRSTFR      BIT(2)
#define CORETSE_FIFO_CFG0_HSTRSTSR      BIT(1)
#define CORETSE_FIFO_CFG0_HSTRSTWT      BIT(0)
#define CORETSE_FIFO_CFG0_ALL_RST	(CORETSE_FIFO_CFG0_HSTRSTFT | \
					 CORETSE_FIFO_CFG0_HSTRSTST | \
					 CORETSE_FIFO_CFG0_HSTRSTFR | \
					 CORETSE_FIFO_CFG0_HSTRSTSR | \
					 CORETSE_FIFO_CFG0_HSTRSTWT)
#define CORETSE_FIFO_CFG5_CFGHDPLX      BIT(22)
#define IFG_VALUE		0x40605060
#define HALF_DUPLEX_VALUE	0x00a0f037
#define FIFO_CONFIG1_VALUE	0x0fff0000
#define FIFO_CONFIG2_VALUE	0x04000180
#define FIFO_CONFIG3_VALUE	0x0680FFFF
#define FIFO_CONFIG4_VALUE	0x00002018
#define FIFO_CONFIG5_MASK	0x000FFFFF
/*
 * PCDMA registers - All accessed through a single base address (pcdma_regs).
 * S2MM (Stream-to-Memory-Mapped, RX) registers start at offset 0x00.
 * MM2S (Memory-Mapped-to-Stream, TX) registers start at offset 0x0400.
 */
/* S2MM PCDMA (RX) */
#define S2MM_PCDMA_VERSION	0x00
#define S2MM_PCDMA_CTRL		0x10
#define S2MM_PCDMA_STATUS	0x14
#define PCDMA_STATUS_DONE               0x01
#define PCDMA_STATUS_ERR                0xFFE
#define PCDMA_STATUS_AXI_ERR_MASK       0xC
#define S2MM_PCDMA_LENGTH	0x18
#define S2MM_PCDMA_ADDR0	0x1C
#define S2MM_PCDMA_ADDR1	0x20
#define S2MM_PCDMA_INT_EN               0x24
#define PCDMA_INT_DONE_EN               0x01
#define PCDMA_INT_DONE_DIS              0xFFE
#define PCDMA_INT_DIS                   0x0
#define PCDMA_INT_MASK                  0xFFF
#define S2MM_PCDMA_INT_SRC              0x28
#define PCDMA_INT_SRC_CLEAR             0xFFF
#define PCDMA_INT_SRC_DONE_CLEAR        0x01
#define PCDMA_INT_SRC_ERR_CLEAR         0xFFE
/* MM2S PCDMA (TX) */
#define MM2S_PCDMA_VERSION	0x0400
#define MM2S_PCDMA_CTRL		0x0410
#define MM2S_PCDMA_STATUS	0x0414
#define MM2S_PCDMA_LENGTH	0x0418
#define MM2S_PCDMA_ADDR0	0x041C
#define MM2S_PCDMA_ADDR1	0x0420
#define MM2S_PCDMA_INT_EN               0x0424
#define MM2S_PCDMA_INT_SRC              0x0428
/* Shared PCDMA diagnostic registers (relative to PCDMA base) */
#define PCDMA_INT_THR_CNT                0x2C
#define PCDMA_PKT_TRNS_CNT               0x30
#define PCDMA_AXI4_BUS_ERR_CNT           0x100
#define PCDMA_PKT_DROP_ERR_CNT           0x104
#define PCDMA_PKT_DROP_OVF_CNT           0x108
#define PCDMA_CMD_FIFO_ERR_CNT           0x10C
#define PCDMA_CMD_FIFO_DOUBLE_ERR_CNT    0x110
#define PCDMA_STATUS_FIFO_ERR_CNT        0x114
#define PCDMA_STATUS_FIFO_DOUBLE_ERR_CNT 0x118
/* DMA descriptor bitfields */
#define PCDMA_START			1
#define PCDMA_BURST_TYPE_OFFSET		1
#define PCDMA_BURST_TYPE_SIZE		2
#define PCDMA_BURST_TYPE_FIXED		0
#define PCDMA_BURST_TYPE_INC		1
#define PCDMA_CMD_ID_OFFSET		16
#define PCDMA_CMD_ID_SIZE		10
#define DELAY_OF_ONE_MILLISEC		10000
#define readx_poll_timeout_coretse(op, addr, val, cond, sleep_us, timeout_us)	\
	read_poll_timeout(op, val, cond, sleep_us, timeout_us, true, addr)
/* Number of RX and TX descriptors */
#define PCDMA_MAX_RX_DESCR              4
#define PCDMA_MAX_TX_DESCR              4
/* 1518 rounded up */
#define CORETSE_MAX_RBUFF_SZ            0x600
#define MAX_TX_LENGTH                   2048
/**
 * struct pcdma_desc - Protocol Converter DMA descriptor
 * @addr0:  DMA address low 32 bits of data buffer
 * @addr1:  DMA address high 32 bits of data buffer
 * @length: length of data buffer
 * @ctrl: Control bits
 */
struct pcdma_desc {
	u32	addr0;
	u32	addr1;
	u32	length;
	u32	ctrl;
};
#define MAPPING_MASK		0xFFFFFFFF
/**
 * struct coretse_tx_skb - data about an skb which is being transmitted
 * @skb:            skb currently being transmitted
 * @mapping:        DMA address of the skb's buffer
 * @size: size of the DMA mapped buffer
 * @mapped_as_page: true when buffer was mapped with skb_frag_dma_map()
 */
struct coretse_tx_skb {
	struct sk_buff		*skb;
	dma_addr_t		mapping;
	size_t			size;
	bool			mapped_as_page;
};
/**
 * Hardware-collected statistics.
 */
struct coretse_stats {
	u32	rx_pause_frames;
	u32	tx_ok;
	u32	tx_single_cols;
	u32	tx_multiple_cols;
	u32	rx_ok;
	u32	rx_fcs_errors;
	u32	rx_align_errors;
	u32	tx_deferred;
	u32	tx_late_cols;
	u32	tx_excessive_cols;
	u32	tx_underruns;
	u32	tx_carrier_errors;
	u32	rx_resource_errors;
	u32	rx_overruns;
	u32	rx_symbol_errors;
	u32	rx_oversize_pkts;
	u32	rx_jabbers;
	u32	rx_undersize_pkts;
	u32	sqe_test_errors;
	u32	rx_length_mismatch;
	u32	tx_pause_frames;
};
/**
 * struct queue_stats - Statistics counters collected by the MAC
 */
struct queue_stats {
	union {
		unsigned long first;
		unsigned long rx_packets;
	};
	unsigned long rx_bytes;
	unsigned long rx_dropped;
	unsigned long tx_packets;
	unsigned long tx_bytes;
	unsigned long tx_dropped;
};
struct coretse;
struct coretse_queue;
/**
 * struct rx_completion - Tracks a completed RX packet from ISR to NAPI
 * @cmd_id: The command ID (descriptor index) of the completed packet
 * @length: The received packet length from the DMA status
 * @valid:  Whether this entry contains a valid completion
 */
struct rx_completion {
	u32	cmd_id;
	u32	length;
	bool	valid;
};
/**
 * struct coretse_queue - Queue to hold dma buffers
 * @bp:		    private per device data
 * @tx_skb:         array of TX skb tracking entries
 * @rx_ring_dma:    Physical address of the RX buffer descriptor ring
 * @rx_buffers_dma: Physical address of the RX data buffers
 * @rx_ring:	    Virtual address of the RX buffer descriptor ring
 * @rx_buffers:     Virtual address of the RX data buffers
 * @napi_rx:	    NAPI RX control structure
 * @stats:	    Statistics counters collected by the MAC
 * @rx_head:        Next RX completion slot to write (producer, ISR context)
 * @rx_tail:        Next RX completion slot to read  (consumer, NAPI context)
 * @rx_completions: Ring of completed RX descriptors pending NAPI processing
 *
 * Debug / performance counters (exposed via ethtool -S):
 * @napi_polls:          Number of times the NAPI poll callback ran
 * @napi_complete:       Number of successful napi_complete_done() transitions
 * @napi_work_done:      Cumulative packets processed by NAPI
 * @napi_budget_hit:     Number of polls that exhausted budget
 * @rx_irq_total:        Total RX IRQs taken
 * @rx_irq_done:         RX IRQs that saw PCDMA_STATUS_DONE
 * @rx_poll_inline_done: Completions harvested inline from poll (not via ISR)
 * @rx_irq_err:          RX error events (ISR + inline poll harvest)
 * @rx_irq_ring_full:    Completion-ring overflow drops in ISR
 * @rx_skb_alloc_fail:   skb allocation failures in poll
 * @rx_gro_submit:       Packets submitted via napi_gro_receive()
 */
struct coretse_queue {
	struct coretse		*bp;
	struct coretse_tx_skb  tx_skb[PCDMA_MAX_TX_DESCR];
	dma_addr_t		rx_ring_dma;
	dma_addr_t		rx_buffers_dma;
	struct pcdma_desc	*rx_ring;
	void			*rx_buffers;
	struct napi_struct	napi_rx;
	struct queue_stats	stats;
	/* RX completion ring: ISR produces, NAPI consumes */
	unsigned int           rx_head;
	unsigned int           rx_tail;
	struct rx_completion   rx_completions[PCDMA_MAX_RX_DESCR];
	/* Debug / performance counters (ethtool -S) */
	u64 napi_polls;
	u64 napi_complete;
	u64 napi_work_done;
	u64 napi_budget_hit;
	u64 rx_irq_total;
	u64 rx_irq_done;
	u64 rx_poll_inline_done;
	u64 rx_irq_err;
	u64 rx_irq_ring_full;
	u64 rx_skb_alloc_fail;
	u64 rx_gro_submit;
};

/**
 * struct coretse - private per device data
 * @regs:                   Base address for the MAC register space
 * @pcdma_regs:             Base address for the PCDMA register space
 *                          (S2MM at offset 0x00, MM2S at offset 0x0400)
 * @queues:	data structure to hold dma buffers
 * @lock:	Spin lock
 * @tx_lock:	Spin lock for tx path
 * @rx_lock:                Spin lock for rx path
 * @tx_irq:                 MM2S TX IRQ number
 * @rx_irq:                 S2MM RX IRQ number
 * @tx_head:                Next TX descriptor to use for submission (producer)
 * @tx_tail:                Next TX descriptor to reclaim after completion (consumer)
 * @tx_count:               Number of TX descriptors currently in-flight
 * @coretse_tx_in_progress: legacy flag for tx busy
 * @clk:	CoreTSE bus clock
 * @dmaclk:	PCDMA clock
 * @pdev:       platform device structure
 * @dev:                    Pointer for net_device
 * @hw_stats:               Hardware-collected statistics
 * @mii_bus:	Pointer to MII bus structure
 * @phylink:	Pointer to phylink instance
 * @phylink_config: phylink configuration settings
 * @pcs_phy:	Reference to PCS/PMA PHY if used
 * @pcs:	phylink pcs structure for PCS PHY
 * @phy_interface:          Phy type (GMII/SGMII/1000Base-X)
 * @max_tx_length:          max tx packet length
 * @timer:                  core1588 PTP timer
 */
struct coretse {
	void __iomem		*regs;
	void __iomem                 *pcdma_regs;
	struct coretse_queue	queues[1];
	spinlock_t		lock;
	spinlock_t                    tx_lock;
	/* protects RX completion ring access */
	spinlock_t                    rx_lock;
	int tx_irq;
	int rx_irq;
	/* TX descriptor ring management */
	unsigned int                  tx_head;
	unsigned int                  tx_tail;
	unsigned int                  tx_count;
	u16 coretse_tx_in_progress;
	struct clk		*clk;
	struct clk		*dmaclk;
	struct platform_device	*pdev;
	struct net_device	*dev;
	struct coretse_stats          hw_stats;
	struct mii_bus		*mii_bus;
	struct phylink		*phylink;
	struct phylink_config	phylink_config;
	struct mdio_device	*pcs_phy;
	struct phylink_pcs	pcs;
	phy_interface_t		phy_interface;
	unsigned int		max_tx_length;
	struct mchp_core1588_timer *timer;
	bool nophy;
};
#endif /* _CORETSE_H */
