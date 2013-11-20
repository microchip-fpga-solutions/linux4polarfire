#include <dt-bindings/dma/at91.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of_dma.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "dmaengine.h"
#include "at_xdmac.h"


static unsigned int init_nr_desc_per_channel = 64;
module_param(init_nr_desc_per_channel, uint, 0644);
MODULE_PARM_DESC(init_nr_desc_per_channel,
		 "initial descriptors per channel (default: 64)");

/**
 * at_xdmac_set_slave_config - set internal channel config register value
 * @chan:	The channel to configure.
 * @sconfig:    The configuration requested by the slave device.
 *
 * Set the internal representation of the channel configuration register
 * according to the dma slave configuration and to dt parameters. It also
 * save the dma slave configuration for future use (mainly for src and dst
 * addresses).
 *
 * Return: 0 on success, -EINVAL otherwise.
 */
static int at_xdmac_set_slave_config(struct dma_chan *chan,
				      struct dma_slave_config *sconfig)
{
	struct at_xdmac_chan *atchan = to_at_xdmac_chan(chan);

	atchan->cfg = AT91_XDMAC_DT_PERID(atchan->perid)
		      | AT91_XDMAC_DT_DWIDTH(atchan->dwidth)
		      | AT91_XDMAC_DT_CSIZE(atchan->csize)
		      |	AT_XDMAC_CC_SWREQ_HWR_CONNECTED
		      | AT91_XDMAC_DT_MBSIZE(atchan->mbsize)
		      | AT_XDMAC_CC_TYPE_PER_TRAN;

	if (sconfig->direction == DMA_DEV_TO_MEM) {
		atchan->cfg |= AT_XDMAC_CC_DAM_INCREMENTED_AM
			       | AT_XDMAC_CC_SAM_FIXED_AM
			       | AT_XDMAC_CC_DIF(atchan->memif)
			       | AT_XDMAC_CC_SIF(atchan->perif)
			       | AT_XDMAC_CC_DSYNC_PER2MEM;
	} else if (sconfig->direction == DMA_MEM_TO_DEV) {
		atchan->cfg |= AT_XDMAC_CC_DAM_FIXED_AM
			       | AT_XDMAC_CC_SAM_INCREMENTED_AM
			       | AT_XDMAC_CC_DIF(atchan->perif)
			       | AT_XDMAC_CC_SIF(atchan->memif)
			       | AT_XDMAC_CC_DSYNC_MEM2PER;
	} else {
		return -EINVAL;
	}

	memcpy(&atchan->dma_sconfig, sconfig, sizeof(struct dma_slave_config));

	dev_dbg(chan2dev(chan), "%s: atchan->cfg=0x%08x\n", __func__, atchan->cfg);

	return 0;
}

/**
 * at_xdmac_off - disable all channels and all interrupts
 * @atxdmac:	atmel xdma controller
 */
static void at_xdmac_off(struct at_xdmac *atxdmac)
{
	at_xdmac_write(atxdmac, AT_XDMAC_GD, -1L);
	while (at_xdmac_read(atxdmac, AT_XDMAC_GS))
		cpu_relax();

	at_xdmac_write(atxdmac, AT_XDMAC_GID, -1L);
	/* need to read some registers to clean pending irqs? */
}

/**
 * at_xdmac_xlate - converts phandle args list into a dma_chan structure
 * @dma_spec:	Device phandle args list.
 * @of_dma:
 *
 * Request a slave channel and set slave device dependant channel parameters.
 *
 * Return: dma channel.
 */
static struct dma_chan *at_xdmac_xlate(struct of_phandle_args *dma_spec,
				       struct of_dma *of_dma)
{
	struct at_xdmac_chan	*atchan;
	struct dma_chan		*chan;
	dma_cap_mask_t 		mask;
	struct platform_device	*pdev = of_find_device_by_node(dma_spec->np);

	if (dma_spec->args_count != 2) {
		dev_err(&pdev->dev, "2 args must be provided after the phandler instead of %d\n", dma_spec->args_count);
		return NULL;
	}

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	/* do I need to pass a filter function? */
	chan = dma_request_channel(mask, NULL, NULL);
	if (!chan)
		return NULL;

	atchan = to_at_xdmac_chan(chan);
	atchan->memif = AT91_XDMAC_DT_GET_MEM_IF(dma_spec->args[0]);
	atchan->perif = AT91_XDMAC_DT_GET_PER_IF(dma_spec->args[0]);
	atchan->perid = AT91_XDMAC_DT_GET_PERID(dma_spec->args[1]);
	atchan->dwidth = AT91_XDMAC_DT_GET_DWIDTH(dma_spec->args[1]);
	atchan->csize = AT91_XDMAC_DT_GET_CSIZE(dma_spec->args[1]);
	atchan->mbsize = AT91_XDMAC_DT_GET_MBSIZE(dma_spec->args[1]);
	dev_info(&pdev->dev, "chan dt cfg: memif=%u perif=%u perid=%u dwidth=%u csize=%u mbsize=%u\n",
		 atchan->memif, atchan->perif, atchan->perid, atchan->dwidth, atchan->csize, atchan->mbsize);

