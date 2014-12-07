/*
 * Xilinx Central DMA Engine support
 *
 * Copyright (C) 2010 - 2013 Xilinx, Inc. All rights reserved.
 *
 * Based on the Freescale DMA driver.
 *
 * Description:
 *  . Axi CDMA engine, it does transfers between memory and memory
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/amba/xilinx_dma.h>
#include <linux/dmapool.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* Hw specific definitions */
#define XILINX_CDMA_MAX_TRANS_LEN	0x7FFFFF /* Max transfer length */

/* Register Offsets */
#define XILINX_CDMA_CONTROL_OFFSET	0x00 /* Control Reg */
#define XILINX_CDMA_STATUS_OFFSET	0x04 /* Status Reg */
#define XILINX_CDMA_CDESC_OFFSET	0x08 /* Current descriptor Reg */
#define XILINX_CDMA_TDESC_OFFSET	0x10 /* Tail descriptor Reg */
#define XILINX_CDMA_SRCADDR_OFFSET	0x18 /* Source Address Reg */
#define XILINX_CDMA_DSTADDR_OFFSET	0x20 /* Dest Address Reg */
#define XILINX_CDMA_BTT_OFFSET		0x28 /* Bytes to transfer Reg */

/* General register bits definitions */
#define XILINX_CDMA_CR_RESET_MASK	0x00000004 /* Reset DMA engine */

#define XILINX_CDMA_SR_IDLE_MASK	0x00000002 /* DMA channel idle */

#define XILINX_CDMA_XR_IRQ_IOC_MASK	0x00001000 /* Completion interrupt */
#define XILINX_CDMA_XR_IRQ_DELAY_MASK	0x00002000 /* Delay interrupt */
#define XILINX_CDMA_XR_IRQ_ERROR_MASK	0x00004000 /* Error interrupt */
#define XILINX_CDMA_XR_IRQ_ALL_MASK	0x00007000 /* All interrupts */

#define XILINX_CDMA_XR_DELAY_MASK	0xFF000000 /* Delay timeout counter */
#define XILINX_CDMA_XR_COALESCE_MASK	0x00FF0000 /* Coalesce counter */

#define XILINX_CDMA_DELAY_SHIFT		24 /* Delay counter shift */
#define XILINX_CDMA_COALESCE_SHIFT	16 /* Coaelsce counter shift */

#define XILINX_CDMA_DELAY_MAX		0xFF /* Maximum delay counter value */
/* Maximum coalescing counter value */
#define XILINX_CDMA_COALESCE_MAX	0xFF

#define XILINX_CDMA_CR_SGMODE_MASK	0x00000008 /* Scatter gather mode */

/* BD definitions for Axi Cdma */
#define XILINX_CDMA_BD_STS_ALL_MASK	0xF0000000

/* Feature encodings */
#define XILINX_CDMA_FTR_DATA_WIDTH_MASK	0x000000FF /* Data width mask, 1024 */
#define XILINX_CDMA_FTR_HAS_SG		0x00000100 /* Has SG */
#define XILINX_CDMA_FTR_HAS_SG_SHIFT	8 /* Has SG shift */

/* Delay loop counter to prevent hardware failure */
#define XILINX_CDMA_RESET_LOOP	1000000
#define XILINX_CDMA_HALT_LOOP	1000000

/* Hardware descriptor */
struct xilinx_cdma_desc_hw {
	u32 next_desc;	/* 0x00 */
	u32 pad1;	/* 0x04 */
	u32 src_addr;	/* 0x08 */
	u32 pad2;	/* 0x0C */
	u32 dest_addr;	/* 0x10 */
	u32 pad3;	/* 0x14 */
	u32 control;	/* 0x18 */
	u32 status;	/* 0x1C */
} __aligned(64);

/* Software descriptor */
struct xilinx_cdma_desc_sw {
	struct xilinx_cdma_desc_hw hw;
	struct list_head node;
	struct list_head tx_list;
	struct dma_async_tx_descriptor async_tx;
} __aligned(64);

