// SPDX-License-Identifier: (GPL-2.0)
/*
 * Microchip coreTSE (Triple Speed Ethernet) MAC driver
 *
 * Copyright (C) 2025 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Praveen Kumar Vattipalli <praveen.kumar@microchip.com>
 */
#include <linux/clk-provider.h>
#include <linux/crc32.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/circ_buf.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/iopoll.h>
#include <linux/reset.h>
#include "mchp_coretse.h"
#define MCHP_FRAME_FILTER_CTRL 0x3F
#define MCHP_MM2S_START 0x1
#define MCHP_MM2S_BURST_TYPE 0x2
#define MCHP_MM2S_START_ADDR BIT(16)
#define MCHP_S2MM_CMD_ID 0x3FF
#define MCHP_S2MM_CMD_ID_OFFSET 16
#define CORETSE_MII_IND_READ(tse) (readl_relaxed((tse)->regs + CORETSE_MII_IND))
#define POLL_TIMEOUT_U_SEC 1000
#define POLL_SLEEP_U_SEC 10

static int coretse_set_mac_address(struct net_device *ndev, void *addr);

/* ----------------------------------------------------------------
 * MDIO helpers
 * ----------------------------------------------------------------
 */
static int coretse_mdio_wait_for_idle(struct coretse *tse, u32 flags)
{
	u32 val;
	int ret;
	ret = readx_poll_timeout_coretse(CORETSE_MII_IND_READ, tse, val,
					 (!(val & flags)), POLL_SLEEP_U_SEC,
					 POLL_TIMEOUT_U_SEC);
	return ret;
}

/* ----------------------------------------------------------------
 * Phylink PCS ops
 * ----------------------------------------------------------------
 */
static void coretse_pcs_get_state(struct phylink_pcs *pcs,
				  unsigned int neg_mode,
				  struct phylink_link_state *state)
{
	state->speed = SPEED_1000;
	state->duplex = 1;
	state->an_complete = 1;
}

static void coretse_pcs_an_restart(struct phylink_pcs *pcs)
{
	/* nothing meaningful to do */
}

static int coretse_pcs_config(struct phylink_pcs *pcs, unsigned int mode,
			      phy_interface_t interface,
			      const unsigned long *advertising,
			      bool permit_pause_to_mac)
{
	/* nothing meaningful to do */
	return 0;
}

static const struct phylink_pcs_ops coretse_pcs_ops = {
	.pcs_get_state = coretse_pcs_get_state,
	.pcs_config = coretse_pcs_config,
	.pcs_an_restart = coretse_pcs_an_restart,
};

/* ----------------------------------------------------------------
 * Phylink MAC ops
 * ----------------------------------------------------------------
 */
static struct phylink_pcs *coretse_mac_select_pcs(struct phylink_config *config,
						  phy_interface_t interface)
{
	struct net_device *ndev = to_net_dev(config->dev);
	struct coretse *bp = netdev_priv(ndev);
	if (interface == PHY_INTERFACE_MODE_1000BASEX ||
	    interface == PHY_INTERFACE_MODE_SGMII)
		return &bp->pcs;
	return NULL;
}

static void coretse_mac_config(struct phylink_config *config, unsigned int mode,
			       const struct phylink_link_state *state)
{
	/* nothing meaningful to do */
}

static void coretse_mac_link_down(struct phylink_config *config,
				  unsigned int mode, phy_interface_t interface)
{
	/* nothing meaningful to do */
}

static void coretse_mac_link_up(struct phylink_config *config,
				struct phy_device *phy, unsigned int mode,
				phy_interface_t interface, int speed,
				int duplex, bool tx_pause, bool rx_pause)
{
	/* nothing meaningful to do */
}

static const struct phylink_mac_ops coretse_phylink_ops = {
	.mac_select_pcs = coretse_mac_select_pcs,
	.mac_config = coretse_mac_config,
	.mac_link_down = coretse_mac_link_down,
	.mac_link_up = coretse_mac_link_up,
};

/* ----------------------------------------------------------------
 * RX descriptor resubmit
 * ----------------------------------------------------------------
 * After NAPI finishes processing a received packet, resubmit the
 * same descriptor so the DMA engine can fill it again.
 */
static void mchp_coretse_resubmit_rx_desc(struct coretse *lp,
					  struct coretse_queue *q,
					  unsigned int idx)
{
	struct pcdma_desc *desc = &q->rx_ring[idx];

	writel_relaxed(desc->addr0, lp->pcdma_regs + S2MM_PCDMA_ADDR0);
	writel_relaxed(desc->addr1, lp->pcdma_regs + S2MM_PCDMA_ADDR1);
	writel_relaxed(desc->ctrl, lp->pcdma_regs + S2MM_PCDMA_CTRL);
}

/* ----------------------------------------------------------------
 * NAPI RX poll – processes up to @budget packets
 * ----------------------------------------------------------------
 * The ISR records completions in q->rx_completions[]; this function
 * drains them up to the NAPI budget.  When fewer than budget packets
 * are processed we call napi_complete_done() and re-enable the S2MM
 * done interrupt so the ISR can wake us again.
 */
