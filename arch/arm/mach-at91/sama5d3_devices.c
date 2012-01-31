#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/i2c-gpio.h>

#include <mach/board.h>
#include <mach/cpu.h>
#include <mach/gpio.h>
#include <mach/sama5d3.h>
#include <mach/sama5d3_matrix.h>
#include <mach/atmel-mci.h>

#include "generic.h"


/* --------------------------------------------------------------------
 *  MMC / SD
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MMC_ATMELMCI) || defined(CONFIG_MMC_ATMELMCI_MODULE)
static u64 mmc_dmamask = DMA_BIT_MASK(32);
static struct mci_platform_data mmc0_data;

static struct resource mmc0_resources[] = {
	[0] = {
		.start  = SAMA5D3_BASE_MMCI,
		.end    = SAMA5D3_BASE_MMCI + SZ_16K - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = SAMA5D3_ID_MMCI,
		.end    = SAMA5D3_ID_MMCI,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device at91miura_mmc0_device = {
	.name           = "atmel_mci",
	.id             = 0,
	.dev            = {
		.dma_mask               = &mmc_dmamask,
		.coherent_dma_mask      = DMA_BIT_MASK(32),
		.platform_data          = &mmc0_data,
	},
	.resource       = mmc0_resources,
	.num_resources  = ARRAY_SIZE(mmc0_resources),
};

/* Consider only one slot : slot 0 */
void __init at91_add_device_mci(short mmc_id, struct mci_platform_data *data)
{
	if (!data)
		return;

	/* Must have at least one usable slot */
	if (!data->slot[0].bus_width)
		return;

#if defined(CONFIG_AT_HDMAC) || defined(CONFIG_AT_HDMAC_MODULE)
	{
		struct at_dma_slave     *atslave;
		struct mci_dma_data     *alt_atslave;

		alt_atslave = kzalloc(sizeof(struct mci_dma_data), GFP_KERNEL);
		atslave = &alt_atslave->sdata;

		/* DMA slave channel configuration */
		atslave->reg_width = AT_DMA_SLAVE_WIDTH_32BIT;
		atslave->cfg = ATC_FIFOCFG_HALFFIFO
			| ATC_SRC_H2SEL_HW | ATC_DST_H2SEL_HW;
		atslave->ctrla = ATC_SCSIZE_8 | ATC_DCSIZE_8;
		if (mmc_id == 0) {	/* MCI0 */
			atslave->cfg |= ATC_SRC_PER(AT_DMA_ID_MCI0)
				| ATC_DST_PER(AT_DMA_ID_MCI0);
			atslave->dma_dev = &at_hdmac0_device.dev;
		} else {		/* MCI1 */
			atslave->cfg |= ATC_SRC_PER(AT_DMA_ID_MCI1)
				| ATC_DST_PER(AT_DMA_ID_MCI1);
			//atslave->dma_dev = &at_hdmac1_device.dev;
		}

		data->dma_slave = alt_atslave;
	}
#endif

	/* input/irq */
	if (data->slot[0].detect_pin) {
		at91_set_gpio_input(data->slot[0].detect_pin, 1);
		at91_set_deglitch(data->slot[0].detect_pin, 1);
	}
	if (data->slot[0].wp_pin)
		at91_set_gpio_input(data->slot[0].wp_pin, 1);

	/* CLK */
	at91_set_A_periph(AT91_PIN_PD9, 0);
	/* CMD */
	at91_set_A_periph(AT91_PIN_PD0, 1);
	/* DAT0, maybe DAT1..DAT3 and maybe DAT4..DAT7 */
	at91_set_A_periph(AT91_PIN_PD1, 1);
	if (data->slot[0].bus_width == 4) {
		at91_set_A_periph(AT91_PIN_PD2, 1);
		at91_set_A_periph(AT91_PIN_PD3, 1);
		at91_set_A_periph(AT91_PIN_PD4, 1);
		if (data->slot[0].bus_width == 8) {
			at91_set_A_periph(AT91_PIN_PD5, 1);
			at91_set_A_periph(AT91_PIN_PD6, 1);
			at91_set_A_periph(AT91_PIN_PD7, 1);
			at91_set_A_periph(AT91_PIN_PD8, 1);
		}
	}

	mmc0_data = *data;
	platform_device_register(&at91miura_mmc0_device);
}
#else
void __init at91_add_device_mci(short mmc_id, struct mci_platform_data *data) {}
#endif

/* --------------------------------------------------------------------
 *  UART
 * -------------------------------------------------------------------- */
#if defined(CONFIG_SERIAL_ATMEL)
static struct resource dbgu_resources[] = {
	[0] = {
		.start  = AT91_VA_BASE_SYS + AT91_DBGU,
		.end    = AT91_VA_BASE_SYS + AT91_DBGU + SZ_512 - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = SAMA5D3_ID_DBGU,
		.end    = SAMA5D3_ID_DBGU,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data dbgu_data = {
	.use_dma_tx     = 0,
	.use_dma_rx     = 0,
	.regs           = (void __iomem *)(AT91_VA_BASE_SYS + AT91_DBGU),
};

static u64 dbgu_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91miura_dbgu_device = {
	.name           = "atmel_usart",
	.id             = 0,
	.dev            = {
		.dma_mask               = &dbgu_dmamask,
		.coherent_dma_mask      = DMA_BIT_MASK(32),
		.platform_data          = &dbgu_data,
	},
	.resource       = dbgu_resources,
	.num_resources  = ARRAY_SIZE(dbgu_resources),
};

static inline void configure_dbgu_pins(void)
{
	at91_set_A_periph(AT91_PIN_PB30, 0);            /* DRXD */
	at91_set_A_periph(AT91_PIN_PB31, 1);            /* DTXD */
}

static struct platform_device *__initdata at91_uarts[ATMEL_MAX_UART];   /* the UARTs to use */
struct platform_device *atmel_default_console_device;   /* the serial console device */

void __init at91_register_uart(unsigned id, unsigned portnr, unsigned pins)
{
	struct platform_device *pdev;
	struct atmel_uart_data *pdata;

	switch (id) {
		case 0:         /* DBGU */
			pdev = &at91miura_dbgu_device;
			configure_dbgu_pins();
			at91_clock_associate("mck", &pdev->dev, "usart");
			break;
		default:
			return;
	}
	pdata = pdev->dev.platform_data;
	pdev->id = portnr;              /* update to mapped ID */

	if (portnr < ATMEL_MAX_UART)
		at91_uarts[portnr] = pdev;
}

void __init at91_set_serial_console(unsigned portnr)
{
	if (portnr < ATMEL_MAX_UART)
		atmel_default_console_device = at91_uarts[portnr];
}

void __init at91_add_device_serial(void)
{
	int i;

	for (i = 0; i < ATMEL_MAX_UART; i++) {
		if (at91_uarts[i])
			platform_device_register(at91_uarts[i]);
	}

	if (!atmel_default_console_device)
		printk(KERN_INFO "AT91: No default serial console defined.\n");
}
#else
void __init at91_register_uart(unsigned id, unsigned portnr, unsigned pins) {}
void __init at91_set_serial_console(unsigned portnr) {}
void __init at91_add_device_serial(void) {}
#endif

static int __init at91_add_standard_devices(void)
{
	printk(KERN_INFO "at91_add_standard_devices\n");
	at91_add_device_hdmac();
	return 0;
}

arch_initcall(at91_add_standard_devices);
