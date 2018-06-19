/*
 * timer-atmel-pit64.c - 64 bits Periodic Interval Timer (PIT)
 *
 * Copyright (C) 2017 Microchip Technology Inc.
 * Copyright (C) 2017 Claudiu Beznea (claudiu.beznea@microchip.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>
#include <linux/slab.h>

#define AT91_PIT64_CR		0x00	/* Control Register */
#define AT91_PIT64_CR_START	BIT(0)
#define AT91_PIT64_CR_SWRST	BIT(8)

#define AT91_PIT64_MR		0x04	/* Mode Register */
#define AT91_PIT64_MR_CONT	BIT(0)
#define AT91_PIT64_MR_SGCLK	BIT(3)
#define AT91_PIT64_MR_SMOD	BIT(4)
#define AT91_PIT64_MR_PRES	GENMASK(11, 8)

#define AT91_PIT64_LSB_PR	0x08	/* LSB Period Register */

#define AT91_PIT64_MSB_PR	0x0C	/* MSB Period Register */

#define AT91_PIT64_IER		0x10	/* Interrupt Enable Register */
#define AT91_PIT64_IER_PERIOD	BIT(0)
#define AT91_PIT64_IER_OVRE	BIT(1)
#define AT91_PIT64_IER_SECE	BIT(4)

#define AT91_PIT64_IDR		0x14	/* Interrupt Disable Register */
#define AT91_PIT64_IDR_PERIOD	BIT(0)
#define AT91_PIT64_IDR_OVRE	BIT(1)
#define AT91_PIT64_IDR_SECE	BIT(4)

#define AT91_PIT64_IMR		0x18	/* Interrupt Mask Register */
#define AT91_PIT64_IMR_PERIOD	BIT(0)
#define AT91_PIT64_IMR_OVRE	BIT(1)
#define AT91_PIT64_IMR_SECE	BIT(4)

#define AT91_PIT64_ISR		0x1C	/* Interrupt Status Register */
#define AT91_PIT64_ISR_PERIOD	BIT(0)
#define AT91_PIT64_ISR_OVRE	BIT(1)
#define AT91_PIT64_ISR_SECE	BIT(4)

#define AT91_PIT64_TLSB_CVR	0x20	/* Timer LSB Current Value Register */

#define AT91_PIT64_TMSB_CVR	0x24	/* Timer MSB Current Value Register */

#define AT91_PIT64_WPMR		0xE4	/* Write Protection Mode Register */
#define AT91_PIT64_WPMR_WPEN	BIT(0)
#define AT91_PIT64_WPMR_WPITEN	BIT(1)
#define AT91_PIT64_WPMR_WPCREN	BIT(2)
#define AT91_PIT64_WPMR_FIRSTE	BIT(4)

#define AT91_PIT64_WPSR		0xE8	/* Write Protection Status Register */
#define AT91_PIT64_WPSR_WPSV	BIT(0)
#define AT91_PIT64_WPSR_CGD	BIT(1)
#define AT91_PIT64_WPSR_SEQE	BIT(2)
#define AT91_PIT64_WPSR_SWE	BIT(3)
#define AT91_PIT64_WPSR_WPVSRC	BIT(8)
#define AT91_PIT64_WPSR_SWETYP	BIT(24)
#define AT91_PIT64_WPSR_ECLASS	BIT(31)

#define AT91_PRES_MAX		0x10
#define AT91_PIT64_CS_RATE	2500000UL	/* 2.5 MHz */
#define AT91_PIT64_CE_RATE	2500000UL	/* 2.5 MHz */
#define AT91_PIT64_LSBMASK	GENMASK_ULL(31, 0)
#define AT91_PIT64_PRESCALER(p)	(AT91_PIT64_MR_PRES & ((p) << 8))

#define pit64_name		"pit64"

struct pit64_common_data {
	void __iomem *base;
	struct clk *clk;
	u64 cycles;
	u8 pres;
};

struct pit64_clksrc_data {
	struct clocksource *clksrc;
	struct pit64_common_data cd;
};

struct pit64_clkevt_data {
	struct clock_event_device *clkevt;
	struct pit64_common_data cd;
};

struct pit64_data {
	struct pit64_clksrc_data *csd;
	struct pit64_clkevt_data *ced;
};

static struct pit64_data data;

static inline u32 pit64_read(void __iomem *base, u32 offset)
{
	return readl_relaxed(base + offset);
}

static inline void pit64_write(void __iomem *base, u32 offset, u32 val)
{
	writel_relaxed(val, base + offset);
}