static int mchp_coretse_rx_poll(struct napi_struct *napi, int budget)
{
	struct coretse_queue *q =
		container_of(napi, struct coretse_queue, napi_rx);
	struct coretse *lp = q->bp;
	struct net_device *dev = lp->dev;
	int work_done = 0;

	q->napi_polls++;
	while (work_done < budget) {
		struct rx_completion *comp;
		unsigned char *p_recv;
		struct sk_buff *skb;
		unsigned int tail;
		u32 cmd_id, len;

		tail = q->rx_tail;
		/* pair with smp_wmb() in ISR after writing rx_head */
		smp_rmb();
		if (tail == q->rx_head) {
			/*
			 * Completion ring is empty.  But IRQ is disabled,
			 * so check HW status directly for packets that
			 * arrived after the last ISR but before we got here.
			 */
			u32 status;

			status = readl_relaxed(lp->pcdma_regs +
					       S2MM_PCDMA_STATUS);
			/* Handle RX error seen inline */
			if (status & PCDMA_STATUS_ERR) {
				dev->stats.rx_errors++;
				writel_relaxed(PCDMA_INT_SRC_ERR_CLEAR,
					       lp->pcdma_regs +
						       S2MM_PCDMA_INT_SRC);
				q->rx_irq_err++;
			}
			if (!(status & PCDMA_STATUS_DONE))
				break; /* truly nothing pending */
			/* A packet arrived — harvest it inline */
			cmd_id = (status >> MCHP_S2MM_CMD_ID_OFFSET) &
				 MCHP_S2MM_CMD_ID;
			len = readl_relaxed(lp->pcdma_regs + S2MM_PCDMA_LENGTH);
			writel_relaxed(PCDMA_INT_SRC_DONE_CLEAR,
				       lp->pcdma_regs + S2MM_PCDMA_INT_SRC);
			q->rx_poll_inline_done++;
			if (unlikely(cmd_id >= PCDMA_MAX_RX_DESCR)) {
				netdev_err(dev, "Invalid RX cmd_id %u\n",
					   cmd_id);
				dev->stats.rx_errors++;
				continue;
			}
			p_recv =
				q->rx_buffers + (cmd_id * CORETSE_MAX_RBUFF_SZ);
			skb = netdev_alloc_skb_ip_align(dev, len);
			if (likely(skb)) {
				skb_copy_to_linear_data(skb, p_recv, len);
				skb_put(skb, len);
				skb->protocol = eth_type_trans(skb, dev);
				skb->ip_summed = CHECKSUM_NONE;
				if (lp->timer && lp->timer->ptp_rxstamp)
					lp->timer->ptp_rxstamp(lp->timer, skb);
				napi_gro_receive(napi, skb);
				dev->stats.rx_packets++;
				dev->stats.rx_bytes += len;
				q->rx_gro_submit++;
			} else {
				dev->stats.rx_dropped++;
				q->rx_skb_alloc_fail++;
			}
			mchp_coretse_resubmit_rx_desc(lp, q, cmd_id);
			work_done++;
			continue;
		}
		comp = &q->rx_completions[tail % PCDMA_MAX_RX_DESCR];
		if (!comp->valid)
			break;
		cmd_id = comp->cmd_id;
		len = comp->length;

		comp->valid = false;
		/* ensure completion is marked invalid before advancing tail */
		smp_wmb();
		q->rx_tail = tail + 1;
		if (unlikely(cmd_id >= PCDMA_MAX_RX_DESCR)) {
			netdev_err(dev, "Invalid RX cmd_id %u\n", cmd_id);
			dev->stats.rx_errors++;
			continue;
		}
		p_recv = q->rx_buffers + (cmd_id * CORETSE_MAX_RBUFF_SZ);
		skb = netdev_alloc_skb_ip_align(dev, len);
		if (likely(skb)) {
			skb_copy_to_linear_data(skb, p_recv, len);
			skb_put(skb, len);
			skb->protocol = eth_type_trans(skb, dev);
			skb->ip_summed = CHECKSUM_NONE;
			if (lp->timer && lp->timer->ptp_rxstamp)
				lp->timer->ptp_rxstamp(lp->timer, skb);
			napi_gro_receive(napi, skb);
			dev->stats.rx_packets++;
			dev->stats.rx_bytes += len;
			q->rx_gro_submit++;
		} else {
			dev->stats.rx_dropped++;
			q->rx_skb_alloc_fail++;
		}
		mchp_coretse_resubmit_rx_desc(lp, q, cmd_id);
		work_done++;
	}
	q->napi_work_done += work_done;
	if (work_done < budget) {
		if (napi_complete_done(napi, work_done)) {
			writel_relaxed(PCDMA_INT_MASK,
				       lp->pcdma_regs + S2MM_PCDMA_INT_EN);
			q->napi_complete++;
		}
	} else {
		q->napi_budget_hit++;
	}
	return work_done;
}

/* ----------------------------------------------------------------
 * S2MM (RX) interrupt handler
 * ----------------------------------------------------------------
 * Each completion is recorded in the rx_completions ring so NAPI can
 * process them in batches.  The done interrupt is disabled until NAPI
 * calls napi_complete_done().
 */
static irqreturn_t mchp_pcdma_rx_irq(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct coretse *lp = netdev_priv(dev);
	struct coretse_queue *q = &lp->queues[0];
	u32 status;
	status = readl_relaxed(lp->pcdma_regs + S2MM_PCDMA_STATUS);
	if (unlikely(!status))
		return IRQ_NONE;
	q->rx_irq_total++;
	if (status & PCDMA_STATUS_ERR) {
		netdev_err(dev, "Rx error 0x%x\n", status);
		writel_relaxed(PCDMA_INT_SRC_ERR_CLEAR,
			       lp->pcdma_regs + S2MM_PCDMA_INT_SRC);
		q->rx_irq_err++;
	}
	if (status & PCDMA_STATUS_DONE) {
		u32 cmd_id, len;
		unsigned int head, tail, next;
		struct rx_completion *comp;

		cmd_id = (status >> MCHP_S2MM_CMD_ID_OFFSET) & MCHP_S2MM_CMD_ID;
		len = readl_relaxed(lp->pcdma_regs + S2MM_PCDMA_LENGTH);
		writel_relaxed(PCDMA_INT_SRC_DONE_CLEAR,
			       lp->pcdma_regs + S2MM_PCDMA_INT_SRC);
		q->rx_irq_done++;
		/* Overflow protection: if ring full, drop */
		head = q->rx_head;
		tail = q->rx_tail;
		next = head + 1;
		/* pair with smp_wmb() in poll after updating rx_tail */
		smp_rmb();
		if ((next - tail) > PCDMA_MAX_RX_DESCR) {
			dev->stats.rx_dropped++;
			q->rx_irq_ring_full++;
			return IRQ_HANDLED;
		}
		comp = &q->rx_completions[head % PCDMA_MAX_RX_DESCR];
		comp->cmd_id = cmd_id;
		comp->length = len;
		comp->valid = true;
		/* publish completion before advancing head */
		smp_wmb();
		q->rx_head = next;
		/*
		 * Only disable IRQ and schedule NAPI if not already
		 * scheduled.  If NAPI is already running, it will
		 * pick up the new completion from the ring.
		 */
		if (napi_schedule_prep(&q->napi_rx)) {
			writel_relaxed(PCDMA_INT_DIS,
				       lp->pcdma_regs + S2MM_PCDMA_INT_EN);
			__napi_schedule(&q->napi_rx);
		}
	}
	return IRQ_HANDLED;
}

/* ----------------------------------------------------------------
 * TX completion helper
 * ----------------------------------------------------------------
 */
