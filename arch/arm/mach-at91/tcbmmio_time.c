/*
 * tcbmmio_time.c - Timer based on TC Block using mmio clocksource
 *
 * Copyright (C) 2013 Atmel, Nicolas Ferre <nicolas.ferre@atmel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * TCB: channel 0: 32 bit free-running counter as a clocksource, running
 *                 @ 5+ MHz.
 *      channel 1: not used.
 *      channel 2: 32 bit clockevent source, periodic or oneshot mode, running
 *                 @ 156250 KHz (20 MHz / 128).
 */
#define DEBUG 12

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/atmel_tc.h>

#include <asm/sched_clock.h>
#include <asm/mach/time.h>

static void __iomem *tcaddr;
static struct clk *tcclk;
static int tcirq;
#define CLK32K_IDX 4

static u32 notrace tcbmmio_sched_read(void)
{
	return readl_relaxed(tcaddr + ATMEL_TC_REG(0, CV));
}

static void __init tcb_setup_single_chan(int mck_divisor_idx)
{
	/* channel 0:  waveform mode, input mclk/8 */
	__raw_writel(mck_divisor_idx			/* likely divide-by-8 */
			| ATMEL_TC_WAVE
			| ATMEL_TC_WAVESEL_UP,		/* free-run */
			tcaddr + ATMEL_TC_REG(0, CMR));
	__raw_writel(0xff, tcaddr + ATMEL_TC_REG(0, IDR));	/* no irqs */
	__raw_writel(ATMEL_TC_CLKEN, tcaddr + ATMEL_TC_REG(0, CCR));

	/* then reset all the timers */
	__raw_writel(ATMEL_TC_SYNC, tcaddr + ATMEL_TC_BCR);
}


static struct of_device_id tcbmmio_timer_ids[] = {
	{ .compatible = "atmel,at91sam9x5-tcbmmio" },
	{ /* sentinel */ }
};

static int __init tcbmmio_setup(void)
{
	struct device_node	*np;

	np = of_find_matching_node(NULL, tcbmmio_timer_ids);
	if (!np)
		goto err;

	tcaddr = of_iomap(np, 0);
	pr_info("AT91: TCBMMIO: tcaddr = 0x%08x\n", (unsigned int)tcaddr);
	if (tcaddr == NULL)
		goto node_err;

	tcirq = irq_of_parse_and_map(np, 0);
	pr_info("AT91: TCBMMIO: tcirq = %d\n", tcirq);
	if (!tcirq)
		goto ioremap_err;

	if (of_machine_is_compatible("ingenico,sama5d4ing"))
		tcclk = clk_get(NULL, "tcb2_clk");
	else
		tcclk = clk_get(NULL, "tcb0_clk");
	if (IS_ERR(tcclk)) {
		pr_crit("AT91: TCBMMIO: Unable to get clk\n");
		goto ioremap_err;
	}

	of_node_put(np);

	return 0;
ioremap_err:
	iounmap(tcaddr);
node_err:
	of_node_put(np);
err:
	return -EINVAL;
}

static int __init tcbmmio_start(unsigned long *hz)
{
	u32 rate, divided_rate = 0;
	int best_divisor_idx = -1;
	int i;

	if (!hz || !tcclk)
		return -EINVAL;

	clk_enable(tcclk);

	/* How fast will we be counting?  Pick something over 5 MHz.  */
	rate = (u32) clk_get_rate(tcclk);
	for (i = 0 ; i < 5 ; i++) {
		unsigned divisor = atmel_tc_divisors[i];
		unsigned tmp;

		if (!divisor) {
			continue;
		}

		tmp = rate / divisor;
		pr_debug("TC: %u / %-3u [%d] --> %u\n", rate, divisor, i, tmp);
		if (best_divisor_idx >= 0) {
			if (tmp < 5 * 1000 * 1000)
				continue;
		}
		divided_rate = tmp;
		best_divisor_idx = i;
	}

	pr_info("AT91: TCBMMIO: rate at %d.%03d MHz\n",
			divided_rate / 1000000,
			(divided_rate % 1000000) / 1000);
	*hz = divided_rate;

	tcb_setup_single_chan(best_divisor_idx);

	return 0;
}

