#ifndef __AT_XDMAC_H__
#define __AT_XDMAC_H__

#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_dma.h>
#include <linux/platform_device.h>

#include "dmaengine.h"

/* Global registers */
#define AT_XDMAC_GTYPE		0x00	/* Global Type Register */
#define		AT_XDMAC_NB_CH(i)	(((i) & 0x1F) + 1)		/* Number of Channels Minus One */	
#define		AT_XDMAC_FIFO_SZ(i)	(((i) >> 5) & 0x7FF)		/* Number of Bytes */
#define		AT_XDMAC_NB_REQ(i)	((((i) >> 16) & 0x3F) + 1)	/* Number of Peripheral Requests Minus One */
#define AT_XDMAC_GCFG		0x04	/* Global Configuration Register */
#define AT_XDMAC_GWAC		0x08	/* Global Weighted Arbiter Configuration Register */
#define AT_XDMAC_GIE		0x0C	/* Global Interrupt Enable Register */
#define AT_XDMAC_GID		0x10	/* Global Interrupt Disable Register */
#define AT_XDMAC_GIM		0x14	/* Global Interrupt Mask Register */
#define AT_XDMAC_GIS		0x18	/* Global Interrupt Status Register */
#define AT_XDMAC_GE		0x1C	/* Global Channel Enable Register */
#define AT_XDMAC_GD		0x20	/* Global Channel Disable Register */
#define AT_XDMAC_GS		0x24	/* Global Channel Status Register */
#define AT_XDMAC_GRS		0x28	/* Global Channel Read Suspend Register */
#define AT_XDMAC_GWS		0x2C	/* Global Write Suspend Register */
#define AT_XDMAC_GRWS		0x30	/* Global Channel Read Write Suspend Register */
#define AT_XDMAC_GRWR		0x34	/* Global Channel Read Write Resume Register */
#define AT_XDMAC_GSWR		0x38	/* Global Channel Software Request Register */
#define AT_XDMAC_GSWS		0x3C	/* Global channel Software Request Status Register */
#define AT_XDMAC_GSWF		0x40	/* Global Channel Software Flush Request Register */
#define AT_XDMAC_VERSION	0xFFC	/* XDMAC Version Register */