static void mchp_coretse_tx_complete(struct coretse *lp)
{
	struct net_device *dev = lp->dev;
	struct coretse_queue *q = &lp->queues[0];
	unsigned int tail = lp->tx_tail;
	struct coretse_tx_skb *tx_entry;

	tx_entry = &q->tx_skb[tail % PCDMA_MAX_TX_DESCR];
	if (!tx_entry->skb)
		return;
	/* core1588 PTP TX timestamp */
	if (lp->timer && lp->timer->ptp_txstamp)
		lp->timer->ptp_txstamp(lp->timer, tx_entry->skb);
	dma_unmap_single(&lp->pdev->dev, tx_entry->mapping, tx_entry->size,
			 DMA_TO_DEVICE);
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += tx_entry->size;
	dev_consume_skb_irq(tx_entry->skb);
	tx_entry->skb = NULL;
	tx_entry->mapping = 0;
	tx_entry->size = 0;
	lp->tx_tail = tail + 1;
	lp->tx_count--;
	if (netif_queue_stopped(dev) && lp->tx_count < PCDMA_MAX_TX_DESCR)
		netif_wake_queue(dev);
}

/* ----------------------------------------------------------------
 * MM2S (TX) interrupt handler
 * ----------------------------------------------------------------
 */
static irqreturn_t mchp_pcdma_tx_irq(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct coretse *lp = netdev_priv(dev);
	u32 status;
	unsigned long flags;
	status = readl_relaxed(lp->pcdma_regs + MM2S_PCDMA_STATUS);
	if (unlikely(!status))
		return IRQ_NONE;
	spin_lock_irqsave(&lp->tx_lock, flags);
	if (status & PCDMA_STATUS_ERR) {
		dev->stats.tx_errors++;
		writel_relaxed(PCDMA_INT_SRC_ERR_CLEAR,
			       lp->pcdma_regs + MM2S_PCDMA_INT_SRC);
		netdev_err(dev, "Tx PCDMA_STATUS_ERR 0x%x\n", status);
	}
	if (status & PCDMA_STATUS_DONE) {
		writel_relaxed(PCDMA_INT_SRC_DONE_CLEAR,
			       lp->pcdma_regs + MM2S_PCDMA_INT_SRC);
		mchp_coretse_tx_complete(lp);
	}
	spin_unlock_irqrestore(&lp->tx_lock, flags);
	return IRQ_HANDLED;
}

/* ----------------------------------------------------------------
 * ethtool -S counters
 * ----------------------------------------------------------------
 */
enum coretse_stat_id {
	CORETSE_S_NAPI_POLLS,
	CORETSE_S_NAPI_COMPLETE,
	CORETSE_S_NAPI_WORK_DONE,
	CORETSE_S_NAPI_BUDGET_HIT,
	CORETSE_S_RX_IRQ_TOTAL,
	CORETSE_S_RX_IRQ_DONE,
	CORETSE_S_RX_POLL_INLINE_DONE,
	CORETSE_S_RX_IRQ_ERR,
	CORETSE_S_RX_IRQ_RING_FULL,
	CORETSE_S_RX_SKB_ALLOC_FAIL,
	CORETSE_S_RX_GRO_SUBMIT,
	CORETSE_S_COUNT,
};

static const char coretse_stat_names[][ETH_GSTRING_LEN] = {
	"napi_polls",	       "napi_complete", "napi_work_done",
	"napi_budget_hit",     "rx_irq_total",	"rx_irq_done",
	"rx_poll_inline_done", "rx_irq_err",	"rx_irq_ring_full",
	"rx_skb_alloc_fail",   "rx_gro_submit",
};

static int mchp_coretse_get_sset_count(struct net_device *dev, int sset)
{
	if (sset == ETH_SS_STATS)
		return CORETSE_S_COUNT;
	return -EOPNOTSUPP;
}

static void mchp_coretse_get_strings(struct net_device *dev, u32 sset, u8 *data)
{
	if (sset != ETH_SS_STATS)
		return;
	memcpy(data, coretse_stat_names, sizeof(coretse_stat_names));
}

static void mchp_coretse_get_ethtool_stats(struct net_device *dev,
					   struct ethtool_stats *stats,
					   u64 *data)
{
	struct coretse *lp = netdev_priv(dev);
	struct coretse_queue *q = &lp->queues[0];

	data[CORETSE_S_NAPI_POLLS] = q->napi_polls;
	data[CORETSE_S_NAPI_COMPLETE] = q->napi_complete;
	data[CORETSE_S_NAPI_WORK_DONE] = q->napi_work_done;
	data[CORETSE_S_NAPI_BUDGET_HIT] = q->napi_budget_hit;
	data[CORETSE_S_RX_IRQ_TOTAL] = q->rx_irq_total;
	data[CORETSE_S_RX_IRQ_DONE] = q->rx_irq_done;
	data[CORETSE_S_RX_POLL_INLINE_DONE] = q->rx_poll_inline_done;
	data[CORETSE_S_RX_IRQ_ERR] = q->rx_irq_err;
	data[CORETSE_S_RX_IRQ_RING_FULL] = q->rx_irq_ring_full;
	data[CORETSE_S_RX_SKB_ALLOC_FAIL] = q->rx_skb_alloc_fail;
	data[CORETSE_S_RX_GRO_SUBMIT] = q->rx_gro_submit;
}

/* ----------------------------------------------------------------
 * Phylink connect
 * ----------------------------------------------------------------
 */
static bool coretse_phy_handle_exists(struct device_node *dn)
{
	dn = of_parse_phandle(dn, "phy-handle", 0);
	of_node_put(dn);
	return dn;
}

static int coretse_phylink_connect(struct coretse *bp)
{
	struct device_node *dn = bp->pdev->dev.of_node;
	struct net_device *dev = bp->dev;
	struct phy_device *phydev;
	int ret = 0;

	if (dn)
		ret = phylink_of_phy_connect(bp->phylink, dn, 0);
	if (!dn || (ret && !coretse_phy_handle_exists(dn))) {
		phydev = phy_find_first(bp->mii_bus);
		if (!phydev) {
			netdev_err(dev, "no PHY found\n");
			return -ENXIO;
		}
		ret = phylink_connect_phy(bp->phylink, phydev);
	}
	if (ret) {
		netdev_err(dev, "Could not attach PHY (%d)\n", ret);
		return ret;
	}
	phylink_start(bp->phylink);
	return 0;
}

/* ----------------------------------------------------------------
 * MDIO read / write
 * ----------------------------------------------------------------
 */
static int mchp_coretse_mdio_read(struct mii_bus *bus, int phy_id, int reg)
{
	struct coretse *tse = bus->priv;
	u32 value;
	int ret;

	value = (phy_id << CORETSE_MII_ADR_PHY_BIT) |
		(reg << CORETSE_MII_ADR_REG_BIT);
	ret = coretse_mdio_wait_for_idle(tse, CORETSE_MII_IND_BUSY);
	if (ret < 0)
		return ret;
	writel_relaxed(value, tse->regs + CORETSE_MII_ADDRESS);
	writel_relaxed(CORETSE_MII_CMD_READ, tse->regs + CORETSE_MII_COMMAND);
	writel_relaxed(0, tse->regs + CORETSE_MII_COMMAND);
	ret = coretse_mdio_wait_for_idle(tse, (CORETSE_MII_IND_NVAL |
					       CORETSE_MII_IND_BUSY));
	if (ret < 0)
		return ret;
	ret = readl_relaxed(tse->regs + CORETSE_MII_STATUS);
	return ret;
}