/* Per DMA specific operations should be embedded in the channel structure */
struct xilinx_cdma_chan {
	void __iomem *regs;			/* Control status registers */
	dma_cookie_t completed_cookie;		/* Maximum cookie completed */
	dma_cookie_t cookie;			/* The current cookie */
	spinlock_t lock;			/* Descriptor operation lock */
	bool sg_waiting;			/* SG transfer waiting */
	struct list_head active_list;		/* Active descriptors */
	struct list_head pending_list;		/* Descriptors waiting */
	struct dma_chan common;			/* DMA common channel */
	struct dma_pool *desc_pool;		/* Descriptors pool */
	struct device *dev;			/* The dma device */
	int irq;				/* Channel IRQ */
	int id;					/* Channel ID */
	enum dma_transfer_direction direction;	/* Transfer direction */
	int max_len;				/* Max data len per transfer */
	bool is_lite;				/* Whether is light build */
	bool has_sg;				/* Support scatter transfers */
	bool has_dre;				/* For unaligned transfers */
	int err;				/* Channel has errors */
	struct tasklet_struct tasklet;		/* Cleanup work after irq */
	u32 feature;				/* IP feature */
	u32 private;				/* Match info for
							channel request */
	void (*start_transfer)(struct xilinx_cdma_chan *chan);
	struct xilinx_cdma_config config;	/* Device configuration info */
};

struct xilinx_cdma_device {
	void __iomem *regs;
	struct device *dev;
	struct dma_device common;
	struct xilinx_cdma_chan *chan;
	u32 feature;
};

#define to_xilinx_chan(chan) \
	container_of(chan, struct xilinx_cdma_chan, common)

/* IO accessors */
static inline void
cdma_write(struct xilinx_cdma_chan *chan, u32 reg, u32 val)
{
	writel(val, chan->regs + reg);
}

static inline u32 cdma_read(struct xilinx_cdma_chan *chan, u32 reg)
{
	return readl(chan->regs + reg);
}

/* Required functions */

static int xilinx_cdma_alloc_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(dchan);

	/* Has this channel already been allocated? */
	if (chan->desc_pool)
		return 1;

	/*
	 * We need the descriptor to be aligned to 64bytes
	 * for meeting Xilinx DMA specification requirement.
	 */
	chan->desc_pool =
		dma_pool_create("xilinx_cdma_desc_pool",
				chan->dev,
				sizeof(struct xilinx_cdma_desc_sw),
				__alignof__(struct xilinx_cdma_desc_sw), 0);
	if (!chan->desc_pool) {
		dev_err(chan->dev,
			"unable to allocate channel %d descriptor pool\n",
			chan->id);
		return -ENOMEM;
	}

	chan->completed_cookie = 1;
	chan->cookie = 1;

	/* there is at least one descriptor free to be allocated */
	return 1;
}

static void xilinx_cdma_free_desc_list(struct xilinx_cdma_chan *chan,
				       struct list_head *list)
{
	struct xilinx_cdma_desc_sw *desc, *_desc;

	list_for_each_entry_safe(desc, _desc, list, node) {
		list_del(&desc->node);
		dma_pool_free(chan->desc_pool, desc, desc->async_tx.phys);
	}
}

static void xilinx_cdma_free_desc_list_reverse(struct xilinx_cdma_chan *chan,
					       struct list_head *list)
{
	struct xilinx_cdma_desc_sw *desc, *_desc;

	list_for_each_entry_safe_reverse(desc, _desc, list, node) {
		list_del(&desc->node);
		dma_pool_free(chan->desc_pool, desc, desc->async_tx.phys);
	}
}

static void xilinx_cdma_free_chan_resources(struct dma_chan *dchan)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(dchan);
	unsigned long flags;

	dev_dbg(chan->dev, "Free all channel resources.\n");
	spin_lock_irqsave(&chan->lock, flags);
	xilinx_cdma_free_desc_list(chan, &chan->active_list);
	xilinx_cdma_free_desc_list(chan, &chan->pending_list);
	spin_unlock_irqrestore(&chan->lock, flags);

	dma_pool_destroy(chan->desc_pool);
	chan->desc_pool = NULL;
}

static enum dma_status xilinx_cdma_desc_status(struct xilinx_cdma_chan *chan,
					       struct xilinx_cdma_desc_sw *desc)
{
	return dma_async_is_complete(desc->async_tx.cookie,
				     chan->completed_cookie,
				     chan->cookie);
}