	return chan;
}

static bool at_xdmac_chan_is_enabled(struct at_xdmac_chan *atchan)
{
	return at_xdmac_chan_read(atchan, AT_XDMAC_GS) & atchan->mask;
}

/*
 * at_xdmac_start_xfer - start a dma transfer
 * @atchan: atmel xdmac chan used.
 * @first: first transfer descriptor.
 *
 * Set transfer as active to indicate that this transfer is in progress. Write
 * the channel configuration and fill CNDA and CNDC to retrieve the first item
 * of the linked list. Enable end of linked list interrupt and enable the
 * channel.
 */
static void at_xdmac_start_xfer(struct at_xdmac_chan *atchan,
				struct at_xdmac_desc *first)
{
	struct at_xdmac	*atxdmac = to_at_xdmac(atchan->chan.device);
	u32 reg;

	dev_vdbg(chan2dev(&atchan->chan), "%s: desc 0x%p\n", __func__, first);

	/* check if chan is enabled */
	if (at_xdmac_chan_is_enabled(atchan)) {
		dev_err(chan2dev(&atchan->chan),
			"BUG: Attempted to start a non-idle channel\n");
		return;
	}

	first->active_xfer = true;

	at_xdmac_chan_write(atchan, AT_XDMAC_CC, atchan->cfg);

	reg = AT_XDMAC_CNDA_NDA(first->tx_dma_desc.phys)
	      | AT_XDMAC_CNDA_NDAIF(atchan->memif);
	at_xdmac_chan_write(atchan, AT_XDMAC_CNDA, reg);

	reg = AT_XDMAC_CNDC_NDVIEW_NDV1
	      | AT_XDMAC_CNDC_NDDUP
	      | AT_XDMAC_CNDC_NDSUP
	      | AT_XDMAC_CNDC_NDE;
	at_xdmac_chan_write(atchan, AT_XDMAC_CNDC, reg);

	dev_vdbg(chan2dev(&atchan->chan),
		 "%s: XDMAC_CC=0x%08x XDMAC_CNDA=0x%08x, XDMAC_CNDC=0x%08x, "
		 "XDMAC_CSA=0x%08x, XDMAC_CDA=0x%08x, XDMAC_CUBC=0x%08x\n",
		 __func__, at_xdmac_chan_read(atchan, AT_XDMAC_CC),
		 at_xdmac_chan_read(atchan, AT_XDMAC_CNDA),
		 at_xdmac_chan_read(atchan, AT_XDMAC_CNDC),
		 at_xdmac_chan_read(atchan, AT_XDMAC_CSA),
		 at_xdmac_chan_read(atchan, AT_XDMAC_CDA),
		 at_xdmac_chan_read(atchan, AT_XDMAC_CUBC));

	/* need to enable other interrupts? */
	at_xdmac_chan_write(atchan, AT_XDMAC_CIE, 0x2); /* TODO: macro */
	/*
	 * There is no end of list when doing cyclic dma, we need to get
	 * an interrupt after each periods.
	 */
	if (at_xdmac_chan_is_cyclic(atchan))
		at_xdmac_chan_write(atchan, AT_XDMAC_CIE, 0x1);
	at_xdmac_write(atxdmac, AT_XDMAC_GIE, atchan->mask);
	dev_vdbg(chan2dev(&atchan->chan),
		 "%s: enable channel (0x%08x)\n", __func__, atchan->mask);
	at_xdmac_write(atxdmac, AT_XDMAC_GE, atchan->mask);
}

static void at_xdmac_terminate_xfer(struct at_xdmac_chan *atchan,
				    struct at_xdmac_desc *desc)
{
	struct dma_async_tx_descriptor	*txd = &desc->tx_dma_desc;
	dma_async_tx_callback		callback = txd->callback;
	void				*param = txd->callback_param;

	dev_dbg(chan2dev(&atchan->chan), "%s: desc 0x%p\n", __func__, desc);

	/* use lock here or before calling this function */

	/*
	 * It is necessary to do this before calling the callback since some
	 * devices will call dma_engine_terminate all causing to do
	 * dma_cookie_complete two times on the same cookie. I don't why
	 * spinlock doesn't prevent this situation...
	 */
	desc->active_xfer = false;
	list_del(&desc->xfer_node);

	/* mark the descriptor as complete */
	if (!at_xdmac_chan_is_cyclic(atchan))
		dma_cookie_complete(txd);

	/* free descriptors used for the xfer */
	if (async_tx_test_ack(txd)) {
		list_splice_init(&desc->descs_list, &atchan->free_descs_list);
	} else {
		dev_dbg(chan2dev(&atchan->chan),
			"%s: desc 0x%p not ACKed\n", __func__, desc);
	}

	if (!at_xdmac_chan_is_cyclic(atchan)) {
		if (callback && (txd->flags & DMA_PREP_INTERRUPT))
			callback(param);
	}

	dma_run_dependencies(txd);
}

