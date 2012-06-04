#include <linux/module.h>
#include <linux/pm.h>
#include <linux/dma-mapping.h>

#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/islero.h>
//#include <mach/at91_pmc.h>
//#include <mach/at91_rstc.h>
//#include <mach/at91_shdwc.h>
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
	.pmc_mask	= 1 << ISLERO_ID_PIOA_NS,
	.type		= CLK_TYPE_PERIPHERAL,
};


static struct clk *periph_clocks[] __initdata = {
#if 0
	&pioA_clk,
#endif
};

static struct clk_lookup periph_clocks_lookups[] = {
#if 0
	/* One additional fake clock for macb_hclk */
	CLKDEV_CON_ID("hclk", &macb_clk),
	CLKDEV_CON_ID("hclk", &gmacb_clk),
	/* One additional fake clock for ohci */
	CLKDEV_CON_ID("ohci_clk", &uhphs_clk),
	CLKDEV_CON_DEV_ID("ehci_clk", "atmel-ehci", &uhphs_clk),
	CLKDEV_CON_DEV_ID("hclk", "atmel_usba_udc", &utmi_clk),
	CLKDEV_CON_DEV_ID("pclk", "atmel_usba_udc", &udphs_clk),
	/* fake hclk clock */
	CLKDEV_CON_DEV_ID("hclk", "at91_ohci", &uhphs_clk),
#endif
	CLKDEV_CON_DEV_ID("mci_clk", "atmel_mci.0", &mck),
};

static struct clk_lookup usart_clocks_lookups[] = {
	CLKDEV_CON_DEV_ID("usart", "atmel_usart.0", &mck),
};

static void __init islero_register_clocks(void)
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

void __init islero_set_console_clock(int id)
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

static struct at91_gpio_bank islero_gpio[] = {
	{
		.id	= ISLERO_ID_PIOA_NS,
		.offset = AT91_PIOA_NS,
		.clock	= &pioA_clk,
	}
};

static void islero_reset(void)
{
}

static void islero_poweroff(void)
{
}



/* --------------------------------------------------------------------
 *  ISLERO processor initialization
 * -------------------------------------------------------------------- */

void __init islero_map_io(void)
{
	at91_init_sram(0, ISLERO_SRAM1_BASE, ISLERO_SRAM1_SIZE);
	init_consistent_dma_size(14 * SZ_1M);
}

void __init islero_initialize(void)
{
	at91_arch_reset = islero_reset;
	pm_power_off = islero_poweroff;

	/* Register GPIO subsystem */
	at91_gpio_init(islero_gpio, 1);
}

/* --------------------------------------------------------------------
 *  Interrupt initialization
 * -------------------------------------------------------------------- */

/*
 * The default interrupt priority levels (0 = lowest, 7 = highest).
 */
static unsigned int islero_default_irq_priority[NR_AIC_IRQS] __initdata = {
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
};

struct at91_init_soc __initdata islero_soc = {
	.map_io = islero_map_io,
	.default_irq_priority = islero_default_irq_priority,
	.register_clocks = islero_register_clocks,
	.init = islero_initialize,
};
