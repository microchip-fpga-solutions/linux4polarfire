#include <linux/module.h>
#include <linux/pm.h>

#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/sama5d3.h>
#include <mach/at91_pmc.h>
#include <mach/at91_rstc.h>
#include <mach/at91_shdwc.h>
#include <mach/cpu.h>

#include "soc.h"
#include "generic.h"
#include "clock.h"

/* --------------------------------------------------------------------
 *  Clocks
 * -------------------------------------------------------------------- */

/*
 * The peripheral clocks.
 */
static struct clk pioA_clk = {
	.name		= "pioA_clk",
	.pmc_mask	= 1 << SAMA5D3_ID_PIOA,
	.type		= CLK_TYPE_PERIPHERAL,
};

static struct clk pioB_clk = {
	.name		= "pioB_clk",
	.pmc_mask	= 1 << SAMA5D3_ID_PIOB,
	.type		= CLK_TYPE_PERIPHERAL,
};

static struct clk pioC_clk = {
	.name		= "pioC_clk",
	.pmc_mask	= 1 << SAMA5D3_ID_PIOC,
	.type		= CLK_TYPE_PERIPHERAL,
};

static struct clk pioD_clk = {
	.name		= "pioD_clk",
	.pmc_mask	= 1 << SAMA5D3_ID_PIOD,
	.type		= CLK_TYPE_PERIPHERAL,
};

static struct clk pioE_clk = {
	.name		= "pioE_clk",
	.pmc_mask	= 1 << SAMA5D3_ID_PIOE,
	.type		= CLK_TYPE_PERIPHERAL,
};

static struct clk mmc0_clk = {
	.name           = "mci0_clk",
	.pmc_mask       = 1 << SAMA5D3_ID_HSMCI0,
	.type           = CLK_TYPE_PERIPHERAL,
};

static struct clk dma0_clk = {
	.name           = "dma_clk",
	.pmc_mask       = 1 << SAMA5D3_ID_DMA0,
	.type           = CLK_TYPE_PERIPHERAL,
};

static struct clk *periph_clocks[] __initdata = {
	&pioA_clk,
	&pioB_clk,
	&pioC_clk,
	&pioD_clk,
	&pioE_clk,
	&mmc0_clk,
	&dma0_clk,
};

static struct clk_lookup periph_clocks_lookups[] = {
	CLKDEV_CON_DEV_ID("mci_clk", "atmel_mci.0", &mmc0_clk),
};

static struct clk_lookup usart_clocks_lookups[] = {
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.0", &mck),
};

static void __init sama5d3_register_clocks(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(periph_clocks); i++)
		clk_register(periph_clocks[i]);
	
	clkdev_add_table(periph_clocks_lookups,
			 ARRAY_SIZE(periph_clocks_lookups));
	clkdev_add_table(usart_clocks_lookups,
			 ARRAY_SIZE(usart_clocks_lookups));
}

static struct clk_lookup console_clock_lookup;

void __init sama5d3_set_console_clock(int id)
{
	if (id >= ARRAY_SIZE(usart_clocks_lookups))
		return;

	console_clock_lookup.con_id = "usart";
	console_clock_lookup.clk = usart_clocks_lookups[id].clk;
	clkdev_add(&console_clock_lookup);
}

/* --------------------------------------------------------------------
 *  GPIO
 * -------------------------------------------------------------------- */

static struct at91_gpio_bank sama5d3_gpio[] = {
	{
		.id	= SAMA5D3_ID_PIOA,
		.offset = AT91_PIOA,
		.clock	= &pioA_clk,
	}, {
		.id	= SAMA5D3_ID_PIOB,
		.offset	= AT91_PIOB,
		.clock	= &pioB_clk,
	}, {
		.id	= SAMA5D3_ID_PIOC,
		.offset	= AT91_PIOC,
		.clock	= &pioC_clk,
	}, {
		.id	= SAMA5D3_ID_PIOD,
		.offset	= AT91_PIOD,
		.clock	= &pioD_clk,
	}, {
		.id	= SAMA5D3_ID_PIOE,
		.offset	= AT91_PIOE,
		.clock	= &pioE_clk,
	}
};

static void sama5d3_reset(void)
{
}

static void sama5d3_poweroff(void)
{
}



/* --------------------------------------------------------------------
 *  SAMA5D3 processor initialization
 * -------------------------------------------------------------------- */

void __init sama5d3_map_io(void)
{
	at91_init_sram(0, SAMA5D3_SRAM_BASE, SAMA5D3_SRAM_SIZE);
}

void __init sama5d3_initialize(unsigned long main_clock)
{
	at91_arch_reset = sama5d3_reset;
	pm_power_off = sama5d3_poweroff;
	at91_extern_irq = (1 << SAMA5D3_ID_IRQ0);

	/* Register GPIO subsystem */
	at91_gpio_init(sama5d3_gpio, 5);
}

/* --------------------------------------------------------------------
 *  Interrupt initialization
 * -------------------------------------------------------------------- */

/*
 * The default interrupt priority levels (0 = lowest, 7 = highest).
 */
static unsigned int sama5d3_default_irq_priority[NR_AIC_IRQS] __initdata = {
	7,      /* Advanced Interrupt Controller (FIQ) */
	7,      /* System Peripherals */
	7,	/* DBGU */
	7,	/* Periodic Interval Timer */
	7,	/* Watchdog Timer */
	7,	/* SMC */
	1,	/* Parallel IO Controller A */
	1,	/* Parallel IO Controller B */
	1,	/* Parallel IO Controller C */
	1,	/* Parallel IO Controller D */
	1,	/* Parallel IO Controller E */
	4,	/* Soft Modem */
	5,	/* USART 0 */
	5,	/* USART 1 */
	5,	/* USART 2 */
	5,	/* USART 3 */
	5,	/* UART 0 */
	5,	/* UART 1 */
	0,	/* Two-Wire Interface 0 */
	0,	/* Two-Wire Interface 1 */
	0,	/* High Speed Multimedia Card Interface 0 */
	0,	/* High Speed Multimedia Card Interface 1 */
	0,	/* High Speed Multimedia Card Interface 2 */
	5,	/* Serial Peripheral Interface 0 */
	5,	/* Serial Peripheral Interface 1 */
	0,	/* Timer Counter 0 */
	0,	/* Timer Counter 1 */
	0,	/* Pulse Width Modulation Controller */
	0,	/* ADC Controller */
	0,	/* DMA Controller 0 */
	0,	/* DMA Controller 1 */
	2,	/* USB Host High Speed Port*/
	2,	/* USB Device High Speed Port */
	3,	/* Gigabit MAC */
	3,	/* Ethernet MAC */
	3,	/* LCD Controller */
	3,	/* Image Sensor Interface */
	4,	/* Synchronous Serial Controller 0 */
	4,	/* Synchronous Serial Controller 1 */
	4,	/* CAN Controller 0 */
	4,	/* CAN Controller 1 */
	0,	/* SHA */
	0,	/* AES */
	0,	/* TDES */
	0,	/* TRNG */
	0,	/* ARM */
	0,	/* Advanced Interrupt Controller (IRQ0) */
	0,	/* FUSE */
	0,	/* MPDDRC */
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0
};

struct at91_init_soc __initdata sama5d3_soc = {
	.map_io = sama5d3_map_io,
	.default_irq_priority = sama5d3_default_irq_priority,
	.register_clocks = sama5d3_register_clocks,
	.init = sama5d3_initialize,
};