/**
 * at_xdmac_advance_work - 
 * @atchan:	atmel xdmac channel
 *
 * If channel is enabled, do nothing, advance work will be triggered after
 * interruption. If transfer list is not empty, get the first transfer and
 * start it if not active otherwise...
 */
static void at_xdmac_advance_work(struct at_xdmac_chan *atchan)
{
	struct at_xdmac_desc *desc;
	unsigned long flags;

	spin_lock_irqsave(&atchan->lock, flags);
	if (at_xdmac_chan_is_enabled(atchan)) {
		dev_dbg(chan2dev(&atchan->chan), "%s: chan enabled\n",
			 __func__);
		return;
	}

	if (list_empty(&atchan->xfers_list)) {
		dev_dbg(chan2dev(&atchan->chan), "%s: xfers list empty\n",
			 __func__);
		return;
	}

	desc = list_first_entry(&atchan->xfers_list,
				struct at_xdmac_desc,
				xfer_node);
	dev_vdbg(chan2dev(&atchan->chan), "%s: desc 0x%p\n", __func__, desc);
	if (!desc->active_xfer)
		at_xdmac_start_xfer(atchan, desc);
	else
		at_xdmac_terminate_xfer(atchan, desc);

	spin_unlock_irqrestore(&atchan->lock, flags);
}

/*
 * Retrieve descriptor list from the dma tx descriptor and put it in the
 * transfer list. Then call advance work to start the transfer.
 */
static dma_cookie_t at_xdmac_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct at_xdmac_desc	*desc = txd_to_at_desc(tx);
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(tx->chan);
	dma_cookie_t		cookie;
	unsigned long		flags;

	spin_lock_irqsave(&atchan->lock, flags);
	cookie = dma_cookie_assign(tx);

	dev_vdbg(chan2dev(tx->chan), "%s: add desc 0x%p to xfers_list\n",
		 __func__, desc);
	list_add_tail(&desc->xfer_node, &atchan->xfers_list);
	if (list_is_singular(&atchan->xfers_list))
		at_xdmac_start_xfer(atchan, desc);

	spin_unlock_irqrestore(&atchan->lock, flags);

	return cookie;
}

static struct at_xdmac_desc *at_xdmac_alloc_descriptor(struct dma_chan *chan,
						       gfp_t gfp_flags)
{
	struct at_xdmac_desc	*desc;
	struct at_xdmac		*atxdmac = to_at_xdmac(chan->device);
	dma_addr_t		phys;

	desc = dma_pool_alloc(atxdmac->at_xdmac_desc_pool, gfp_flags, &phys);
	if (desc) {
		memset(desc, 0, sizeof(*desc));
		INIT_LIST_HEAD(&desc->descs_list);
		dma_async_tx_descriptor_init(&desc->tx_dma_desc, chan);
		desc->tx_dma_desc.flags = DMA_CTRL_ACK;
		desc->tx_dma_desc.tx_submit = at_xdmac_tx_submit;
		desc->tx_dma_desc.phys = phys;
	}

	return desc;
}

static struct at_xdmac_desc *at_xdmac_get_desc(struct at_xdmac_chan *atchan)
{
	struct at_xdmac_desc	*desc, *_desc, *free_desc = NULL;
	unsigned long 		flags;

	spin_lock_irqsave(&atchan->lock, flags);
	list_for_each_entry_safe(desc, _desc, &atchan->free_descs_list, desc_node) {
		if (async_tx_test_ack(&desc->tx_dma_desc)) {
			list_del(&desc->desc_node);
			free_desc = desc;
			break;
		}
		dev_dbg(chan2dev(&atchan->chan),
			"%s: desc 0x%p not ACKed\n", __func__, desc);
	}
	spin_unlock_irqrestore(&atchan->lock, flags);

	if (!free_desc) {
		dev_dbg(chan2dev(&atchan->chan),
			 "%s: not enough descriptors available\n", __func__);
		free_desc = at_xdmac_alloc_descriptor(&atchan->chan, GFP_ATOMIC);
		if (free_desc) {
			spin_lock_irqsave(&atchan->lock, flags);
			atchan->descs_allocated++;
			dev_dbg(chan2dev(&atchan->chan),
				"%s: descriptor allocated (total = %u\n)",
				__func__,  atchan->descs_allocated++);
			spin_unlock_irqrestore(&atchan->lock, flags);
		} else {
			dev_err(chan2dev(&atchan->chan),
				"can't allocate new descriptor\n");
		}
	}

	return free_desc;
}

/**
 * at_xdmac_tx_status -
 * @chan
 * @cookie
 * @txstate
 *
 * This function updates txstate to return the dma transfer residue. To know
 * the residue value with a good accuracy, the dma channel has to be partially
 * suspended i.e. only read or write depending on the dma transfer direction.
 * For instance, only write will be suspended for a per2mem transfer allowing
 * to continue to fill the DMA FIFO. Then we can flush the FIFO content in
 * memory. To know how many bytes have been already transfered, we have to look
 * which descriptor is used by the DMA then to browse the descriptor list
 * 
 *
 * Return: DMA_SUCCESS or DMA_ERROR
 */