static void xilinx_cdma_chan_desc_cleanup(struct xilinx_cdma_chan *chan)
{
	struct xilinx_cdma_desc_sw *desc, *_desc;
	unsigned long flags;

	spin_lock_irqsave(&chan->lock, flags);

	list_for_each_entry_safe(desc, _desc, &chan->active_list, node) {
		dma_async_tx_callback callback;
		void *callback_param;

		if (xilinx_cdma_desc_status(chan, desc) == DMA_IN_PROGRESS)
			break;

		/* Remove from the list of running transactions */
		list_del(&desc->node);

		/* Run the link descriptor callback function */
		callback = desc->async_tx.callback;
		callback_param = desc->async_tx.callback_param;
		if (callback) {
			spin_unlock_irqrestore(&chan->lock, flags);
			callback(callback_param);
			spin_lock_irqsave(&chan->lock, flags);
		}

		/* Run any dependencies, then free the descriptor */
		dma_run_dependencies(&desc->async_tx);
		dma_pool_free(chan->desc_pool, desc, desc->async_tx.phys);
	}

	spin_unlock_irqrestore(&chan->lock, flags);
}

static enum dma_status xilinx_tx_status(struct dma_chan *dchan,
					dma_cookie_t cookie,
					struct dma_tx_state *txstate)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(dchan);
	dma_cookie_t last_used;
	dma_cookie_t last_complete;

	xilinx_cdma_chan_desc_cleanup(chan);

	last_used = dchan->cookie;
	last_complete = chan->completed_cookie;

	dma_set_tx_state(txstate, last_complete, last_used, 0);

	return dma_async_is_complete(cookie, last_complete, last_used);
}

static int cdma_is_idle(struct xilinx_cdma_chan *chan)
{
	return cdma_read(chan, XILINX_CDMA_STATUS_OFFSET) &
	       XILINX_CDMA_SR_IDLE_MASK;
}

/* Only needed for Axi CDMA v2_00_a or earlier core */
static void cdma_sg_toggle(struct xilinx_cdma_chan *chan)
{
	cdma_write(chan, XILINX_CDMA_CONTROL_OFFSET,
		   cdma_read(chan, XILINX_CDMA_CONTROL_OFFSET) &
		   ~XILINX_CDMA_CR_SGMODE_MASK);

	cdma_write(chan, XILINX_CDMA_CONTROL_OFFSET,
		   cdma_read(chan, XILINX_CDMA_CONTROL_OFFSET) |
		   XILINX_CDMA_CR_SGMODE_MASK);
}

static void xilinx_cdma_start_transfer(struct xilinx_cdma_chan *chan)
{
	unsigned long flags;
	struct xilinx_cdma_desc_sw *desch, *desct;
	struct xilinx_cdma_desc_hw *hw;

	if (chan->err)
		return;

	spin_lock_irqsave(&chan->lock, flags);

	if (list_empty(&chan->pending_list))
		goto out_unlock;

	/* If hardware is busy, cannot submit */
	if (!cdma_is_idle(chan)) {
		dev_dbg(chan->dev, "DMA controller still busy %x\n",
			cdma_read(chan, XILINX_CDMA_STATUS_OFFSET));
		goto out_unlock;
	}

	/* Enable interrupts */
	cdma_write(chan, XILINX_CDMA_CONTROL_OFFSET,
		   cdma_read(chan, XILINX_CDMA_CONTROL_OFFSET) |
		   XILINX_CDMA_XR_IRQ_ALL_MASK);

	desch = list_first_entry(&chan->pending_list,
				 struct xilinx_cdma_desc_sw, node);

	if (chan->has_sg) {

		/* If hybrid mode, append pending list to active list */
		desct = container_of(chan->pending_list.prev,
				     struct xilinx_cdma_desc_sw, node);

		list_splice_tail_init(&chan->pending_list, &chan->active_list);

		/*
		 * If hardware is idle, then all descriptors on the active list
		 * are done, start new transfers
		 */
		cdma_sg_toggle(chan);

		cdma_write(chan, XILINX_CDMA_CDESC_OFFSET,
			   desch->async_tx.phys);

		/* Update tail ptr register and start the transfer */
		cdma_write(chan, XILINX_CDMA_TDESC_OFFSET,
			   desch->async_tx.phys);
		goto out_unlock;
	}

	/* In simple mode */
	list_del(&desch->node);
	list_add_tail(&desch->node, &chan->active_list);

	hw = &desch->hw;

	cdma_write(chan, XILINX_CDMA_SRCADDR_OFFSET, hw->src_addr);
	cdma_write(chan, XILINX_CDMA_DSTADDR_OFFSET, hw->dest_addr);

	/* Start the transfer */
	cdma_write(chan, XILINX_CDMA_BTT_OFFSET,
		   hw->control & XILINX_CDMA_MAX_TRANS_LEN);

out_unlock:
	spin_unlock_irqrestore(&chan->lock, flags);
}