/* Channel relative registers offsets */
#define AT_XDMAC_CIE		0x00	/* Channel Interrupt Enable Register */
#define AT_XDMAC_CID		0x04	/* Channel Interrupt Disable Register */
#define AT_XDMAC_CIM		0x08	/* Channel Interrupt Mask Register */
#define AT_XDMAC_CIS		0x0C	/* Channel Interrupt Status Register */
#define		AT_XDMAC_CIx_BIS	(0x1 << 0)	/* End of Block Interrupt */
#define		AT_XDMAC_CIx_LIS	(0x1 << 1)	/* End of Linked List Interrupt */
#define		AT_XDMAC_CIx_DIS	(0x1 << 2)	/* End of Disable Interrupt */
#define		AT_XDMAC_CIx_FIS	(0x1 << 3)	/* End of Flush Interrupt */
#define		AT_XDMAC_CIx_RBEIS	(0x1 << 4)	/* Read Bus Error Interrupt */
#define		AT_XDMAC_CIx_WBEIS	(0x1 << 5)	/* Write Bus Error Interrupt */
#define		AT_XDMAC_CIx_ROIS	(0x1 << 6)	/* Request Overflow Interrupt (only relevant for peripheral synchronized transfers) */
#define AT_XDMAC_CSA		0x10	/* Channel Source Address Register */
#define AT_XDMAC_CDA		0x14	/* Channel Destination Address Register */
#define AT_XDMAC_CNDA		0x18	/* Channel Next Descriptor Address Register */
#define 	AT_XDMAC_CNDA_NDAIF(i)	((i) & 0x1)			/* Channel x Next Descriptor Interface */
#define		AT_XDMAC_CNDA_NDA(i)	((i) & 0xfffffffc)		/* Channel x Next Descriptor Address */
#define AT_XDMAC_CNDC		0x1C	/* Channel Next Descriptor Control Register */
#define		AT_XDMAC_CNDC_NDE		(0x1 << 0)		/* Channel x Next Descriptor Enable */
#define		AT_XDMAC_CNDC_NDSUP		(0x1 << 1)		/* Channel x Next Descriptor Source Update */
#define		AT_XDMAC_CNDC_NDDUP		(0x1 << 2)		/* Channel x Next Descriptor Destination Update */
#define		AT_XDMAC_CNDC_NDVIEW_NDV0	(0x0 << 3)		/* Channel x Next Descriptor View 0 */
#define		AT_XDMAC_CNDC_NDVIEW_NDV1	(0x1 << 3)		/* Channel x Next Descriptor View 1 */
#define		AT_XDMAC_CNDC_NDVIEW_NDV2	(0x2 << 3)		/* Channel x Next Descriptor View 2 */
#define		AT_XDMAC_CNDC_NDVIEW_NDV3	(0x3 << 3)		/* Channel x Next Descriptor View 3 */
#define AT_XDMAC_CUBC		0x20	/* Channel Microblock Control Register */
#define AT_XDMAC_CBC		0x24	/* Channel Block Control Register */
#define AT_XDMAC_CC		0x28	/* Channel Configuration Register */
#define		AT_XDMAC_CC_TYPE	(0x1 << 0)	/* Channel Transfer Type */
#define			AT_XDMAC_CC_TYPE_MEM_TRAN	(0x0 << 0)	/* Memory to Memory Transfer */
#define			AT_XDMAC_CC_TYPE_PER_TRAN	(0x1 << 0)	/* Peripheral to Memory or Memory to Peripheral Transfer */
#define		AT_XDMAC_CC_MBSIZE_MASK	(0x3 << 1)
#define			AT_XDMAC_CC_MBSIZE_SINGLE	(0x0 << 1)
#define			AT_XDMAC_CC_MBSIZE_FOUR		(0x1 << 1)
#define			AT_XDMAC_CC_MBSIZE_EIGHT	(0x2 << 1)
#define			AT_XDMAC_CC_MBSIZE_SIXTEEN	(0x3 << 1)
#define		AT_XDMAC_CC_DSYNC	(0x1 << 4)	/* Channel Synchronization */
#define			AT_XDMAC_CC_DSYNC_PER2MEM	(0x0 << 4)
#define			AT_XDMAC_CC_DSYNC_MEM2PER	(0x1 << 4)
#define		AT_XDMAC_CC_PROT	(0x1 << 5)	/* Channel Protection */
#define			AT_XDMAC_CC_PROT_SEC		(0x0 << 5)
#define			AT_XDMAC_CC_PROT_UNSEC		(0x1 << 5)
#define		AT_XDMAC_CC_SWREQ	(0x1 << 6)	/* Channel Software Request Trigger */
#define			AT_XDMAC_CC_SWREQ_HWR_CONNECTED	(0x0 << 6)
#define			AT_XDMAC_CC_SWREQ_SWR_CONNECTED	(0x1 << 6)
#define		AT_XDMAC_CC_MEMSET	(0x1 << 7)	/* Channel Fill Block of memory */
#define			AT_XDMAC_CC_MEMSET_NORMAL_MODE	(0x0 << 7)
#define			AT_XDMAC_CC_MEMSET_HW_MODE	(0x1 << 7)
#define		AT_XDMAC_CC_CSIZE_MASK	(0x7 << 8)	/* Channel Chunk Size */
#define			AT_XDMAC_CC_CSIZE_CHK_1		(0x0 << 8)
#define			AT_XDMAC_CC_CSIZE_CHK_2		(0x1 << 8)
#define			AT_XDMAC_CC_CSIZE_CHK_4		(0x2 << 8)
#define			AT_XDMAC_CC_CSIZE_CHK_8		(0x3 << 8)
#define			AT_XDMAC_CC_CSIZE_CHK_16	(0x4 << 8)
#define		AT_XDMAC_CC_DWIDTH(i)	((i) << 11)	/* Channel Data Width */
#define			AT_XDMAC_CC_DWIDTH_BYTE		0x0
#define			AT_XDMAC_CC_DWIDTH_HALFWORD	0x1
#define			AT_XDMAC_CC_DWIDTH_WORD		0x2
#define			AT_XDMAC_CC_DWIDTH_DWORD	0x3
#define		AT_XDMAC_CC_SIF(i)	((0x1 & (i)) << 13)	/* Channel Source Interface Identifier */
#define		AT_XDMAC_CC_DIF(i)	((0x1 & (i)) << 14)	/* Channel Destination Interface Identifier */
#define		AT_XDMAC_CC_SAM_MASK	(0x3 << 16)	/* Channel Source Addressing Mode */
#define			AT_XDMAC_CC_SAM_FIXED_AM	(0x0 << 16)
#define			AT_XDMAC_CC_SAM_INCREMENTED_AM	(0x1 << 16)
#define			AT_XDMAC_CC_SAM_UBS_AM		(0x2 << 16)
#define			AT_XDMAC_CC_SAM_UBS_DS_AM	(0x3 << 16)
#define		AT_XDMAC_CC_DAM_MASK	(0x3 << 18)	/* Channel Source Addressing Mode */
#define			AT_XDMAC_CC_DAM_FIXED_AM	(0x0 << 18)
#define			AT_XDMAC_CC_DAM_INCREMENTED_AM	(0x1 << 18)
#define			AT_XDMAC_CC_DAM_UBS_AM		(0x2 << 18)
#define			AT_XDMAC_CC_DAM_UBS_DS_AM	(0x3 << 18)
#define		AT_XDMAC_CC_INITD	(0x1 << 21)	/* Channel Initialization Terminated (read only) */
#define			AT_XDMAC_CC_INITD_TERMINATED	(0x0 << 21)
#define			AT_XDMAC_CC_INITD_IN_PROGRESS	(0x1 << 21)
#define		AT_XDMAC_CC_RDIP	(0x1 << 22)	/* Read in Progress (read only) */
#define			AT_XDMAC_CC_RDIP_DONE		(0x0 << 22)
#define			AT_XDMAC_CC_RDIP_IN_PROGRESS	(0x1 << 22)
#define		AT_XDMAC_CC_WDIP	(0x1 << 23)	/* Write in Progress (read only) */
#define			AT_XDMAC_CC_WDIP_DONE		(0x0 << 23)
#define			AT_XDMAC_CC_WDIP_IN_PROGRESS	(0x1 << 23)
#define		AT_XDMAC_CC_PERID(i)	(0x7f & (h) << 24)	/* Channel Peripheral Identifier */
#define AT_XDMAC_CDS_MSP	0x2C	/* Channel Data Stride Memory Set Pattern */
#define AT_XDMAC_CSUS		0x30	/* Channel Source Microblock Stride */
#define AT_XDMAC_CDUS		0x34	/* Channel Destination Microblock Stride */