static enum dma_status
at_xdmac_tx_status(struct dma_chan *chan, dma_cookie_t cookie,
		struct dma_tx_state *txstate)
{
	struct at_xdmac_chan 	*atchan = to_at_xdmac_chan(chan);
	struct at_xdmac		*atxdmac = to_at_xdmac(atchan->chan.device);
	struct at_xdmac_desc	*desc, *_desc;
	unsigned long		flags;
	enum dma_status		ret;
	int			residue, size_first_desc;
	u32			cur_nda;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_SUCCESS)
		return ret;

	if (!txstate)
		return DMA_ERROR;

	spin_lock_irqsave(&atchan->lock, flags);

	desc = list_first_entry(&atchan->xfers_list, struct at_xdmac_desc, xfer_node);
	size_first_desc = (desc->lld.mbr_ubc & 0xffffff) << atchan->dwidth;

	dev_dbg(chan2dev(chan),
		"%s: desc=0x%p, tx_dma_desc.phys=0x%08x\n",
		__func__, desc, desc->tx_dma_desc.phys);

	if (!desc->active_xfer)
		dev_err(chan2dev(chan),
			"something goes wrong, there is no active transfer\n");

	residue = desc->xfer_size;

	/* write channel suspend */
	at_xdmac_write(atxdmac, AT_XDMAC_GWS, atchan->mask);

	/* flush FIFO, resume is automatically done */
	at_xdmac_write(atxdmac, AT_XDMAC_GSWF, atchan->mask);
	while (!(at_xdmac_chan_read(atchan, AT_XDMAC_CIS) & AT_XDMAC_CIx_FIS));
		cpu_relax();

	cur_nda = at_xdmac_chan_read(atchan, AT_XDMAC_CNDA) & 0xfffffffc;
	/* 
	 * remove size of all microblocks already transferred and the current
	 * one
	 */
	list_for_each_entry_safe(desc, _desc, &desc->descs_list, desc_node) {
		residue -= (desc->lld.mbr_ubc & 0xffffff) << atchan->dwidth;
		if ((desc->lld.mbr_nda & 0xfffffffc) == cur_nda)
			break;
	}
	/* add the remaining size to transfer of the current microblock */
	residue += at_xdmac_chan_read(atchan, AT_XDMAC_CUBC) << atchan->dwidth;

	spin_unlock_irqrestore(&atchan->lock, flags);

	dma_set_residue(txstate, residue);

	dev_vdbg(chan2dev(chan), "%s: tx_status=%d, cookie=%d, residue=%d\n",
		 __func__, ret, cookie, residue);

	return ret;
}

static struct dma_async_tx_descriptor *
at_xdmac_prep_dma_cyclic(struct dma_chan *chan, dma_addr_t buf_addr,
			 size_t buf_len, size_t period_len,
			 enum dma_transfer_direction direction,
			 unsigned long flags, void *context)
{
	struct at_xdmac_chan 	*atchan = to_at_xdmac_chan(chan);
	struct dma_slave_config	*sconfig = &atchan->dma_sconfig;
	struct at_xdmac_desc	*first = NULL, *prev = NULL;
	unsigned int	periods = buf_len / period_len;
	int i;

	dev_dbg(chan2dev(chan), "%s: buf_addr=0x%08x, buf_len=%d, period_len=%d, "
		"dir=%s, flags=0x%lx\n",
		__func__, buf_addr, buf_len, period_len,
		direction == DMA_MEM_TO_DEV ? "mem2per" : "per2mem", flags);

	if (test_and_set_bit(AT_XDMAC_CHAN_IS_CYCLIC, &atchan->status)) {
		dev_dbg(chan2dev(chan), "%s: channel in use\n", __func__);
		return NULL;
	}

	for (i = 0; i < periods; i++) {
		struct at_xdmac_desc	*desc = NULL;

		desc = at_xdmac_get_desc(atchan);
		if (!desc) {
			dev_err(chan2dev(chan),
				"can't get descriptor\n");
			//goto TODO;
		}
		dev_dbg(chan2dev(chan),
			"%s: desc=0x%p, tx_dma_desc.phys=0x%08x\n",
			__func__, desc, desc->tx_dma_desc.phys);

		switch (direction) {
		case DMA_DEV_TO_MEM:
			desc->lld.mbr_sa = sconfig->src_addr;
			desc->lld.mbr_da = buf_addr + i * period_len;
			break;
		case DMA_MEM_TO_DEV:
			desc->lld.mbr_sa = buf_addr + i * period_len;
			desc->lld.mbr_da = sconfig->dst_addr;
			break;
		default:
			clear_bit(AT_XDMAC_CHAN_IS_CYCLIC, &atchan->status);
			return NULL;
		}
		desc->lld.mbr_ubc = AT_XDMAC_MBR_UBC_NDV1
			| AT_XDMAC_MBR_UBC_NDEN
			| AT_XDMAC_MBR_UBC_NSEN
			| AT_XDMAC_MBR_UBC_NDE
			| period_len / (1 << atchan->dwidth);

		dev_dbg(chan2dev(chan),
			 "%s: lld: mbr_sa = 0x%08x, mbr_da = 0x%08x, mbr_ubc = 0x%08x\n",
			 __func__, desc->lld.mbr_sa, desc->lld.mbr_da, desc->lld.mbr_ubc);

		/* chain lld */
		if (prev) {
			prev->lld.mbr_nda = desc->tx_dma_desc.phys;
			dev_dbg(chan2dev(chan),
				 "%s: chain lld: prev = 0x%p, mbr_nda = 0x%08x\n",
				 __func__, prev, prev->lld.mbr_nda);
		}

		prev = desc;
		if (!first)
			first = desc;

		dev_dbg(chan2dev(chan), "%s: add desc 0x%p to descs_list 0x%p\n",
			 __func__, desc, first);
		list_add_tail(&desc->desc_node, &first->descs_list);
	}

	prev->lld.mbr_nda = first->tx_dma_desc.phys;
	dev_dbg(chan2dev(chan),
		"%s: chain lld: prev = 0x%p, mbr_nda = 0x%08x\n",
		__func__, prev, prev->lld.mbr_nda);
	first->tx_dma_desc.cookie = -EBUSY;
	first->tx_dma_desc.flags = flags;
	first->xfer_size = buf_len;

	return &first->tx_dma_desc;
}