/*
 * If sg mode, link the pending list to running list; if simple mode, get the
 * head of the pending list and submit it to hw
 */
static void xilinx_cdma_issue_pending(struct dma_chan *dchan)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(dchan);

	xilinx_cdma_start_transfer(chan);
}

/**
 * xilinx_cdma_update_completed_cookie - Update the completed cookie.
 * @chan : xilinx DMA channel
 *
 * CONTEXT: hardirq
 */
static void xilinx_cdma_update_completed_cookie(struct xilinx_cdma_chan *chan)
{
	struct xilinx_cdma_desc_sw *desc = NULL;
	struct xilinx_cdma_desc_hw *hw = NULL;
	unsigned long flags;
	dma_cookie_t cookie = -EBUSY;
	int done = 0;

	spin_lock_irqsave(&chan->lock, flags);

	if (list_empty(&chan->active_list)) {
		dev_dbg(chan->dev, "no running descriptors\n");
		goto out_unlock;
	}

	/* Get the last completed descriptor, update the cookie to that */
	list_for_each_entry(desc, &chan->active_list, node) {
		if (chan->has_sg) {
			hw = &desc->hw;

			/* If a BD has no status bits set, hw has it */
			if (!(hw->status & XILINX_CDMA_BD_STS_ALL_MASK)) {
				break;
			} else {
				done = 1;
				cookie = desc->async_tx.cookie;
			}
		} else {
			/* In non-SG mode, all active entries are done */
			done = 1;
			cookie = desc->async_tx.cookie;
		}
	}

	if (done)
		chan->completed_cookie = cookie;

out_unlock:
	spin_unlock_irqrestore(&chan->lock, flags);
}

/* Reset hardware */
static int cdma_reset(struct xilinx_cdma_chan *chan)
{
	int loop = XILINX_CDMA_RESET_LOOP;
	u32 tmp;

	cdma_write(chan, XILINX_CDMA_CONTROL_OFFSET,
		   cdma_read(chan, XILINX_CDMA_CONTROL_OFFSET) |
		   XILINX_CDMA_CR_RESET_MASK);

	tmp = cdma_read(chan, XILINX_CDMA_CONTROL_OFFSET) &
	      XILINX_CDMA_CR_RESET_MASK;

	/* Wait for the hardware to finish reset */
	while (loop && tmp) {
		tmp = cdma_read(chan, XILINX_CDMA_CONTROL_OFFSET) &
		      XILINX_CDMA_CR_RESET_MASK;
		loop -= 1;
	}

	if (!loop) {
		dev_err(chan->dev, "reset timeout, cr %x, sr %x\n",
			cdma_read(chan, XILINX_CDMA_CONTROL_OFFSET),
			cdma_read(chan, XILINX_CDMA_STATUS_OFFSET));
		return -EBUSY;
	}

	/* For Axi CDMA, always do sg transfers if sg mode is built in */
	if (chan->has_sg)
		cdma_write(chan, XILINX_CDMA_CONTROL_OFFSET,
			   tmp | XILINX_CDMA_CR_SGMODE_MASK);

	return 0;
}