static int mchp_coretse_mdio_write(struct mii_bus *bus, int phy_id, int reg,
				   u16 data)
{
	struct coretse *tse = bus->priv;
	u32 value;
	int ret;

	value = (phy_id << CORETSE_MII_ADR_PHY_BIT) |
		(reg << CORETSE_MII_ADR_REG_BIT);
	ret = coretse_mdio_wait_for_idle(tse, CORETSE_MII_IND_BUSY);
	if (ret < 0)
		return ret;
	writel_relaxed(value, tse->regs + CORETSE_MII_ADDRESS);
	writel_relaxed(data, tse->regs + CORETSE_MII_CTRL);
	ret = coretse_mdio_wait_for_idle(tse, CORETSE_MII_IND_BUSY);
	return ret;
}

/* ----------------------------------------------------------------
 * DMA coherent memory allocation / free
 * ----------------------------------------------------------------
 */
static int mchp_coretse_alloc_coherent(struct coretse *lp)
{
	struct coretse_queue *q = &lp->queues[0];

	q->rx_ring = dma_alloc_coherent(
		&lp->pdev->dev, PCDMA_MAX_RX_DESCR * sizeof(struct pcdma_desc),
		&q->rx_ring_dma, GFP_KERNEL);
	if (!q->rx_ring)
		return -ENOMEM;
	q->rx_buffers = dma_alloc_coherent(
		&lp->pdev->dev, PCDMA_MAX_RX_DESCR * CORETSE_MAX_RBUFF_SZ,
		&q->rx_buffers_dma, GFP_KERNEL);
	if (!q->rx_buffers) {
		dma_free_coherent(&lp->pdev->dev,
				  PCDMA_MAX_RX_DESCR *
					  sizeof(struct pcdma_desc),
				  q->rx_ring, q->rx_ring_dma);
		q->rx_ring = NULL;
		return -ENOMEM;
	}
	return 0;
}

static void mchp_coretse_free_coherent(struct coretse *lp)
{
	struct coretse_queue *q = &lp->queues[0];
	if (q->rx_ring) {
		dma_free_coherent(&lp->pdev->dev,
				  PCDMA_MAX_RX_DESCR *
					  sizeof(struct pcdma_desc),
				  q->rx_ring, q->rx_ring_dma);
		q->rx_ring = NULL;
	}
	if (q->rx_buffers) {
		dma_free_coherent(&lp->pdev->dev,
				  PCDMA_MAX_RX_DESCR * CORETSE_MAX_RBUFF_SZ,
				  q->rx_buffers, q->rx_buffers_dma);
		q->rx_buffers = NULL;
	}
}

/* ----------------------------------------------------------------
 * Ring initialisation helpers
 * ----------------------------------------------------------------
 */
static void mchp_coretse_init_tx_ring(struct coretse *lp)
{
	struct coretse_queue *q = &lp->queues[0];
	int i;

	lp->tx_head = 0;
	lp->tx_tail = 0;
	lp->tx_count = 0;
	lp->coretse_tx_in_progress = 0;
	for (i = 0; i < PCDMA_MAX_TX_DESCR; i++) {
		q->tx_skb[i].skb = NULL;
		q->tx_skb[i].mapping = 0;
		q->tx_skb[i].size = 0;
		q->tx_skb[i].mapped_as_page = false;
	}
}

static void mchp_coretse_init_rx_ring(struct coretse_queue *q)
{
	int i;

	q->rx_head = 0;
	q->rx_tail = 0;
	for (i = 0; i < PCDMA_MAX_RX_DESCR; i++)
		q->rx_completions[i].valid = false;
	/* Reset debug / ethtool counters on each start */
	q->napi_polls = 0;
	q->napi_complete = 0;
	q->napi_work_done = 0;
	q->napi_budget_hit = 0;
	q->rx_irq_total = 0;
	q->rx_irq_done = 0;
	q->rx_poll_inline_done = 0;
	q->rx_irq_err = 0;
	q->rx_irq_ring_full = 0;
	q->rx_skb_alloc_fail = 0;
	q->rx_gro_submit = 0;
}

/* ----------------------------------------------------------------
 * Start / Stop the MAC + DMA
 * ----------------------------------------------------------------
 */
static int mchp_coretse_start(struct coretse *lp)
{
	struct coretse_queue *q = &lp->queues[0];
	dma_addr_t addr;
	int i, ret;
	ret = mchp_coretse_alloc_coherent(lp);
	if (ret)
		return ret;
	mchp_coretse_init_tx_ring(lp);
	mchp_coretse_init_rx_ring(q);
	/* Clear pending interrupt sources */
	writel_relaxed(PCDMA_INT_SRC_CLEAR,
		       lp->pcdma_regs + MM2S_PCDMA_INT_SRC);
	writel_relaxed(PCDMA_INT_SRC_CLEAR,
		       lp->pcdma_regs + S2MM_PCDMA_INT_SRC);
	/* Enable PCDMA interrupts */
	writel_relaxed(PCDMA_INT_MASK, lp->pcdma_regs + MM2S_PCDMA_INT_EN);
	writel_relaxed(PCDMA_INT_MASK, lp->pcdma_regs + S2MM_PCDMA_INT_EN);
	/* Setup and submit all RX descriptors */
	addr = q->rx_buffers_dma;
	for (i = 0; i < PCDMA_MAX_RX_DESCR; i++) {
		q->rx_ring[i].addr0 = (u32)addr;
		q->rx_ring[i].addr1 = (u32)(addr >> 32);
		q->rx_ring[i].length = 0;
		q->rx_ring[i].ctrl =
			((i << PCDMA_CMD_ID_OFFSET) |
			 (PCDMA_BURST_TYPE_INC << PCDMA_BURST_TYPE_OFFSET) |
			 PCDMA_START);
		writel_relaxed(q->rx_ring[i].addr0,
			       lp->pcdma_regs + S2MM_PCDMA_ADDR0);
		writel_relaxed(q->rx_ring[i].addr1,
			       lp->pcdma_regs + S2MM_PCDMA_ADDR1);
		writel_relaxed(q->rx_ring[i].ctrl,
			       lp->pcdma_regs + S2MM_PCDMA_CTRL);
		addr += CORETSE_MAX_RBUFF_SZ;
	}
	/* Enable Receive and Transmit in the MAC */
	writel_relaxed(CORETSE_CFG1_RX_ENA | CORETSE_CFG1_TX_ENA,
		       lp->regs + CORETSE_CONFIG1);
	coretse_set_mac_address(lp->dev, NULL);
	return 0;
}