static void tcbmmio_clkevt_mode(enum clock_event_mode m, struct clock_event_device *d)
{
	switch (m) {
	case CLOCK_EVT_MODE_PERIODIC:
		__raw_writel(0xff, tcaddr + ATMEL_TC_REG(2, IDR));
		__raw_writel(ATMEL_TC_CLKDIS, tcaddr + ATMEL_TC_REG(2, CCR));

		/* slow clock, count up to RC, then irq and restart */
		__raw_writel(CLK32K_IDX | ATMEL_TC_WAVE | ATMEL_TC_WAVESEL_UP_AUTO,
				tcaddr + ATMEL_TC_REG(2, CMR));
		__raw_writel((32768 + HZ/2) / HZ, tcaddr + ATMEL_TC_REG(2, RC));

		/* Enable clock and interrupts on RC compare */
		__raw_writel(ATMEL_TC_CPCS, tcaddr + ATMEL_TC_REG(2, IER));

		/* go go gadget! */
		__raw_writel(ATMEL_TC_CLKEN | ATMEL_TC_SWTRG,
				tcaddr + ATMEL_TC_REG(2, CCR));
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		__raw_writel(0xff, tcaddr + ATMEL_TC_REG(2, IDR));
		__raw_writel(ATMEL_TC_CLKDIS, tcaddr + ATMEL_TC_REG(2, CCR));

		/* slow clock, count up to RC, then irq and stop */
		__raw_writel(CLK32K_IDX | ATMEL_TC_CPCSTOP
				| ATMEL_TC_WAVE | ATMEL_TC_WAVESEL_UP_AUTO,
				tcaddr + ATMEL_TC_REG(2, CMR));
		__raw_writel(ATMEL_TC_CPCS, tcaddr + ATMEL_TC_REG(2, IER));

		/* set_next_event() configures and starts the timer */
		break;

	default:
		break;
	}
}
static int tcbmmio_clkevt_next_event(unsigned long delta, struct clock_event_device *d)
{
	__raw_writel(delta, tcaddr + ATMEL_TC_REG(2, RC));

	/* go go gadget! */
	__raw_writel(ATMEL_TC_CLKEN | ATMEL_TC_SWTRG,
		     tcaddr + ATMEL_TC_REG(2, CCR));
	return 0;
}

static struct clock_event_device tcbmmio_clkevt = {
	.name		= "tcbmmio_tick",
	.features	= CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC,
	.shift		= 32,
	.rating		= 100,
	.set_next_event	= tcbmmio_clkevt_next_event,
	.set_mode	= tcbmmio_clkevt_mode,
};

static irqreturn_t ch2_irq(int irq, void *dev_id)
{
	unsigned int sr;

	sr = __raw_readl(tcaddr + ATMEL_TC_REG(2, SR));
	if (sr & ATMEL_TC_CPCS) {
		tcbmmio_clkevt.event_handler(&tcbmmio_clkevt);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static struct irqaction tcbmmio_irqaction = {
	.name		= "tcbmmio_clkevt",
	.flags		= IRQF_TIMER | IRQF_DISABLED,
	.handler	= ch2_irq,
};

void __init tcbmmio_init(void)
{
	int		ret;
	unsigned long	hz = 0;

	ret = tcbmmio_setup();
	if (ret) {
		pr_crit("AT91: TCBMMIO: Unable to setup from DT\n");
		return;
	}

	ret = tcbmmio_start(&hz);
	if (ret || hz == 0) {
		pr_crit("AT91: TCBMMIO: Unable to start timer\n");
		return;
	}

	setup_sched_clock(tcbmmio_sched_read, 32, hz);

	/*
	 * Setup free-running clocksource timer
	 */
	ret = clocksource_mmio_init(tcaddr + ATMEL_TC_REG(0, CV), "tcbmmio",
				    hz, 100, 32,
				    clocksource_mmio_readl_up);
	if (ret)
		pr_crit("AT91: TCBMMIO: Unable to init mmio clocksource\n");
	else
		pr_info("AT91: TCBMMIO: Clocksource initialized\n");


	/*
	 * Setup clockevent timer (interrupt-driven)
	 */
	setup_irq(tcirq, &tcbmmio_irqaction);
	tcbmmio_clkevt.cpumask = cpumask_of(0);
	clockevents_config_and_register(&tcbmmio_clkevt,
					32768, 1, 0xfffffffe);
	return;
}