static irqreturn_t cdma_intr_handler(int irq, void *data)
{
	struct xilinx_cdma_chan *chan = data;
	int update_cookie = 0;
	int to_transfer = 0;
	u32 stat, reg;

	reg = cdma_read(chan, XILINX_CDMA_CONTROL_OFFSET);

	/* Disable intr */
	cdma_write(chan, XILINX_CDMA_CONTROL_OFFSET,
		   reg & ~XILINX_CDMA_XR_IRQ_ALL_MASK);

	stat = cdma_read(chan, XILINX_CDMA_STATUS_OFFSET);
	if (!(stat & XILINX_CDMA_XR_IRQ_ALL_MASK))
		return IRQ_NONE;

	/* Ack the interrupts */
	cdma_write(chan, XILINX_CDMA_STATUS_OFFSET,
		   XILINX_CDMA_XR_IRQ_ALL_MASK);

	/* Check for only the interrupts which are enabled */
	stat &= (reg & XILINX_CDMA_XR_IRQ_ALL_MASK);

	if (stat & XILINX_CDMA_XR_IRQ_ERROR_MASK) {
		dev_err(chan->dev,
			"Channel %x has errors %x, cdr %x tdr %x\n",
			(u32)chan,
			(u32)cdma_read(chan, XILINX_CDMA_STATUS_OFFSET),
			(u32)cdma_read(chan, XILINX_CDMA_CDESC_OFFSET),
			(u32)cdma_read(chan, XILINX_CDMA_TDESC_OFFSET));
		chan->err = 1;
	}

	/*
	 * Device takes too long to do the transfer when user requires
	 * responsiveness
	 */
	if (stat & XILINX_CDMA_XR_IRQ_DELAY_MASK)
		dev_dbg(chan->dev, "Inter-packet latency too long\n");

	if (stat & XILINX_CDMA_XR_IRQ_IOC_MASK) {
		update_cookie = 1;
		to_transfer = 1;
	}

	if (update_cookie)
		xilinx_cdma_update_completed_cookie(chan);

	if (to_transfer)
		chan->start_transfer(chan);

	tasklet_schedule(&chan->tasklet);
	return IRQ_HANDLED;
}

static void cdma_do_tasklet(unsigned long data)
{
	struct xilinx_cdma_chan *chan = (struct xilinx_cdma_chan *)data;

	xilinx_cdma_chan_desc_cleanup(chan);
}

/* Append the descriptor list to the pending list */
static void append_desc_queue(struct xilinx_cdma_chan *chan,
			      struct xilinx_cdma_desc_sw *desc)
{
	struct xilinx_cdma_desc_sw *tail =
		container_of(chan->pending_list.prev,
			     struct xilinx_cdma_desc_sw, node);
	struct xilinx_cdma_desc_hw *hw;

	if (list_empty(&chan->pending_list))
		goto out_splice;

	/*
	 * Add the hardware descriptor to the chain of hardware descriptors
	 * that already exists in memory.
	 */
	hw = &(tail->hw);
	hw->next_desc = (u32)desc->async_tx.phys;

	/*
	 * Add the software descriptor and all children to the list
	 * of pending transactions
	 */
out_splice:
	list_splice_tail_init(&desc->tx_list, &chan->pending_list);
}

/*
 * Assign cookie to each descriptor, and append the descriptors to the pending
 * list
 */
static dma_cookie_t xilinx_cdma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct xilinx_cdma_chan *chan = to_xilinx_chan(tx->chan);
	struct xilinx_cdma_desc_sw *desc =
		container_of(tx, struct xilinx_cdma_desc_sw, async_tx);
	struct xilinx_cdma_desc_sw *child;
	unsigned long flags;
	dma_cookie_t cookie = -EBUSY;

	if (chan->err) {
		/*
		 * If reset fails, need to hard reset the system.
		 * Channel is no longer functional
		 */
		if (!cdma_reset(chan))
			chan->err = 0;
		else
			return cookie;
	}

	spin_lock_irqsave(&chan->lock, flags);

	/*
	 * assign cookies to all of the software descriptors
	 * that make up this transaction
	 */
	cookie = chan->cookie;
	list_for_each_entry(child, &desc->tx_list, node) {
		cookie++;
		if (cookie < 0)
			cookie = DMA_MIN_COOKIE;

		child->async_tx.cookie = cookie;
	}

	chan->cookie = cookie;

	/* put this transaction onto the tail of the pending queue */
	append_desc_queue(chan, desc);

	spin_unlock_irqrestore(&chan->lock, flags);

	return cookie;
}

static struct xilinx_cdma_desc_sw *xilinx_cdma_alloc_descriptor(
	struct xilinx_cdma_chan *chan)
{
	struct xilinx_cdma_desc_sw *desc;
	dma_addr_t pdesc;