static struct dma_async_tx_descriptor *
at_xdmac_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		       unsigned int sg_len, enum dma_transfer_direction direction,
		       unsigned long flags, void *context)
{
	struct at_xdmac_chan 	*atchan = to_at_xdmac_chan(chan);
	struct dma_slave_config	*sconfig = &atchan->dma_sconfig;
	struct at_xdmac_desc	*first = NULL, *prev = NULL;
	struct scatterlist	*sg;
	int 			i;

	dev_dbg(chan2dev(chan), "%s: sg_len = %d, dir = %s, flags = 0x%lx\n",
		 __func__, sg_len,
		 direction == DMA_MEM_TO_DEV ? "to device" : "from device",
		 flags);

	/* prepare descriptors */
	for_each_sg(sgl, sg, sg_len, i) {
		struct at_xdmac_desc	*desc = NULL;
		u32			len, mem;

		desc = at_xdmac_get_desc(atchan);
		if (!desc) {
			dev_err(chan2dev(chan),
				"can't get descriptor\n");
			goto err_desc_get;
		}

		len = sg_dma_len(sg);
		mem = sg_dma_address(sg);
		if (unlikely(!len)) {
			dev_dbg(chan2dev(chan),
				"%s: sg(%d) data length is zero\n", __func__, i);
		}
		dev_dbg(chan2dev(chan), "%s: * sg%d len = %u, mem = 0x%08x\n",
			 __func__, i, len, mem);

		/* linked list descriptor */
		switch (direction) {
		case DMA_DEV_TO_MEM:
			desc->lld.mbr_sa = sconfig->src_addr;
			desc->lld.mbr_da = mem;
			break;
		case DMA_MEM_TO_DEV:
			desc->lld.mbr_sa = mem;
			desc->lld.mbr_da = sconfig->dst_addr;
			break;
		default:
			return NULL;
		}
		desc->lld.mbr_ubc = AT_XDMAC_MBR_UBC_NDV1
			| AT_XDMAC_MBR_UBC_NDEN
			| AT_XDMAC_MBR_UBC_NSEN
			| (i == sg_len - 1 ? 0 : AT_XDMAC_MBR_UBC_NDE)
			| len / (1 << atchan->dwidth);

		dev_dbg(chan2dev(chan),
			 "%s: lld: mbr_sa = 0x%08x, mbr_da = 0x%08x, mbr_ubc = 0x%08x\n",
			 __func__, desc->lld.mbr_sa, desc->lld.mbr_da, desc->lld.mbr_ubc);

		/* chain lld */
		if (prev) {
			prev->lld.mbr_nda = desc->tx_dma_desc.phys;
			dev_dbg(chan2dev(chan),
				 "%s: chain lld: prev = 0x%p, mbr_nda = 0x%08x\n",
				 __func__, prev, prev->lld.mbr_nda);
		}

		prev = desc;
		if (!first)
			first = desc;

		dev_dbg(chan2dev(chan), "%s: add desc 0x%p to descs_list 0x%p\n",
			 __func__, desc, first);
		list_add_tail(&desc->desc_node, &first->descs_list);
	}

	first->tx_dma_desc.cookie = -EBUSY;
	first->tx_dma_desc.flags = flags;
	first->xfer_size = sg_len;

	return &first->tx_dma_desc;

err_desc_get:
	/* TODO set descriptors taken as available */

	return NULL;

}