static inline u64 pit64_get_period(void __iomem *base)
{
	u32 lsb, msb;

	/* LSB must be read first to guarantee an atomic read of the 64 bit
	 * timer.
	 */
	lsb = pit64_read(base, AT91_PIT64_TLSB_CVR);
	msb = pit64_read(base, AT91_PIT64_TMSB_CVR);

	return (((u64)msb << 32) | lsb);
}

static inline void pit64_set_period(void __iomem *base, u64 cycles)
{
	u32 lsb, msb;

	lsb = cycles & AT91_PIT64_LSBMASK;
	msb = cycles >> 32;

	/* LSB must be write last to guarantee an atomic update of the timer
	 * even when SMOD=1.
	 */
	pit64_write(base, AT91_PIT64_MSB_PR, msb);
	pit64_write(base, AT91_PIT64_LSB_PR, lsb);
}

static inline void pit64_reset(struct pit64_common_data *data, u32 mode,
			       bool irq_ena)
{
	mode |= AT91_PIT64_PRESCALER(data->pres - 1);

	pit64_write(data->base, AT91_PIT64_CR, AT91_PIT64_CR_SWRST);
	pit64_write(data->base, AT91_PIT64_MR, mode);
	pit64_set_period(data->base, data->cycles);
	if (irq_ena)
		pit64_write(data->base, AT91_PIT64_IER, AT91_PIT64_IER_PERIOD);
	pit64_write(data->base, AT91_PIT64_CR, AT91_PIT64_CR_START);
}

static u64 pit64_read_clk(struct clocksource *cs)
{
	return pit64_get_period(data.csd->cd.base);
}

static u64 sched_read_clk(void)
{
	return pit64_get_period(data.csd->cd.base);
}

static struct clocksource clksrc = {
	.name = pit64_name,
	.mask = CLOCKSOURCE_MASK(64),
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
	.rating = 250,
	.read = pit64_read_clk,
};

static int pit64_clkevt_shutdown(struct clock_event_device *cedev)
{
	pit64_write(data.ced->cd.base, AT91_PIT64_CR, AT91_PIT64_CR_SWRST);

	return 0;
}

static int pit64_clkevt_set_periodic(struct clock_event_device *cedev)
{
	pit64_reset(&data.ced->cd, AT91_PIT64_MR_CONT, true);

	return 0;
}

static int pit64_clkevt_set_oneshot(struct clock_event_device *cedev)
{
	pit64_reset(&data.ced->cd, AT91_PIT64_MR_SMOD, true);

	return 0;
}

static int pit64_clkevt_set_next_event(unsigned long evt,
				       struct clock_event_device *cedev)
{
	pit64_set_period(data.ced->cd.base, evt);
	pit64_write(data.ced->cd.base, AT91_PIT64_CR, AT91_PIT64_CR_START);

	return 0;
}

static void pit64_clkevt_suspend(struct clock_event_device *cedev)
{
	/* TODO: test it on suspend/resume. */
	pit64_write(data.ced->cd.base, AT91_PIT64_CR, AT91_PIT64_CR_SWRST);
	clk_disable_unprepare(data.ced->cd.clk);
}

static void pit64_clkevt_resume(struct clock_event_device *cedev)
{
	u32 mode = AT91_PIT64_MR_SMOD;

	/* TODO: test it on suspend/resume. */
	if (clk_prepare_enable(data.ced->cd.clk)) {
		pr_err("clkevt: Failed to enable clk on resume!\n");
		return;
	}

	if (clockevent_state_periodic(data.ced->clkevt))
		mode = AT91_PIT64_MR_CONT;

	pit64_reset(&data.ced->cd, mode, true);
}

static struct clock_event_device clkevt = {
	.name = pit64_name,
	.features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC,
	.rating = 250,
	.set_state_shutdown = pit64_clkevt_shutdown,
	.set_state_periodic = pit64_clkevt_set_periodic,
	.set_state_oneshot = pit64_clkevt_set_oneshot,
	.set_next_event = pit64_clkevt_set_next_event,
	.suspend = pit64_clkevt_resume,
	.resume = pit64_clkevt_suspend,
};