	desc = dma_pool_alloc(chan->desc_pool, GFP_ATOMIC, &pdesc);
	if (!desc) {
		dev_dbg(chan->dev, "out of memory for desc\n");
		return NULL;
	}

	memset(desc, 0, sizeof(*desc));
	INIT_LIST_HEAD(&desc->tx_list);
	dma_async_tx_descriptor_init(&desc->async_tx, &chan->common);
	desc->async_tx.tx_submit = xilinx_cdma_tx_submit;
	desc->async_tx.phys = pdesc;

	return desc;
}

/**
 * xilinx_cdma_prep_memcpy - prepare descriptors for a memcpy transaction
 * @dchan: DMA channel
 * @dma_dst: destination address
 * @dma_src: source address
 * @len: transfer length
 * @flags: transfer ack flags
 */
static struct dma_async_tx_descriptor *xilinx_cdma_prep_memcpy(
	struct dma_chan *dchan, dma_addr_t dma_dst, dma_addr_t dma_src,
	size_t len, unsigned long flags)
{
	struct xilinx_cdma_chan *chan;
	struct xilinx_cdma_desc_sw *first = NULL, *prev = NULL, *new;
	struct xilinx_cdma_desc_hw *hw, *prev_hw;
	size_t copy;
	dma_addr_t src = dma_src;
	dma_addr_t dst = dma_dst;

	if (!dchan)
		return NULL;

	if (!len)
		return NULL;

	chan = to_xilinx_chan(dchan);

	if (chan->err) {

		/*
		 * If reset fails, need to hard reset the system.
		 * Channel is no longer functional
		 */
		if (!cdma_reset(chan))
			chan->err = 0;
		else
			return NULL;
	}

	/*
	 * If build does not have Data Realignment Engine (DRE),
	 * src has to be aligned
	 */
	if (!chan->has_dre) {
		if ((dma_src &
		     (chan->feature & XILINX_CDMA_FTR_DATA_WIDTH_MASK)) ||
		    (dma_dst &
		     (chan->feature & XILINX_CDMA_FTR_DATA_WIDTH_MASK))) {

			dev_err(chan->dev,
				"Src/Dest address not aligned when no DRE\n");

			return NULL;
		}
	}

	do {
		/* Allocate descriptor from DMA pool */
		new = xilinx_cdma_alloc_descriptor(chan);
		if (!new) {
			dev_err(chan->dev,
				"No free memory for link descriptor\n");
			goto fail;
		}

		copy = min_t(size_t, len, chan->max_len);

		/* if lite build, transfer cannot cross page boundary */
		if (chan->is_lite)
			copy = min(copy, (size_t)(PAGE_MASK -
						  (src & PAGE_MASK)));

		if (!copy) {
			dev_err(chan->dev,
				"Got zero transfer length for %x\n",
				(unsigned int)src);
			goto fail;
		}

		hw = &(new->hw);
		hw->control =
			(hw->control & ~XILINX_CDMA_MAX_TRANS_LEN) | copy;
		hw->src_addr = src;
		hw->dest_addr = dst;

		if (!first)
			first = new;
		else {
			prev_hw = &(prev->hw);
			prev_hw->next_desc = new->async_tx.phys;
		}

		new->async_tx.cookie = 0;
		async_tx_ack(&new->async_tx);

		prev = new;
		len -= copy;
		src += copy;
		dst += copy;

		/* Insert the descriptor to the list */
		list_add_tail(&new->node, &first->tx_list);
	} while (len);

	/* Link the last BD with the first BD */
	hw->next_desc = first->async_tx.phys;

	new->async_tx.flags = flags; /* client is in control of this ack */
	new->async_tx.cookie = -EBUSY;

	return &first->async_tx;

fail:
	if (!first)
		return NULL;

	xilinx_cdma_free_desc_list_reverse(chan, &first->tx_list);
	return NULL;
}

/* Run-time device configuration for Axi CDMA */
static int xilinx_cdma_device_control(struct dma_chan *dchan,
				      enum dma_ctrl_cmd cmd, unsigned long arg)
{
	struct xilinx_cdma_chan *chan;
	unsigned long flags;

	if (!dchan)
		return -EINVAL;