static void at_xdmac_handle_cyclic(struct at_xdmac_chan *atchan)
{
	struct at_xdmac_desc		*desc;
	struct dma_async_tx_descriptor	*txd;
	dma_async_tx_callback		callback;
	void				*param;

	desc = list_first_entry(&atchan->xfers_list, struct at_xdmac_desc, xfer_node);
	txd = &desc->tx_dma_desc;
	callback = txd->callback;
	param = txd->callback_param;

	if (callback && (txd->flags & DMA_PREP_INTERRUPT))
		callback(param);
}


static void at_xdmac_tasklet(unsigned long data)
{
	struct at_xdmac_chan *atchan = (struct at_xdmac_chan *)data;
	u32 error_mask;

	dev_dbg(chan2dev(&atchan->chan), "%s: status = 0x%08lx\n",
		 __func__, atchan->status);

	error_mask = AT_XDMAC_CIx_RBEIS
		     | AT_XDMAC_CIx_WBEIS
		     | AT_XDMAC_CIx_ROIS;

	if (atchan->status & error_mask) {
		dev_err(chan2dev(&atchan->chan), "%s: error\n", __func__);
		/* TODO fetch here? */
		return;
	} else if (at_xdmac_chan_is_cyclic(atchan)) {
		at_xdmac_handle_cyclic(atchan);
	} else if (atchan->status & AT_XDMAC_CIx_LIS) {
		/* start next job */
		at_xdmac_advance_work(atchan);
	}
}

static irqreturn_t at_xdmac_interrupt(int irq, void *dev_id)
{
	struct at_xdmac	*atxdmac = (struct at_xdmac *)dev_id;
	struct at_xdmac_chan *atchan;
	u32 imr, status, pending;
	u32 chan_imr, chan_status;
	int ret = IRQ_NONE;
	int i;

	do {
		imr = at_xdmac_read(atxdmac, AT_XDMAC_GIM);
		status = at_xdmac_read(atxdmac, AT_XDMAC_GIS);
		pending = status & imr;

		dev_vdbg(atxdmac->dma.dev,
			 "%s: status=0x%08x, imr=0x%08x, pending=0x%08x\n",
			 __func__, status, imr, pending);

		if (!pending)
			break;

		for (i = 0; i < atxdmac->dma.chancnt; i++) {
			/*
			 * If there is no interrupt for this channel, check
			 * next one, else retrieve channel status.
			 */
			if (!((1 << i) & pending))
				continue;

			atchan = &atxdmac->chan[i];
			chan_imr = at_xdmac_chan_read(atchan, AT_XDMAC_CIM);
			chan_status = at_xdmac_chan_read(atchan, AT_XDMAC_CIS);
			atchan->status = chan_status & chan_imr;
			dev_vdbg(atxdmac->dma.dev,
				 "%s: chan%d: imr = 0x%x, status = 0x%x\n",
				 __func__, i, chan_imr, chan_status);
			dev_vdbg(chan2dev(&atchan->chan),
				 "%s: XDMAC_CC=0x%08x XDMAC_CNDA=0x%08x, "
				 "XDMAC_CNDC=0x%08x, XDMAC_CSA=0x%08x, "
				 "XDMAC_CDA=0x%08x, XDMAC_CUBC=0x%08x\n",
				 __func__,
				 at_xdmac_chan_read(atchan, AT_XDMAC_CC),
				 at_xdmac_chan_read(atchan, AT_XDMAC_CNDA),
				 at_xdmac_chan_read(atchan, AT_XDMAC_CNDC),
				 at_xdmac_chan_read(atchan, AT_XDMAC_CSA),
				 at_xdmac_chan_read(atchan, AT_XDMAC_CDA),
				 at_xdmac_chan_read(atchan, AT_XDMAC_CUBC));

			if (atchan->status & (AT_XDMAC_CIx_RBEIS | AT_XDMAC_CIx_WBEIS)) {
				/* disable channel immediately */
			}

			tasklet_schedule(&atchan->tasklet);
			ret = IRQ_HANDLED;
		}

	} while (pending);

	return ret;
}

static void at_xdmac_issue_pending(struct dma_chan *chan)
{
	//printk("--- %s ---\n", __func__);

	return;
}

/**
 * at_xdmac_control - device_control dma routine
 * @chan:	dma channel on which cmd will be exercised
 * @cmd:	dma cmd to exercise
 * @arg:	cmd argument if needed
 *
 * Manage DMA_PAUSE, DMA_RESUME, DMA_TERMINATE_ALL and DMA_SLAVE_CONFIG
 * commands.
 *
 * Return: 0 on success, -ENXIO otherwise.
 */