#define AT_XDMAC_CHAN_REG_BASE	0x50	/* Channel registers base address */

/* TODO */
#define AT_XDMAC_MBR_UBC_UBLEN_MAX	0xFFFFFFUL
#define AT_XDMAC_MBR_UBC_NDE		(0x1 << 24)
#define AT_XDMAC_MBR_UBC_NSEN		(0x1 << 25)
#define AT_XDMAC_MBR_UBC_NDEN		(0x1 << 26)
#define AT_XDMAC_MBR_UBC_NDV0		(0x0 << 27)
#define AT_XDMAC_MBR_UBC_NDV1		(0x1 << 27)
#define AT_XDMAC_MBR_UBC_NDV2		(0x2 << 27)
#define AT_XDMAC_MBR_UBC_NDV3		(0x3 << 27)

#define AT_XDMAC_MAX_CHAN	0x20

enum atc_status {
	AT_XDMAC_CHAN_IS_CYCLIC = 0,
};

/* ----- Channels ----- */
struct at_xdmac_chan {
	struct dma_chan	chan;
	struct at_dma	*device;
	void __iomem	*ch_regs;
	u32		mask;
	u32		cfg;		/* channel configuration register */
	u8		perid;
	u8		dwidth;
	u8		csize;
	u8		mbsize;
	u8		perif;
	u8		memif;
	u32		remain_desc;
	unsigned long	status;
	struct tasklet_struct	tasklet;
	struct dma_slave_config	dma_sconfig;