	chan = to_xilinx_chan(dchan);

	if (cmd == DMA_TERMINATE_ALL) {
		spin_lock_irqsave(&chan->lock, flags);

		/* Remove and free all of the descriptors in the lists */
		xilinx_cdma_free_desc_list(chan, &chan->pending_list);
		xilinx_cdma_free_desc_list(chan, &chan->active_list);

		spin_unlock_irqrestore(&chan->lock, flags);
		return 0;
	} else if (cmd == DMA_SLAVE_CONFIG) {
		/*
		 * Configure interrupt coalescing and delay counter
		 * Use value XILINX_CDMA_NO_CHANGE to signal no change
		 */
		struct xilinx_cdma_config *cfg =
			(struct xilinx_cdma_config *)arg;
		u32 reg = cdma_read(chan, XILINX_CDMA_CONTROL_OFFSET);

		if (cfg->coalesc <= XILINX_CDMA_COALESCE_MAX) {
			reg &= ~XILINX_CDMA_XR_COALESCE_MASK;
			reg |= cfg->coalesc << XILINX_CDMA_COALESCE_SHIFT;

			chan->config.coalesc = cfg->coalesc;
		}

		if (cfg->delay <= XILINX_CDMA_DELAY_MAX) {
			reg &= ~XILINX_CDMA_XR_DELAY_MASK;
			reg |= cfg->delay << XILINX_CDMA_DELAY_SHIFT;
			chan->config.delay = cfg->delay;
		}

		cdma_write(chan, XILINX_CDMA_CONTROL_OFFSET, reg);

		return 0;
	}

	return -ENXIO;
}

static void xilinx_cdma_free_channels(struct xilinx_cdma_device *xdev)
{

	list_del(&xdev->chan->common.device_node);
	tasklet_kill(&xdev->chan->tasklet);
	irq_dispose_mapping(xdev->chan->irq);
}

/*
 * Probing channels
 *
 * . Get channel features from the device tree entry
 * . Initialize special channel handling routines
 */
static int xilinx_cdma_chan_probe(struct xilinx_cdma_device *xdev,
				  struct device_node *node, u32 feature)
{
	struct xilinx_cdma_chan *chan;
	int err;
	u32 device_id, value, width = 0;