static irqreturn_t pit64_interrupt(int irq, void *dev_id)
{
	struct pit64_clkevt_data *irq_data = dev_id;

	if (data.ced != irq_data)
		return IRQ_NONE;

	if (pit64_read(irq_data->cd.base, AT91_PIT64_ISR) &
	    AT91_PIT64_ISR_PERIOD) {
		irq_data->clkevt->event_handler(irq_data->clkevt);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static u16 __init pit64_pres_compute(unsigned long clk_rate,
				     unsigned long long max_rate)
{
	u32 tmp;
	u16 pres;

	for (pres = 2; pres < AT91_PRES_MAX; pres++) {
		tmp = clk_rate / pres;
		if (tmp <= max_rate)
			break;
	}

	/*
	 * Use the last prescaller in case we don't locate one for max_rate Hz.
	 */
	if (pres == AT91_PRES_MAX + 1)
		pres = AT91_PRES_MAX;

	return pres;
}

static int __init pit64_dt_init_clksrc(void __iomem *base, struct clk *clk)
{
	struct pit64_clksrc_data *csd;
	unsigned long clk_rate;
	int ret;

	csd = kzalloc(sizeof(*csd), GFP_KERNEL);
	if (!csd)
		return -ENOMEM;

	csd->cd.base = base;
	csd->cd.clk = clk;

	ret = clk_prepare_enable(csd->cd.clk);
	if (ret) {
		pr_err("clksrc: Failed to enable PIT64 clock!\n");
		goto free;
	}

	clk_rate = clk_get_rate(csd->cd.clk);
	csd->cd.pres = pit64_pres_compute(clk_rate, AT91_PIT64_CS_RATE);
	clk_rate = clk_rate / csd->cd.pres;
	csd->cd.cycles = ULLONG_MAX;

	pit64_reset(&csd->cd, AT91_PIT64_MR_CONT, false);

	data.csd = csd;

	csd->clksrc = &clksrc;

	ret = clocksource_register_hz(csd->clksrc, clk_rate);
	if (ret) {
		pr_err("clksrc: Failed to register PIT64 clocksource!\n");
		goto clk_unprepare;
	}

	sched_clock_register(sched_read_clk, 64, clk_rate);

	return 0;

clk_unprepare:
	clk_disable_unprepare(csd->cd.clk);
free:
	kfree(csd);
	data.csd = NULL;

	return ret;
}

static int __init pit64_dt_init_clkevt(void __iomem *base, struct clk *clk,
				       u32 irq)
{
	struct pit64_clkevt_data *ced;
	unsigned long clk_rate;
	int ret;

	ced = kzalloc(sizeof(*ced), GFP_KERNEL);
	if (!ced)
		return -ENOMEM;

	ced->cd.base = base;
	ced->cd.clk = clk;

	ret = clk_prepare_enable(ced->cd.clk);
	if (ret) {
		pr_err("clkevt: Failed to enable PIT64 clock!\n");
		goto free;
	}

	clk_rate = clk_get_rate(ced->cd.clk);
	ced->cd.pres = pit64_pres_compute(clk_rate, AT91_PIT64_CE_RATE);
	clk_rate = clk_rate / ced->cd.pres;
	ced->cd.cycles = DIV_ROUND_CLOSEST(clk_rate, HZ);

	ret = request_irq(irq, pit64_interrupt, IRQF_TIMER, "pit64_tick", ced);
	if (ret) {
		pr_err("clkevt: Failed to setup IRQ\n");
		goto clk_unprepare;
	}

	data.ced = ced;

	/* Set up and register clockevents. */
	ced->clkevt = &clkevt;
	ced->clkevt->cpumask = cpumask_of(0);
	ced->clkevt->irq = irq;
	clockevents_config_and_register(ced->clkevt, clk_rate, 1, ULONG_MAX);

	return 0;

clk_unprepare:
	clk_disable_unprepare(ced->cd.clk);
free:
	kfree(ced);
	data.ced = NULL;

	return ret;
}

static int __init pit64_dt_init(struct device_node *node)
{
	void __iomem *base;
	struct clk *clk;
	u32 irq;
	int ret = -EINVAL;

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%s: Could not map PIT64 address!\n", node->name);
		return -ENXIO;
	}

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk)) {
		pr_err("%s: Failed to get PIT64 clock!\n", node->name);
		ret = PTR_ERR(clk);
		goto unmap;
	}

	irq = irq_of_parse_and_map(node, 0);
	if (!irq) {
		pr_err("%s: Failed to get PIT64 clockevent IRQ!\n",
		       node->name);
		ret = -EINVAL;
		goto clock_put;
	}
	ret = pit64_dt_init_clkevt(base, clk, irq);
	if (ret)
		goto irq_unmap;

/* commenting clock soruce initalization for now.
	ret = pit64_dt_init_clksrc(base, clk);
	if (ret)
		goto clock_put;
*/
	return 0;

irq_unmap:
	irq_dispose_mapping(irq);
clock_put:
	clk_put(clk);
unmap:
	iounmap(base);

	return ret;
}

CLOCKSOURCE_OF_DECLARE(at91_pit64, "atmel,pit64", pit64_dt_init);