static void mchp_coretse_stop(struct coretse *lp)
{
	struct coretse_queue *q = &lp->queues[0];
	int i;
	/* Disable PCDMA interrupts */
	writel_relaxed(0, lp->pcdma_regs + MM2S_PCDMA_INT_EN);
	writel_relaxed(0, lp->pcdma_regs + S2MM_PCDMA_INT_EN);
	/* Disable Receiver and Transmitter */
	writel_relaxed(0, lp->regs + CORETSE_CONFIG1);
	/* Free any in-flight TX skbs */
	for (i = 0; i < PCDMA_MAX_TX_DESCR; i++) {
		if (q->tx_skb[i].skb) {
			dma_unmap_single(&lp->pdev->dev, q->tx_skb[i].mapping,
					 q->tx_skb[i].size, DMA_TO_DEVICE);
			dev_kfree_skb_any(q->tx_skb[i].skb);
			q->tx_skb[i].skb = NULL;
		}
	}
	mchp_coretse_free_coherent(lp);
}

/* ----------------------------------------------------------------
 * Net device open / close
 * ----------------------------------------------------------------
 * Cleaner ordering:
 *  1. start HW (alloc rings, program descriptors, enable MAC/DMA)
 *  2. enable NAPI
 *  3. connect/start phylink
 *  4. start TX queue
 */
static int mchp_coretse_open(struct net_device *dev)
{
	struct coretse *lp = netdev_priv(dev);
	int ret;
	/* 1. start HW first */
	ret = mchp_coretse_start(lp);
	if (ret)
		return ret;
	/* 2. enable NAPI before link can start delivering packets */
	napi_enable(&lp->queues[0].napi_rx);
	/* 3. connect and start phylink (skip if no PHY / TSN endpoint) */
	if (!lp->nophy) {
		ret = coretse_phylink_connect(lp);
		if (ret)
			goto err_napi;
	}
	/* 4. allow the stack to push TX */
	netif_start_queue(dev);
	/* 5. TSN endpoint with no PHY: force carrier on */
	if (lp->nophy) {
		if (netif_running(dev)) {
			netif_carrier_on(dev);
			netdev_info(dev, "TSN endpoint: carrier forced on\n");
		}
	}
	return 0;
err_napi:
	napi_disable(&lp->queues[0].napi_rx);
	mchp_coretse_stop(lp);
	return ret;
}

static int mchp_coretse_close(struct net_device *dev)
{
	struct coretse *lp = netdev_priv(dev);
	netif_stop_queue(dev);
	netif_carrier_off(dev);
	if (!lp->nophy) {
		phylink_stop(lp->phylink);
		phylink_disconnect_phy(lp->phylink);
	}
	napi_disable(&lp->queues[0].napi_rx);
	mchp_coretse_stop(lp);
	return 0;
}

/* ----------------------------------------------------------------
 * Transmit
 * ----------------------------------------------------------------
 * Uses PCDMA_MAX_TX_DESCR (4) descriptors in a ring.  The queue is
 * stopped when all slots are occupied; the TX ISR wakes it.
 *
 * NOTE: HW currently accepts one in-flight TX at a time via the
 *       MM2S_PCDMA_CTRL register; the 4-entry TX ring is primarily
 *       software bookkeeping to smooth out completion handling.
 */
static netdev_tx_t mchp_coretse_start_xmit(struct sk_buff *skb,
					   struct net_device *dev)
{
	struct coretse *lp = netdev_priv(dev);
	struct coretse_queue *q = &lp->queues[0];
	struct coretse_tx_skb *tx_entry;
	unsigned int desc_idx;
	unsigned long flags;

	dma_addr_t mapping;
	u32 addr0, addr1;

	spin_lock_irqsave(&lp->tx_lock, flags);
	if (lp->tx_count >= PCDMA_MAX_TX_DESCR) {
		spin_unlock_irqrestore(&lp->tx_lock, flags);
		netif_stop_queue(dev);
		netdev_dbg(dev, "%s: all TX descriptors busy\n", __func__);
		return NETDEV_TX_BUSY;
	}
	desc_idx = lp->tx_head % PCDMA_MAX_TX_DESCR;
	tx_entry = &q->tx_skb[desc_idx];
	mapping = dma_map_single(&lp->pdev->dev, skb->data, skb->len,
				 DMA_TO_DEVICE);
	if (dma_mapping_error(&lp->pdev->dev, mapping)) {
		spin_unlock_irqrestore(&lp->tx_lock, flags);
		dev_kfree_skb_any(skb);
		dev->stats.tx_dropped++;
		netdev_err(dev, "%s: DMA mapping error\n", __func__);
		return NETDEV_TX_OK;
	}
	tx_entry->skb = skb;
	tx_entry->mapping = mapping;
	tx_entry->size = skb->len;
	tx_entry->mapped_as_page = false;
	lp->tx_head++;
	lp->tx_count++;
	/* Stop queue if ring is now full */
	if (lp->tx_count >= PCDMA_MAX_TX_DESCR)
		netif_stop_queue(dev);
	addr0 = (u32)(mapping & MAPPING_MASK);
	addr1 = (u32)((mapping >> 32) & MAPPING_MASK);
	writel_relaxed(addr0, lp->pcdma_regs + MM2S_PCDMA_ADDR0);
	writel_relaxed(addr1, lp->pcdma_regs + MM2S_PCDMA_ADDR1);
	writel_relaxed(skb->len, lp->pcdma_regs + MM2S_PCDMA_LENGTH);
	/* Start transmission with cmd_id = desc_idx for tracking */
	writel_relaxed(((desc_idx << PCDMA_CMD_ID_OFFSET) |
			MCHP_MM2S_BURST_TYPE | MCHP_MM2S_START),
		       lp->pcdma_regs + MM2S_PCDMA_CTRL);
	spin_unlock_irqrestore(&lp->tx_lock, flags);
	return NETDEV_TX_OK;
}

/* ----------------------------------------------------------------
 * Ethtool / ioctl
 * ----------------------------------------------------------------
 */