	spinlock_t		lock;
	spinlock_t		xfers_list_lock;

	struct list_head	xfers_list;
	struct list_head	free_descs_list;
	unsigned int		descs_allocated;
};

/**
 * struct at_xdmac - internal representation of an Atmel XDMA Controller
 * @dma:
 * @regs:
 * @clk:
 * @chan: channels table to store at_xdmac_chan structures
 */

struct at_xdmac {
	struct dma_device	dma;
	void __iomem		*regs;
	struct clk		*clk;
	struct dma_pool		*at_xdmac_desc_pool;
	struct at_xdmac_chan	chan[0];
};

/* ----- Descriptors ----- */

/* Linked List Descriptor */
struct at_xdmac_lld {
	dma_addr_t	mbr_nda;	/* dma_addr_t ? Next Descriptor Member */
	u32		mbr_ubc;	/* Microblock Control Member */
	dma_addr_t	mbr_sa;		/* Source Address Member */
	dma_addr_t	mbr_da;		/* Destination Address Member */
	u32		mbr_cfg;	/* Configuration Register */
};

struct at_xdmac_desc {
	struct at_xdmac_lld		lld;
	enum dma_transfer_direction	direction;
	struct dma_async_tx_descriptor	tx_dma_desc;
	struct list_head		desc_node;
	/* Following members are only used by the first descriptor */
	bool				active_xfer;
	unsigned int			xfer_size;
	struct list_head		descs_list;
	struct list_head		xfer_node;
};

static inline void __iomem *at_xdmac_chan_reg_base(struct at_xdmac *atxdmac, unsigned int chan_nb)
{
	return (void __iomem*)(atxdmac->regs + (AT_XDMAC_CHAN_REG_BASE + chan_nb * 0x40));
}

#define at_xdmac_read(atxdmac, reg) __raw_readl((atxdmac)->regs + (reg))
#define at_xdmac_write(atxdmac, reg, value) \
	__raw_writel((value), (atxdmac)->regs + (reg))

#define at_xdmac_chan_read(atchan, reg) __raw_readl((atchan)->ch_regs + (reg))
#define at_xdmac_chan_write(atchan, reg, value) __raw_writel((value), (atchan)->ch_regs + (reg))

static inline struct at_xdmac_chan *to_at_xdmac_chan(struct dma_chan *dchan)
{
	return container_of(dchan, struct at_xdmac_chan, chan);
}

static struct device *chan2dev(struct dma_chan *chan)
{
	return &chan->dev->device;
}

static inline struct at_xdmac *to_at_xdmac(struct dma_device *ddev)
{
	return container_of(ddev, struct at_xdmac, dma);
}

static inline struct at_xdmac_desc *txd_to_at_desc(struct dma_async_tx_descriptor *txd)
{
	return container_of(txd, struct at_xdmac_desc, tx_dma_desc);
}

static inline u32 at_xdmac_convert_bus_width(enum dma_slave_buswidth width)
{
	switch (width) {
	case DMA_SLAVE_BUSWIDTH_2_BYTES:
		return AT_XDMAC_CC_DWIDTH_HALFWORD;
	case DMA_SLAVE_BUSWIDTH_4_BYTES:
		return AT_XDMAC_CC_DWIDTH_WORD;
	case DMA_SLAVE_BUSWIDTH_8_BYTES:
		return AT_XDMAC_CC_DWIDTH_DWORD;
	case DMA_SLAVE_BUSWIDTH_1_BYTE:
	default:
		return AT_XDMAC_CC_DWIDTH_BYTE;
	}
}

static inline int at_xdmac_chan_is_cyclic(struct at_xdmac_chan *atchan)
{
	return test_bit(AT_XDMAC_CHAN_IS_CYCLIC, &atchan->status);
}

#endif /* __AT_XDMAC_H__ */
