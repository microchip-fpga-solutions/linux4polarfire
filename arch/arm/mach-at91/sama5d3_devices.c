#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <linux/dma-mapping.h>
#include <mach/gpio.h>
#include <linux/platform_device.h>

#include <mach/board.h>
#include <mach/sama5d3.h>
#include <mach/at_hdmac.h>
#include <mach/atmel-mci.h>

#include "generic.h"


/* --------------------------------------------------------------------
 *  HDMAC - AHB DMA Controller
 * -------------------------------------------------------------------- */

#if defined(CONFIG_AT_HDMAC) || defined(CONFIG_AT_HDMAC_MODULE)
static u64 hdmac0_dmamask = DMA_BIT_MASK(32);

static struct at_dma_platform_data atdma0_pdata = {
	.nr_channels	= 8,
	.mem_if		= 0,
	.per_if		= 2,
};

static struct resource hdmac0_resources[] = {
	[0] = {
		.start	= AT91_BASE_SYS + AT91_DMA0,
		.end	= AT91_BASE_SYS + AT91_DMA0 + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= SAMA5D3_ID_DMA0,
		.end	= SAMA5D3_ID_DMA0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at_hdmac0_device = {
	.name		= "at_hdmac",
	.id		= 0,
	.dev		= {
				.dma_mask		= &hdmac0_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &atdma0_pdata,
	},
	.resource	= hdmac0_resources,
	.num_resources	= ARRAY_SIZE(hdmac0_resources),
};

void __init at91_add_device_hdmac(void)
{
	dma_cap_set(DMA_MEMCPY, atdma0_pdata.cap_mask);
	dma_cap_set(DMA_SLAVE, atdma0_pdata.cap_mask);
	platform_device_register(&at_hdmac0_device);
}
#else
void __init at91_add_device_hdmac(void) {}
#endif


/* --------------------------------------------------------------------
 *  USB Host (OHCI)
 * -------------------------------------------------------------------- */

#if defined(CONFIG_USB_OHCI_HCD) || defined(CONFIG_USB_OHCI_HCD_MODULE)
static u64 ohci_dmamask = DMA_BIT_MASK(32);
static struct at91_usbh_data usbh_ohci_data;

static struct resource usbh_ohci_resources[] = {
	[0] = {
		.start	= SAMA5D3_OHCI_BASE,
		.end	= SAMA5D3_OHCI_BASE + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= SAMA5D3_ID_UHPHS,
		.end	= SAMA5D3_ID_UHPHS,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91_usbh_ohci_device = {
	.name		= "at91_ohci",
	.id		= -1,
	.dev		= {
				.dma_mask		= &ohci_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &usbh_ohci_data,
	},
	.resource	= usbh_ohci_resources,
	.num_resources	= ARRAY_SIZE(usbh_ohci_resources),
};

void __init at91_add_device_usbh_ohci(struct at91_usbh_data *data)
{
	int i;

	if (!data)
		return;

	/* Enable VBus control for UHP ports */
	for (i = 0; i < data->ports; i++) {
		if (data->vbus_pin[i])
			at91_set_gpio_output(data->vbus_pin[i], 0);
	}

	/* Enable overcurrent notification */
	for (i = 0; i < data->ports; i++) {
		if (data->overcurrent_pin[i])
			at91_set_gpio_input(data->overcurrent_pin[i], 1);
	}

	usbh_ohci_data = *data;
	platform_device_register(&at91_usbh_ohci_device);
}
#else
void __init at91_add_device_usbh_ohci(struct at91_usbh_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  USB Host HS (EHCI)
 *  Needs an OHCI host for low and full speed management
 * -------------------------------------------------------------------- */

#if defined(CONFIG_USB_EHCI_HCD) || defined(CONFIG_USB_EHCI_HCD_MODULE)
static u64 ehci_dmamask = DMA_BIT_MASK(32);
static struct at91_usbh_data usbh_ehci_data;

static struct resource usbh_ehci_resources[] = {
	[0] = {
		.start	= SAMA5D3_EHCI_BASE,
		.end	= SAMA5D3_EHCI_BASE + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= SAMA5D3_ID_UHPHS,
		.end	= SAMA5D3_ID_UHPHS,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91_usbh_ehci_device = {
	.name		= "atmel-ehci",
	.id		= -1,
	.dev		= {
				.dma_mask		= &ehci_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &usbh_ehci_data,
	},
	.resource	= usbh_ehci_resources,
	.num_resources	= ARRAY_SIZE(usbh_ehci_resources),
};

void __init at91_add_device_usbh_ehci(struct at91_usbh_data *data)
{
	int i;

	if (!data)
		return;

	/* Enable VBus control for UHP ports */
	for (i = 0; i < data->ports; i++) {
		if (data->vbus_pin[i])
			at91_set_gpio_output(data->vbus_pin[i], 0);
	}

	usbh_ehci_data = *data;
	platform_device_register(&at91_usbh_ehci_device);
}
#else
void __init at91_add_device_usbh_ehci(struct at91_usbh_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  USB HS Device (Gadget)
 * -------------------------------------------------------------------- */

#if defined(CONFIG_USB_ATMEL_USBA) || defined(CONFIG_USB_ATMEL_USBA_MODULE)
static struct resource usba_udc_resources[] = {
	[0] = {
		.start	= SAMA5D3_UDPHS_FIFO,
		.end	= SAMA5D3_UDPHS_FIFO + SZ_512K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= SAMA5D3_BASE_UDPHS,
		.end	= SAMA5D3_BASE_UDPHS + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= SAMA5D3_ID_UDPHS,
		.end	= SAMA5D3_ID_UDPHS,
		.flags	= IORESOURCE_IRQ,
	},
};

#define EP(nam, idx, maxpkt, maxbk, dma, isoc)			\
	[idx] = {						\
		.name		= nam,				\
		.index		= idx,				\
		.fifo_size	= maxpkt,			\
		.nr_banks	= maxbk,			\
		.can_dma	= dma,				\
		.can_isoc	= isoc,				\
	}

static struct usba_ep_data usba_udc_ep[] __initdata = {
	EP( "ep0",  0,   64, 1, 0, 0),
	EP( "ep1",  1, 1024, 3, 1, 1),
	EP( "ep2",  2, 1024, 3, 1, 1),
	EP( "ep3",  3, 1024, 2, 1, 1),
	EP( "ep4",  4, 1024, 2, 1, 1),
	EP( "ep5",  5, 1024, 2, 1, 1),
	EP( "ep6",  6, 1024, 2, 1, 1),
	EP( "ep7",  7, 1024, 2, 1, 1),
	EP( "ep8",  8, 1024, 2, 0, 1),
	EP( "ep9",  9, 1024, 2, 0, 1),
	EP("ep10", 10, 1024, 2, 0, 1),
	EP("ep11", 11, 1024, 2, 0, 1),
	EP("ep12", 12, 1024, 2, 0, 1),
	EP("ep13", 13, 1024, 2, 0, 1),
	EP("ep14", 14, 1024, 2, 0, 1),
	EP("ep15", 15, 1024, 2, 0, 1),
};

#undef EP

/*
 * pdata doesn't have room for any endpoints, so we need to
 * append room for the ones we need right after it.
 */
static struct {
	struct usba_platform_data pdata;
	struct usba_ep_data ep[16];
} usba_udc_data;

static struct platform_device at91_usba_udc_device = {
	.name		= "atmel_usba_udc",
	.id		= -1,
	.dev		= {
				.platform_data	= &usba_udc_data.pdata,
	},
	.resource	= usba_udc_resources,
	.num_resources	= ARRAY_SIZE(usba_udc_resources),
};

void __init at91_add_device_usba(struct usba_platform_data *data)
{
	usba_udc_data.pdata.vbus_pin = -EINVAL;
	usba_udc_data.pdata.num_ep = ARRAY_SIZE(usba_udc_ep);
	memcpy(usba_udc_data.ep, usba_udc_ep, sizeof(usba_udc_ep));

	if (data && data->vbus_pin > 0) {
		at91_set_gpio_input(data->vbus_pin, 0);
		at91_set_deglitch(data->vbus_pin, 1);
		usba_udc_data.pdata.vbus_pin = data->vbus_pin;
	}

	/* Pullup pin is handled internally by USB device peripheral */

	platform_device_register(&at91_usba_udc_device);
}
#else
void __init at91_add_device_usba(struct usba_platform_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  Ethernet Gigabit
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MACB) || defined(CONFIG_MACB_MODULE)
static u64 eth_giga_dmamask = DMA_BIT_MASK(32);
static struct macb_platform_data eth_giga_data;

static struct resource eth_giga_resources[] = {
	[0] = {
		.start	= SAMA5D3_BASE_GMAC,
		.end	= SAMA5D3_BASE_GMAC + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= SAMA5D3_ID_GMAC,
		.end	= SAMA5D3_ID_GMAC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sama5d3_eth_giga_device = {
	.name		= "macb",
	.id		= -1,
	.dev		= {
				.dma_mask		= &eth_giga_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &eth_giga_data,
	},
	.resource	= eth_giga_resources,
	.num_resources	= ARRAY_SIZE(eth_giga_resources),
};

void __init at91_add_device_eth_giga(struct macb_platform_data *data)
{
	if (!data)
		return;

	if (data->phy_irq_pin) {
		at91_set_gpio_input(data->phy_irq_pin, 0);
		at91_set_deglitch(data->phy_irq_pin, 1);
	}

	/* Pins used for RGMII */
	at91_set_A_periph(AT91_PIN_PB0,  0);	/* GTX0 */
	at91_set_A_periph(AT91_PIN_PB1,  0);	/* GTX1 */
	at91_set_A_periph(AT91_PIN_PB2,  0);	/* GTX2 */
	at91_set_A_periph(AT91_PIN_PB3,  0);	/* GTX3 */
	at91_set_A_periph(AT91_PIN_PB4,  0);	/* GRX0 */
	at91_set_A_periph(AT91_PIN_PB5,  0);	/* GRX1 */
	at91_set_A_periph(AT91_PIN_PB6,  0);	/* GRX2 */
	at91_set_A_periph(AT91_PIN_PB7,  0);	/* GRX3 */
	at91_set_A_periph(AT91_PIN_PB8,  0);	/* GTXCK */
	at91_set_A_periph(AT91_PIN_PB9,  0);	/* GTXEN */
	at91_set_A_periph(AT91_PIN_PB10, 0);	/* GTXER */
	at91_set_A_periph(AT91_PIN_PB11, 0);	/* GRXCK */
	at91_set_A_periph(AT91_PIN_PB12, 0);	/* GRXDV */
	at91_set_A_periph(AT91_PIN_PB13, 0);	/* GRXER */
	at91_set_A_periph(AT91_PIN_PB14, 0);	/* GCRS */
	at91_set_A_periph(AT91_PIN_PB15, 0);	/* GCOL */
	at91_set_A_periph(AT91_PIN_PB16, 0);	/* GMDC */
	at91_set_A_periph(AT91_PIN_PB17, 0);	/* GMDIO */
	at91_set_A_periph(AT91_PIN_PB18, 0);	/* G125CK */
	//at91_set_B_periph(AT91_PIN_PB19, 0);	/* GTX4 */
	//at91_set_B_periph(AT91_PIN_PB20, 0);	/* GTX5 */
	//at91_set_B_periph(AT91_PIN_PB21, 0);	/* GTX6 */
	//at91_set_B_periph(AT91_PIN_PB22, 0);	/* GTX7 */
	//at91_set_B_periph(AT91_PIN_PB23, 0);	/* GRX4 */
	//at91_set_B_periph(AT91_PIN_PB24, 0);	/* GRX5 */
	//at91_set_B_periph(AT91_PIN_PB25, 0);	/* GRX6 */
	//at91_set_B_periph(AT91_PIN_PB26, 0);	/* GRX7 */
	//at91_set_B_periph(AT91_PIN_PB27, 0);	/* G125CKO */

	eth_giga_data = *data;
	platform_device_register(&sama5d3_eth_giga_device);
}
#else
void __init at91_add_device_eth_giga(struct macb_platform_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  MMC / SD
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MMC_ATMELMCI) || defined(CONFIG_MMC_ATMELMCI_MODULE)
static u64 mmc_dmamask = DMA_BIT_MASK(32);
static struct mci_platform_data mmc0_data;

static struct resource mmc0_resources[] = {
	[0] = {
		.start  = SAMA5D3_BASE_HSMCI0,
		.end    = SAMA5D3_BASE_HSMCI0 + SZ_16K - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = SAMA5D3_ID_HSMCI0,
		.end    = SAMA5D3_ID_HSMCI0,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device sama5d3_mmc0_device = {
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
		atslave->ctrla = ATC_SCSIZE_16 | ATC_DCSIZE_16;
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
	
	if (mmc_id == 0) {		/* MCI0 */
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
	}

	mmc0_data = *data;
	platform_device_register(&sama5d3_mmc0_device);
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
		.start  = AT91_BASE_SYS + AT91_DBGU,
		.end    = AT91_BASE_SYS + AT91_DBGU + SZ_512 - 1,
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
};

static u64 dbgu_dmamask = DMA_BIT_MASK(32);

static struct platform_device sama5d3_dbgu_device = {
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
			pdev = &sama5d3_dbgu_device;
			configure_dbgu_pins();
			break;
		default:
			return;
	}
	pdata = pdev->dev.platform_data;
	pdata->num = portnr;              /* update to mapped ID */

	if (portnr < ATMEL_MAX_UART)
		at91_uarts[portnr] = pdev;
}

void __init at91_set_serial_console(unsigned portnr)
{
	if (portnr < ATMEL_MAX_UART) {
		atmel_default_console_device = at91_uarts[portnr];
		sama5d3_set_console_clock(at91_uarts[portnr]->id);
	}
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

/* -------------------------------------------------------------------- */
/*
 * These devices are always present and don't need any board-specific
 * setup.
 */
static int __init at91_add_standard_devices(void)
{
	at91_add_device_hdmac();
	return 0;
}

arch_initcall(at91_add_standard_devices);