#ifdef CONFIG_CORE1588_HWTSTAMP
static int mchp_core1588_get_ts_info(struct net_device *dev,
				     struct kernel_ethtool_ts_info *info)
{
	struct coretse *bp = netdev_priv(dev);
	if (bp->timer) {
		ethtool_op_get_ts_info(dev, info);
		info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
					SOF_TIMESTAMPING_RX_SOFTWARE |
					SOF_TIMESTAMPING_SOFTWARE |
					SOF_TIMESTAMPING_TX_HARDWARE |
					SOF_TIMESTAMPING_RX_HARDWARE |
					SOF_TIMESTAMPING_RAW_HARDWARE;
		info->tx_types = (1 << HWTSTAMP_TX_ONESTEP_SYNC) |
				 (1 << HWTSTAMP_TX_OFF) | (1 << HWTSTAMP_TX_ON);
		info->rx_filters = (1 << HWTSTAMP_FILTER_NONE) |
				   (1 << HWTSTAMP_FILTER_ALL);
		info->phc_index = bp->timer->ptp_clock ? bp->timer->phc_index :
							 -1;
	} else {
		info->phc_index = -1;
		netdev_info(
			dev,
			"PTP is not supported for this network interface\n");
	}
	return 0;
}
#endif

static int mchp_coretse_change_mtu(struct net_device *dev, int new_mtu)
{
	if (netif_running(dev))
		return -EBUSY;
	dev->mtu = new_mtu;
	return 0;
}

static int coretse_set_mac_address(struct net_device *ndev, void *addr)
{
	struct coretse *bp = netdev_priv(ndev);

	if (addr)
		eth_hw_addr_set(ndev, addr);
	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);
	writel_relaxed((ndev->dev_addr[0]) | (ndev->dev_addr[1] << 8) |
			       (ndev->dev_addr[2] << 16) |
			       (ndev->dev_addr[3] << 24),
		       bp->regs + CORETSE_STATION_ADDR0);
	writel_relaxed((ndev->dev_addr[4] << 16) | (ndev->dev_addr[5] << 24),
		       bp->regs + CORETSE_STATION_ADDR1);
	return 0;
}

static int mchp_coretse_set_mac_address(struct net_device *ndev, void *p)
{
	struct sockaddr *addr = p;

	coretse_set_mac_address(ndev, addr->sa_data);
	return 0;
}

static int mchp_coretse_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct coretse *bp = netdev_priv(dev);

	if (!netif_running(dev))
		return -EINVAL;
#ifdef CONFIG_CORE1588_HWTSTAMP
	switch (cmd) {
	case SIOCSHWTSTAMP:
		if (bp->timer && bp->timer->set_hwtst)
			return bp->timer->set_hwtst(bp->timer, rq, cmd);
		return -EOPNOTSUPP;
	case SIOCGHWTSTAMP:
		if (bp->timer && bp->timer->get_hwtst)
			return bp->timer->get_hwtst(bp->timer, rq);
		return -EOPNOTSUPP;
	}
#endif

	/* Handle phytool / mii-tool register access */
	if (!bp->nophy && bp->phylink)
		return phylink_mii_ioctl(bp->phylink, rq, cmd);

	return -EOPNOTSUPP;
}

/* ----------------------------------------------------------------
 * Net device ops / ethtool ops
 * ----------------------------------------------------------------
 */
static const struct net_device_ops mchp_coretse_netdev_ops = {
	.ndo_open = mchp_coretse_open,
	.ndo_stop = mchp_coretse_close,
	.ndo_start_xmit = mchp_coretse_start_xmit,
	.ndo_change_mtu = mchp_coretse_change_mtu,
	.ndo_set_mac_address = mchp_coretse_set_mac_address,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_eth_ioctl = mchp_coretse_ioctl,
};

static const struct ethtool_ops mchp_coretse_ethtool_ops = {
#ifdef CONFIG_CORE1588_HWTSTAMP
	.get_ts_info = mchp_core1588_get_ts_info,
#endif
	.get_sset_count = mchp_coretse_get_sset_count,
	.get_strings = mchp_coretse_get_strings,
	.get_ethtool_stats = mchp_coretse_get_ethtool_stats,
};

/* ----------------------------------------------------------------
 * Phylink / MII init
 * ----------------------------------------------------------------
 */
static int mchp_coretse_mii_probe(struct net_device *dev)
{
	struct coretse *bp = netdev_priv(dev);
	bp->phylink_config.dev = &dev->dev;
	bp->phylink_config.type = PHYLINK_NETDEV;
	bp->phylink_config.mac_capabilities = MAC_SYM_PAUSE | MAC_ASYM_PAUSE |
					      MAC_10FD | MAC_100FD | MAC_1000FD;
	bp->phylink_config.poll_fixed_state = true;

	__set_bit(PHY_INTERFACE_MODE_SGMII,
		  bp->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_1000BASEX,
		  bp->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_RGMII,
		  bp->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_RGMII_ID,
		  bp->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_RGMII_RXID,
		  bp->phylink_config.supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_RGMII_TXID,
		  bp->phylink_config.supported_interfaces);

	bp->phylink = phylink_create(&bp->phylink_config, bp->pdev->dev.fwnode,
				     bp->phy_interface, &coretse_phylink_ops);
	if (IS_ERR(bp->phylink)) {
		netdev_err(dev, "Could not create a phylink instance (%ld)\n",
			   PTR_ERR(bp->phylink));
		return PTR_ERR(bp->phylink);
	}
	return 0;
}

static int mchp_coretse_mdiobus_register(struct coretse *bp)
{
	struct device_node *child, *np = bp->pdev->dev.of_node;
	int ret;
	child = of_get_child_by_name(np, "mdio");
	if (child) {
		ret = of_mdiobus_register(bp->mii_bus, child);
		of_node_put(child);
		return ret;
	}
	if (of_phy_is_fixed_link(np))
		return mdiobus_register(bp->mii_bus);
	for_each_available_child_of_node(np, child)
		if (of_mdiobus_child_is_phy(child)) {
			of_node_put(child);
			return of_mdiobus_register(bp->mii_bus, np);
		}
	return mdiobus_register(bp->mii_bus);
}