static int at_xdmac_control(struct dma_chan *chan, enum dma_ctrl_cmd cmd,
			    unsigned long arg)
{
	struct at_xdmac_desc	*desc, *_desc;
	struct at_xdmac_chan	*atchan = to_at_xdmac_chan(chan);
	struct at_xdmac		*atxdmac = to_at_xdmac(atchan->chan.device);
	unsigned long		flags;

	dev_dbg(chan2dev(chan), "%s: cmd=%d\n", __func__, cmd);

	switch (cmd) {
	case DMA_PAUSE:
		/* TODO */
		dev_err(chan2dev(chan), "%s: PAUSE not yet implemented\n", __func__);
		break;
	case DMA_RESUME:
		/* TODO */
		dev_err(chan2dev(chan), "%s: RESUME not yet implemented\n", __func__);
		break;
	case DMA_TERMINATE_ALL:
		spin_lock_irqsave(&atchan->lock, flags);

		at_xdmac_write(atxdmac, AT_XDMAC_GIE, atchan->mask);
		at_xdmac_write(atxdmac, AT_XDMAC_GD, atchan->mask);
		/* TODO: use end of disable interrupt or polling? */
		while (at_xdmac_read(atxdmac, AT_XDMAC_GS) & atchan->mask)
			cpu_relax();

		/* terminate all pending transfers */
		list_for_each_entry_safe(desc, _desc, &atchan->xfers_list, xfer_node)
			at_xdmac_terminate_xfer(atchan, desc);

		clear_bit(AT_XDMAC_CHAN_IS_CYCLIC, &atchan->status);

		spin_unlock_irqrestore(&atchan->lock, flags);
		break;
	case DMA_SLAVE_CONFIG:
		return at_xdmac_set_slave_config(chan,
				(struct dma_slave_config *)arg);
	default:
		dev_dbg(chan2dev(chan), "%s: unmanaged or unknown dma cmd\n",
			 __func__);
		return -ENXIO;
	}

	return 0;
}

/*
 * at_xdmac_alloc_chan_resources - dma device_alloc_chan_resources routine
 * @chan: dma chan.
 *
 * Allocate atmel xdmac descriptors from dma pool and put them into the free
 * descriptors list of the channel.
 *
 * Returns: 0 on success, -EIO otherwise.
 */
static int at_xdmac_alloc_chan_resources(struct dma_chan *chan)
{
	struct at_xdmac_chan *atchan = to_at_xdmac_chan(chan);
	struct at_xdmac *atxdmac = to_at_xdmac(chan->device);
	struct at_xdmac_desc *desc;
	unsigned long flags;
	int i;
	LIST_HEAD(tmp_list);	/* do I need this list? */

	if (at_xdmac_chan_is_enabled(atchan)) {
		dev_dbg(chan2dev(chan), "%s: DMA channel not idle\n", __func__);
		return -EIO;
	}

	/* do I need to check if allocation has been done earlier? */
	for (i = 0; i < init_nr_desc_per_channel; i++) {
		desc = at_xdmac_alloc_descriptor(chan, GFP_KERNEL);
		if (!desc) {
			dev_err(atxdmac->dma.dev,
				"only %d descriptors have been allocated\n", i);
			break;
		}
		list_add_tail(&desc->desc_node, &tmp_list);
	}

	/* do I need to take the lock here? */
	spin_lock_irqsave(&atchan->lock, flags);
	atchan->descs_allocated = i;
	INIT_LIST_HEAD(&atchan->xfers_list);
	//atchan->remain_desc = 0;
	list_splice(&tmp_list, &atchan->free_descs_list);
	dma_cookie_init(chan);
	spin_unlock_irqrestore(&atchan->lock, flags);

	/* enable channel irq here? */

	dev_dbg(chan2dev(chan),
		"%s: allocated %d descriptors\n",
		__func__, atchan->descs_allocated);

	return atchan->descs_allocated;
}

static void at_xdmac_free_chan_resources(struct dma_chan *chan)
{
	return;
}