	/* alloc channel */
	chan = devm_kzalloc(xdev->dev, sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;

	chan->feature = feature;
	chan->max_len = XILINX_CDMA_MAX_TRANS_LEN;

	chan->has_dre = of_property_read_bool(node, "xlnx,include-dre");

	err = of_property_read_u32(node, "xlnx,datawidth", &value);
	if (err) {
		dev_err(xdev->dev, "unable to read datawidth property");
		return err;
	} else {
		width = value >> 3; /* convert bits to bytes */

		/* If data width is greater than 8 bytes, DRE is not in hw */
		if (width > 8)
			chan->has_dre = 0;

		chan->feature |= width - 1;
	}

	err = of_property_read_u32(node, "xlnx,device-id", &device_id);
	if (err) {
		dev_err(xdev->dev, "unable to read device id property");
		return err;
	}

	chan->direction = DMA_MEM_TO_MEM;
	chan->start_transfer = xilinx_cdma_start_transfer;

	chan->has_sg = (xdev->feature & XILINX_CDMA_FTR_HAS_SG) >>
		       XILINX_CDMA_FTR_HAS_SG_SHIFT;

	chan->is_lite = of_property_read_bool(node, "xlnx,lite-mode");
	if (chan->is_lite) {
		err = of_property_read_u32(node, "xlnx,max-burst-len", &value);
		if (err) {
			dev_err(xdev->dev, "unable to read max burstlen property");
			return err;
		}
		if (value) {
			if (!width) {
				dev_err(xdev->dev,
					"Lite mode w/o data width property\n");
				return -EPERM;
			}
			chan->max_len = width * value;
		}
	}

	chan->regs = xdev->regs;

	/*
	 * Used by dmatest channel matching in slave transfers
	 * Can change it to be a structure to have more matching information
	 */
	chan->private = (chan->direction & 0xFF) | XILINX_DMA_IP_CDMA |
			(device_id << XILINX_DMA_DEVICE_ID_SHIFT);
	chan->common.private = (void *)&(chan->private);

	if (!chan->has_dre)
		xdev->common.copy_align = fls(width - 1);

	chan->dev = xdev->dev;
	xdev->chan = chan;

	/* Initialize the channel */
	err = cdma_reset(chan);
	if (err) {
		dev_err(xdev->dev, "Reset channel failed\n");
		return err;
	}

	spin_lock_init(&chan->lock);
	INIT_LIST_HEAD(&chan->pending_list);
	INIT_LIST_HEAD(&chan->active_list);

	chan->common.device = &xdev->common;

	/* Find the IRQ line, if it exists in the device tree */
	chan->irq = irq_of_parse_and_map(node, 0);
	err = devm_request_irq(xdev->dev, chan->irq, cdma_intr_handler,
			       IRQF_SHARED,
			       "xilinx-cdma-controller", chan);
	if (err) {
		dev_err(xdev->dev, "unable to request IRQ\n");
		return err;
	}

	tasklet_init(&chan->tasklet, cdma_do_tasklet, (unsigned long)chan);

	/* Add the channel to DMA device channel list */
	list_add_tail(&chan->common.device_node, &xdev->common.channels);

	return 0;
}

static int xilinx_cdma_probe(struct platform_device *pdev)
{
	struct xilinx_cdma_device *xdev;
	struct device_node *child, *node;
	struct resource *res;
	int ret;
	u32 value;

	xdev = devm_kzalloc(&pdev->dev, sizeof(*xdev), GFP_KERNEL);
	if (!xdev)
		return -ENOMEM;

	xdev->dev = &(pdev->dev);
	INIT_LIST_HEAD(&xdev->common.channels);

	node = pdev->dev.of_node;

	/* iomap registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	xdev->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(xdev->regs))
		return PTR_ERR(xdev->regs);

	/* Check if SG is enabled */
	value = of_property_read_bool(node, "xlnx,include-sg");
	if (value)
		xdev->feature |= XILINX_CDMA_FTR_HAS_SG;

	/* Axi CDMA only does memcpy */
	dma_cap_set(DMA_MEMCPY, xdev->common.cap_mask);
	xdev->common.device_prep_dma_memcpy = xilinx_cdma_prep_memcpy;

	xdev->common.device_control = xilinx_cdma_device_control;
	xdev->common.device_issue_pending = xilinx_cdma_issue_pending;
	xdev->common.device_alloc_chan_resources =
		xilinx_cdma_alloc_chan_resources;
	xdev->common.device_free_chan_resources =
		xilinx_cdma_free_chan_resources;
	xdev->common.device_tx_status = xilinx_tx_status;
	xdev->common.dev = &pdev->dev;

	platform_set_drvdata(pdev, xdev);

	for_each_child_of_node(node, child) {
		ret = xilinx_cdma_chan_probe(xdev, child, xdev->feature);
		if (ret) {
			dev_err(&pdev->dev, "Probing channels failed\n");
			goto free_chan_resources;
		}
	}

	ret = dma_async_device_register(&xdev->common);
	if (ret) {
		dev_err(&pdev->dev, "CDMA device registration failed\n");
		goto free_chan_resources;
	}

	dev_info(&pdev->dev, "Probing xilinx axi cdma engine...Successful\n");

	return 0;

free_chan_resources:
	xilinx_cdma_free_channels(xdev);

	return ret;
}

static int xilinx_cdma_remove(struct platform_device *pdev)
{
	struct xilinx_cdma_device *xdev;

	xdev = platform_get_drvdata(pdev);
	dma_async_device_unregister(&xdev->common);

	xilinx_cdma_free_channels(xdev);

	return 0;
}

static const struct of_device_id xilinx_cdma_of_match[] = {
	{ .compatible = "xlnx,axi-cdma", },
	{}
};
MODULE_DEVICE_TABLE(of, xilinx_cdma_of_match);

static struct platform_driver xilinx_cdma_driver = {
	.driver = {
		.name = "xilinx-cdma",
		.owner = THIS_MODULE,
		.of_match_table = xilinx_cdma_of_match,
	},
	.probe = xilinx_cdma_probe,
	.remove = xilinx_cdma_remove,
};

module_platform_driver(xilinx_cdma_driver);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx CDMA driver");
MODULE_LICENSE("GPL v2");