static int mchp_coretse_mii_init(struct coretse *bp)
{
	struct device_node *np;
	int err = -ENXIO;
	bp->mii_bus = mdiobus_alloc();
	if (!bp->mii_bus) {
		err = -ENOMEM;
		goto err_out;
	}
	bp->mii_bus->name = "Microchip CoreTSE MDIO";
	bp->mii_bus->read = &mchp_coretse_mdio_read;
	bp->mii_bus->write = &mchp_coretse_mdio_write;
	snprintf(bp->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x", bp->pdev->name,
		 bp->pdev->id);
	bp->mii_bus->priv = bp;
	bp->mii_bus->parent = &bp->pdev->dev;
	dev_set_drvdata(&bp->dev->dev, bp->mii_bus);

	if (!bp->nophy) {
		np = of_parse_phandle(bp->pdev->dev.of_node, "pcs-handle", 0);
		if (!np)
			np = of_parse_phandle(bp->pdev->dev.of_node,
					      "phy-handle", 0);
		if (!np) {
			err = -EINVAL;
			goto err_out_free_mdiobus;
		}

		if (np) {
			err = mchp_coretse_mdiobus_register(bp);
			if (err)
				goto err_out_free_mdiobus;
		}

		of_node_put(np);
		bp->pcs.ops = &coretse_pcs_ops;
		bp->pcs.poll = true;

		err = mchp_coretse_mii_probe(bp->dev);
		if (err)
			goto err_out_unregister_bus;
	} else {
		/* TSN endpoint: register MDIO bus only (no PCS/phylink) */
		err = mchp_coretse_mdiobus_register(bp);
		if (err)
			goto err_out_free_mdiobus;
	}
	return 0;
err_out_unregister_bus:
	mdiobus_unregister(bp->mii_bus);
err_out_free_mdiobus:
	mdiobus_free(bp->mii_bus);
err_out:
	return err;
}

/* ----------------------------------------------------------------
 * PCDMA resource probe
 * ----------------------------------------------------------------
 */
static int mchp_pcdma_probe(struct platform_device *pdev, struct coretse *bp)
{
	struct device_node *np;
	struct resource res;
	char intr_name[24];
	int ret;

	np = of_parse_phandle(pdev->dev.of_node, "pcdma-connected", 0);
	if (IS_ERR(np)) {
		dev_err(&pdev->dev, "could not find pcdma node\n");
		return PTR_ERR(np);
	}

	ret = of_address_to_resource(np, 0, &res);
	if (ret) {
		dev_err(&pdev->dev, "unable to get pcdma resource\n");
		return ret;
	}

	bp->pcdma_regs = devm_ioremap_resource(&pdev->dev, &res);
	if (IS_ERR(bp->pcdma_regs)) {
		dev_err(&pdev->dev, "ioremap failed for the pcdma\n");
		return PTR_ERR(bp->pcdma_regs);
	}

	snprintf(intr_name, sizeof(intr_name), "s2mm_pcdma");

	bp->rx_irq = platform_get_irq_byname(pdev, intr_name);
	if (bp->rx_irq < 0)
		return bp->rx_irq;

	snprintf(intr_name, sizeof(intr_name), "mm2s_pcdma");

	bp->tx_irq = platform_get_irq_byname(pdev, intr_name);

	if (bp->tx_irq < 0)
		return bp->tx_irq;
	return 0;
}

/* ----------------------------------------------------------------
 * Hardware initialisation
 * ----------------------------------------------------------------
 */
static int mchp_coretse_hw_init(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct coretse *bp = netdev_priv(dev);
	int ret;
	u32 reg;
	bp->queues[0].bp = bp;
	dev->netdev_ops = &mchp_coretse_netdev_ops;
	dev->ethtool_ops = &mchp_coretse_ethtool_ops;
	ret = request_irq(bp->tx_irq, mchp_pcdma_tx_irq, IRQF_SHARED, dev->name,
			  dev);
	if (ret)
		return ret;
	ret = request_irq(bp->rx_irq, mchp_pcdma_rx_irq, IRQF_SHARED, dev->name,
			  dev);
	if (ret) {
		free_irq(bp->tx_irq, dev);
		return ret;
	}
	/* Reset all PE-MCXMAC modules and configure */
	reg = readl_relaxed(bp->regs + CORETSE_CONFIG2);
	reg &= ~(CORETSE_CFG2_MODE_MSK << CORETSE_CFG2_MODE_BIT);
	writel_relaxed(reg, bp->regs + CORETSE_CONFIG2);
	reg = readl_relaxed(bp->regs + CORETSE_CONFIG1);
	reg |= CORETSE_CFG1_RST;
	writel_relaxed(reg, bp->regs + CORETSE_CONFIG1);
	writel_relaxed(CORETSE_MGMT_CLOCK_SEL, bp->regs + CORETSE_MII_CONFIG);
	/* Assert all FIFO resets, then de-assert */
	writel_relaxed(CORETSE_FIFO_CFG0_ALL_RST,
		       bp->regs + CORETSE_FIFO_CONFIG0);
	reg = readl_relaxed(bp->regs + CORETSE_FIFO_CONFIG0);
	reg &= ~CORETSE_FIFO_CFG0_ALL_RST;
	writel_relaxed(reg, bp->regs + CORETSE_FIFO_CONFIG0);
	writel_relaxed(0, bp->regs + CORETSE_CONFIG1);
	reg = readl_relaxed(bp->regs + CORETSE_CONFIG2);
	reg &= ~(CORETSE_CFG2_MODE_MSK << CORETSE_CFG2_MODE_BIT);
	writel_relaxed(reg, bp->regs + CORETSE_CONFIG2);
	/* TBI or GMII */
	reg = CORETSE_CFG2_FULL_DUP | CORETSE_CFG2_CRC_EN |
	      CORETSE_CFG2_PAD_CRC | CORETSE_CFG2_LEN_CHECK |
	      (CORETSE_CFG2_MODE_BYTE << CORETSE_CFG2_MODE_BIT) |
	      (CORETSE_CFG2_PREAM_LEN_DEFAULT << CORETSE_CFG2_PREAM_LEN_BIT);
	writel_relaxed(reg, bp->regs + CORETSE_CONFIG2);
	writel_relaxed(IFG_VALUE, bp->regs + CORETSE_IFG);
	writel_relaxed(HALF_DUPLEX_VALUE, bp->regs + CORETSE_HALF_DUPLEX);
	writel_relaxed(CORETSE_MAX_RBUFF_SZ, bp->regs + CORETSE_MAX_FRAME_LEN);
	writel_relaxed(CORETSE_FIFO_CFG0_ALL_REQ,
		       bp->regs + CORETSE_FIFO_CONFIG0);
	writel_relaxed(FIFO_CONFIG1_VALUE, bp->regs + CORETSE_FIFO_CONFIG1);
	writel_relaxed(FIFO_CONFIG2_VALUE, bp->regs + CORETSE_FIFO_CONFIG2);
	writel_relaxed(FIFO_CONFIG3_VALUE, bp->regs + CORETSE_FIFO_CONFIG3);
	/* Filter out bad packets */
	reg = readl_relaxed(bp->regs + CORETSE_FIFO_CONFIG5);
	reg |= FIFO_CONFIG5_MASK;
	reg &= ~FIFO_CONFIG4_VALUE;
	writel_relaxed(reg, bp->regs + CORETSE_FIFO_CONFIG5);
	writel_relaxed(FIFO_CONFIG4_VALUE, bp->regs + CORETSE_FIFO_CONFIG4);
	writel_relaxed(0x0, bp->regs + CORETSE_MISCC);
	writel_relaxed(MCHP_FRAME_FILTER_CTRL, bp->regs + CORETSE_FPC);
	return 0;
}