static int __init at_xdmac_probe(struct platform_device *pdev)
{
	struct resource *io;
	struct at_xdmac	*atxdmac;
	int irq, size, err, nr_channels, i, ret;
	dma_cap_mask_t cap_mask;
	void __iomem	*base;
	u32 reg;

	io = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!io)
		return -EINVAL;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	base = devm_ioremap_resource(&pdev->dev, io);
	if (IS_ERR(base))
		return PTR_ERR(base);
	dev_info(&pdev->dev, "remap done at 0x%p\n", base);

	/* 
	 * Read number of xdmac channels, read helper function can't be used
	 * since atxdmac is not yet allocated and we need to know the number
	 * of channels to do the allocation.
	 */
	reg = __raw_readl(base + AT_XDMAC_GTYPE);
	nr_channels = AT_XDMAC_NB_CH(reg);
	if (nr_channels > AT_XDMAC_MAX_CHAN) {
		dev_err(&pdev->dev, "invalid number of channels (%u)\n",
			nr_channels);
		return -EINVAL;
	}
	dev_info(&pdev->dev, "%u channels\n", nr_channels);

	size = sizeof(*atxdmac);
	size += nr_channels * sizeof(struct at_xdmac_chan);
	atxdmac = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!atxdmac) {
		dev_err(&pdev->dev, "can't allocate at_xdmac structure\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, atxdmac);

	atxdmac->regs = base;

	atxdmac->clk = devm_clk_get(&pdev->dev, "dma_clk");
	if (IS_ERR(atxdmac->clk)) {
		err = PTR_ERR(atxdmac->clk);
		return err;
	}

	ret = clk_prepare_enable(atxdmac->clk);
	if (ret) {
		dev_err(&pdev->dev, "can't prepare or enable clock\n");
		return ret;
	}

	dev_dbg(&pdev->dev, "%s: dma_pool_create (block size = %u)\n", __func__, sizeof(struct at_xdmac_desc));
	atxdmac->at_xdmac_desc_pool = dma_pool_create("at_xdmac_desc_pool",
						      &pdev->dev,
						      sizeof(struct at_xdmac_desc),
						      4 /* word alignment */,
						      0);
	if (!atxdmac->at_xdmac_desc_pool) {
		dev_err(&pdev->dev, "No memory for descriptors dma pool\n");
		return -ENOMEM;
	}

	/* set xdmac capabilities */
	dma_cap_set(DMA_CYCLIC, cap_mask);
	//dma_cap_set(DMA_MEMCPY, cap_mask);
	dma_cap_set(DMA_SLAVE, cap_mask);
	atxdmac->dma.cap_mask = cap_mask;
	/* all_chan_mask ? */

	/* set dma routines */
	atxdmac->dma.dev = &pdev->dev;
	atxdmac->dma.device_alloc_chan_resources = at_xdmac_alloc_chan_resources;
	atxdmac->dma.device_free_chan_resources = at_xdmac_free_chan_resources;
	atxdmac->dma.device_tx_status = at_xdmac_tx_status;
	atxdmac->dma.device_issue_pending = at_xdmac_issue_pending;
	atxdmac->dma.device_prep_dma_cyclic = at_xdmac_prep_dma_cyclic;
	//atxdmac->dma.device_prep_dma_memcpy = ;
	atxdmac->dma.device_prep_slave_sg = at_xdmac_prep_slave_sg;
	atxdmac->dma.device_control = at_xdmac_control;

	at_xdmac_off(atxdmac);

	/* init channels */
	INIT_LIST_HEAD(&atxdmac->dma.channels);
	for (i = 0; i < nr_channels; i++) {
		struct at_xdmac_chan *atchan = &atxdmac->chan[i];

		atchan->chan.device = &atxdmac->dma;
		list_add_tail(&atchan->chan.device_node,
			      &atxdmac->dma.channels);

		atchan->ch_regs = at_xdmac_chan_reg_base(atxdmac, i);
		atchan->mask = 1 << i;

		spin_lock_init(&atchan->lock);
		INIT_LIST_HEAD(&atchan->xfers_list);
		INIT_LIST_HEAD(&atchan->free_descs_list);
		tasklet_init(&atchan->tasklet, at_xdmac_tasklet,
			     (unsigned long)atchan);

		/* clear pending interrupt */
		while (at_xdmac_chan_read(atchan, AT_XDMAC_CIS))
			cpu_relax();

		/* atc_chan_enable irq ? move it on chan allocation */
	}

	dev_dbg(&pdev->dev, "%s: devm_request_irq\n", __func__);
	err = devm_request_irq(&pdev->dev, irq, at_xdmac_interrupt, 0,
			       "at_xdmac", atxdmac);
	/* TODO: manage error case */

	dma_async_device_register(&atxdmac->dma);

	dev_dbg(&pdev->dev, "%s: of_dma_controller_register\n", __func__);
	err = of_dma_controller_register(pdev->dev.of_node,
					 at_xdmac_xlate, atxdmac);
	if (err) {
		dev_err(&pdev->dev, "could not register of dma controller\n");
	}
	/* TODO of_dma_controller_free */

	dev_info(&pdev->dev, "Atmel XDMA Controller ( %s%s), %d channels\n",
		 dma_has_cap(DMA_MEMCPY, atxdmac->dma.cap_mask) ? "cpy " : "",
		 dma_has_cap(DMA_SLAVE, atxdmac->dma.cap_mask) ? "slave " : "",
		 nr_channels);

	return 0;
}

static int at_xdmac_remove(struct platform_device *pdev)
{
	/* TODO */
	return 0;
}

static void at_xdmac_shutdown(struct platform_device *pdev)
{
	/* TODO */
	return;
}

static const struct of_device_id atmel_xdmac_dt_ids[] = {
	{
		.compatible = "atmel,sama5d4-dma",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, atmel_xdmac_dt_ids);

static struct platform_driver at_xdmac_driver = {
	.remove		= at_xdmac_remove,
	.shutdown	= at_xdmac_shutdown,
	.driver = {
		.name		= "at_xdmac",
		.of_match_table	= of_match_ptr(atmel_xdmac_dt_ids),
	}
};

static int __init at_xdmac_init(void)
{
	return platform_driver_probe(&at_xdmac_driver, at_xdmac_probe);
}
subsys_initcall(at_xdmac_init);

static void __exit at_xdmac_exit(void)
{
	platform_driver_unregister(&at_xdmac_driver);
}
module_exit(at_xdmac_exit);

MODULE_DESCRIPTION("Atmel Extended DMA Controller driver");
MODULE_AUTHOR("Ludovic Desroches <ludovic.desroches@atmel.com>");
MODULE_LICENSE("GPL");