/* ----------------------------------------------------------------
 * PTP init
 * ----------------------------------------------------------------
 */
static void mchp_core1588_ptp_init(struct coretse *bp)
{
	struct device_node *ptp_node;
	struct platform_device *ptp_dev = NULL;

	ptp_node = of_parse_phandle(bp->pdev->dev.of_node,
				    "microchip,core1588-ptp-handle", 0);
	if (ptp_node)
		ptp_dev = of_find_device_by_node(ptp_node);
	if (ptp_dev)
		bp->timer = platform_get_drvdata(ptp_dev);
	else
		bp->timer = NULL;
}

/* ----------------------------------------------------------------
 * Platform probe / remove
 * ----------------------------------------------------------------
 */
static int mchp_coretse_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	phy_interface_t interface;
	struct net_device *dev;
	u8 mac_addr[ETH_ALEN];
	struct coretse *bp;
	struct clk *pclk, *pcdma_clk;
	void __iomem *mem;
	int ret;

	mem = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mem))
		return PTR_ERR(mem);
	pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(pclk))
		return dev_err_probe(&pdev->dev, PTR_ERR(pclk),
				     "could not get pclk\n");
	pcdma_clk = devm_clk_get(&pdev->dev, "pcdma");
	if (IS_ERR(pcdma_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(pcdma_clk),
				     "could not get pcdma clock\n");
	ret = clk_prepare_enable(pclk);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret,
				     "failed to enable pclk\n");
	}
	ret = clk_prepare_enable(pcdma_clk);
	if (ret) {
		dev_err_probe(&pdev->dev, ret,
			      "failed to enable pcdma clock\n");
		goto err_disable_pclk;
	}
	dev = alloc_etherdev_mq(sizeof(*bp), 1);
	if (!dev) {
		ret = -ENOMEM;
		goto err_disable_clocks;
	}
	SET_NETDEV_DEV(dev, &pdev->dev);
	bp = netdev_priv(dev);
	bp->pdev = pdev;
	bp->dev = dev;
	bp->regs = mem;
	bp->clk = pclk;
	bp->dmaclk = pcdma_clk;
	bp->max_tx_length = MAX_TX_LENGTH;
	spin_lock_init(&bp->lock);
	spin_lock_init(&bp->tx_lock);
	spin_lock_init(&bp->rx_lock);
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "No suitable DMA available\n");
		goto err_out_free_netdev;
	}
	mchp_core1588_ptp_init(bp);
	platform_set_drvdata(pdev, dev);
	/* MTU range: 68 - 1500 */
	dev->min_mtu = ETH_MIN_MTU;
	dev->max_mtu = ETH_DATA_LEN;
	ret = mchp_pcdma_probe(pdev, bp);
	if (ret)
		goto err_out_free_netdev;
	/* Retrieve the MAC address */
	ret = of_get_mac_address(np, mac_addr);
	if (!ret) {
		coretse_set_mac_address(dev, mac_addr);
	} else {
		dev_warn(&pdev->dev,
			 "could not find MAC address property: %d\n", ret);
		coretse_set_mac_address(dev, NULL);
	}
	ret = of_get_phy_mode(np, &interface);
	if (ret)
		bp->phy_interface = PHY_INTERFACE_MODE_SGMII;
	else
		bp->phy_interface = interface;

	/* IP specific init */
	ret = mchp_coretse_hw_init(pdev);
	if (ret)
		goto err_out_free_netdev;

	/* Check if this is a TSN endpoint (no PHY attached) */
	bp->nophy = of_property_read_bool(np, "microchip,phy-null");
	if (bp->nophy)
		netdev_info(dev, "Configured as TSN endpoint (no PHY)\n");

	ret = mchp_coretse_mii_init(bp);
	if (ret)
		goto err_out_phy_exit;
	netif_carrier_off(dev);
	netif_napi_add(dev, &bp->queues[0].napi_rx, mchp_coretse_rx_poll);
	ret = register_netdev(dev);
	if (ret) {
		dev_err(&pdev->dev, "Cannot register net device, aborting.\n");
		goto err_out_cleanup_phylink;
	}
	netdev_info(dev, "CoreTSE probe done (RX desc: %d, TX desc: %d)\n",
		    PCDMA_MAX_RX_DESCR, PCDMA_MAX_TX_DESCR);
	return 0;
err_out_cleanup_phylink:
	netif_napi_del(&bp->queues[0].napi_rx);
	if (!bp->nophy)
		phylink_destroy(bp->phylink);
	mdiobus_unregister(bp->mii_bus);
	mdiobus_free(bp->mii_bus);

err_out_phy_exit:
	if (bp->pcs_phy)
		put_device(&bp->pcs_phy->dev);
err_out_free_netdev:
	free_netdev(dev);
err_disable_clocks:
	clk_disable_unprepare(pcdma_clk);
err_disable_pclk:
	clk_disable_unprepare(pclk);
	return ret;
}

static void mchp_coretse_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct coretse *bp = netdev_priv(ndev);

	unregister_netdev(ndev);
	netif_napi_del(&bp->queues[0].napi_rx);

	if (!bp->nophy && bp->phylink)
		phylink_destroy(bp->phylink);
	if (bp->pcs_phy)
		put_device(&bp->pcs_phy->dev);
	if (bp->mii_bus) {
		mdiobus_unregister(bp->mii_bus);
		mdiobus_free(bp->mii_bus);
	}

	free_irq(bp->tx_irq, ndev);
	free_irq(bp->rx_irq, ndev);
	clk_disable_unprepare(bp->clk);
	clk_disable_unprepare(bp->dmaclk);
	free_netdev(ndev);
}

static const struct of_device_id mchp_coretse_of_match[] = {
	{
		.compatible = "microchip,coretse-rtl-v3",
	},
	{}
};
MODULE_DEVICE_TABLE(of, mchp_coretse_of_match);

static struct platform_driver mchp_coretse_driver = {
	.probe = mchp_coretse_probe,
	.remove = mchp_coretse_remove,
	.driver = {
		 .name = "microchip_coretse",
		 .of_match_table = mchp_coretse_of_match,
	},
};

module_platform_driver(mchp_coretse_driver);
MODULE_AUTHOR("Praveen Kumar Vattipalli <praveen.kumar@microchip.com>");
MODULE_DESCRIPTION("Microchip CoreTSE(Ethernet) driver");
MODULE_LICENSE("GPL");
