/*
 *  On-Chip devices setup code for the AT91SAM9N12 SoC
 *
 *  Copyright (C) 2011 Atmel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/i2c-gpio.h>
#include <linux/atmel-mci.h>
#include <linux/fb.h>

#include <video/atmel_lcdfb.h>

#include <mach/board.h>
#include <mach/gpio.h>
#include <mach/atmel_hlcdc.h>
#include <mach/cpu.h>
#include <mach/at91sam9n12.h>
#include <mach/at91sam9n12_matrix.h>
#include <mach/at91sam9_smc.h>
#include <mach/at_hdmac.h>
#include <mach/atmel-mci.h>

#include "generic.h"

/* --------------------------------------------------------------------
 *  HDMAC - AHB DMA Controller
 * -------------------------------------------------------------------- */

#if defined(CONFIG_AT_HDMAC) || defined(CONFIG_AT_HDMAC_MODULE)
static u64 hdmac_dmamask = DMA_BIT_MASK(32);

static struct at_dma_platform_data atdma_pdata = {
	.nr_channels	= 8,
};

static struct resource hdmac_resources[] = {
	[0] = {
		.start	= AT91_BASE_SYS + AT91_DMA,
		.end	= AT91_BASE_SYS + AT91_DMA + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_DMA,
		.end	= AT91SAM9N12_ID_DMA,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at_hdmac_device = {
	.name		= "at_hdmac",
	.id		= 0,
	.dev		= {
				.dma_mask		= &hdmac_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &atdma_pdata,
	},
	.resource	= hdmac_resources,
	.num_resources	= ARRAY_SIZE(hdmac_resources),
};

void __init at91_add_device_hdmac(void)
{
	dma_cap_set(DMA_MEMCPY, atdma_pdata.cap_mask);
	dma_cap_set(DMA_SLAVE, atdma_pdata.cap_mask);
	dma_cap_set(DMA_CYCLIC, atdma_pdata.cap_mask);
	at91_clock_associate("dma_clk", &at_hdmac_device.dev, "dma_clk");
	platform_device_register(&at_hdmac_device);
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
		.start	= AT91SAM9N12_OHCI_BASE,
		.end	= AT91SAM9N12_OHCI_BASE + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_UHPFS,
		.end	= AT91SAM9N12_ID_UHPFS,
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

	usbh_ohci_data = *data;
	platform_device_register(&at91_usbh_ohci_device);
}
#else
void __init at91_add_device_usbh_ohci(struct at91_usbh_data *data) {}
#endif

/* --------------------------------------------------------------------
 *  USB FS Device (Gadget)
 * -------------------------------------------------------------------- */
#ifdef CONFIG_USB_GADGET_AT91
static struct at91_udc_data udc_data;

static struct resource udc_resources[] = {
	[0] = {
		.start  = AT91SAM9N12_BASE_UDPFS,
		.end    = AT91SAM9N12_BASE_UDPFS + SZ_16K - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = AT91SAM9N12_ID_UDPFS,
		.end    = AT91SAM9N12_ID_UDPFS,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device at91_udc_device = {
	.name       = "at91_udc",
	.id         = -1,
	.dev        = {
		.platform_data      = &udc_data,
	},
	.resource   = udc_resources,
	.num_resources  = ARRAY_SIZE(udc_resources),
};

void __init at91_add_device_udc(struct at91_udc_data *data)
{
	if (data == NULL)
		return;

	if (data->vbus_pin) {
		at91_set_gpio_input(data->vbus_pin, 0); /* pull-up disable */
		at91_set_deglitch(data->vbus_pin, 1);
	}

	/* Pullup pin is handled internally by USB device peripheral */
	udc_data = *data;
	platform_device_register(&at91_udc_device);
}
#else
void __init at91_add_device_udc(struct at91_udc_data *data) {}
#endif

/* --------------------------------------------------------------------
 *  MMC / SD
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MMC_ATMELMCI) || defined(CONFIG_MMC_ATMELMCI_MODULE)
static u64 mmc_dmamask = DMA_BIT_MASK(32);
static struct mci_platform_data mmc_data;

static struct resource mmc_resources[] = {
	[0] = {
		.start	= AT91SAM9N12_BASE_MCI,
		.end	= AT91SAM9N12_BASE_MCI + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_MCI,
		.end	= AT91SAM9N12_ID_MCI,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9n12_mmc_device = {
	.name		= "atmel_mci",
	.id		= 0,
	.dev		= {
				.dma_mask		= &mmc_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &mmc_data,
	},
	.resource	= mmc_resources,
	.num_resources	= ARRAY_SIZE(mmc_resources),
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
	struct at_dma_slave	*atslave;
	struct mci_dma_data	*alt_atslave;

	alt_atslave = kzalloc(sizeof(struct mci_dma_data), GFP_KERNEL);
	atslave = &alt_atslave->sdata;

	/* DMA slave channel configuration */
	atslave->reg_width = AT_DMA_SLAVE_WIDTH_32BIT;
	atslave->cfg = ATC_FIFOCFG_HALFFIFO
			| ATC_SRC_H2SEL_HW | ATC_DST_H2SEL_HW;
	atslave->ctrla = ATC_SCSIZE_16 | ATC_DCSIZE_16;
	atslave->cfg |= ATC_SRC_PER(AT_DMA_ID_MCI) | ATC_DST_PER(AT_DMA_ID_MCI);
	atslave->dma_dev = &at_hdmac_device.dev;

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
	at91_set_A_periph(AT91_PIN_PA17, 0);

	/* CMD */
	at91_set_A_periph(AT91_PIN_PA16, 1);

	/* DAT0, maybe DAT1..DAT3 */
	at91_set_A_periph(AT91_PIN_PA15, 1);
	if (data->slot[0].bus_width == 4) {
		at91_set_A_periph(AT91_PIN_PA18, 1);
		at91_set_A_periph(AT91_PIN_PA19, 1);
		at91_set_A_periph(AT91_PIN_PA20, 1);
	}

	mmc_data = *data;
	at91_clock_associate("mci_clk", &at91sam9n12_mmc_device.dev, "mci_clk");
	platform_device_register(&at91sam9n12_mmc_device);
}
#else
void __init at91_add_device_mci(short mmc_id, struct mci_platform_data *data) {}
#endif


/* --------------------------------------------------------------------
 *  NAND / SmartMedia
 * -------------------------------------------------------------------- */

#if defined(CONFIG_MTD_NAND_ATMEL) || defined(CONFIG_MTD_NAND_ATMEL_MODULE)
static struct atmel_nand_data nand_data;

#define NAND_BASE	AT91_CHIPSELECT_3

static struct resource nand_resources[] = {
	[0] = {
		.start	= NAND_BASE,
		.end	= NAND_BASE + SZ_256M - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91_BASE_SYS + AT91_PMECC,
		.end	= AT91_BASE_SYS + AT91_PMECC + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= AT91_BASE_SYS + AT91_PMERRLOC,
		.end	= AT91_BASE_SYS + AT91_PMERRLOC + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[3] = {
		.start	= AT91SAM9N12_ROM_BASE,
		.end	= AT91SAM9N12_ROM_BASE + AT91SAM9N12_ROM_SIZE,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device at91sam9n12_nand_device = {
	.name		= "atmel_nand",
	.id		= -1,
	.dev		= {
				.platform_data	= &nand_data,
	},
	.resource	= nand_resources,
	.num_resources	= ARRAY_SIZE(nand_resources),
};

void __init at91_add_device_nand(struct atmel_nand_data *data)
{
	unsigned long csa;

	if (!data)
		return;

	csa = at91_sys_read(AT91_MATRIX_EBICSA);

	/* Assign CS3 to NAND/SmartMedia Interface */
	csa |= AT91_MATRIX_EBI_CS3A_SMC_NANDFLASH;
	/* Configure databus */
	if (!data->bus_on_d0)
		csa |= AT91_MATRIX_NFD0_ON_D16;
	else
		csa &= ~AT91_MATRIX_NFD0_ON_D16;
	/* Configure IO drive */
	csa |= AT91_MATRIX_EBI_HIGH_DRIVE;

	at91_sys_write(AT91_MATRIX_EBICSA, csa);

	/* enable pin */
	if (data->enable_pin)
		at91_set_gpio_output(data->enable_pin, 1);

	/* ready/busy pin */
	if (data->rdy_pin)
		at91_set_gpio_input(data->rdy_pin, 1);

	/* card detect pin */
	if (data->det_pin)
		at91_set_gpio_input(data->det_pin, 1);

	/* configure NANDOE */
	at91_set_A_periph(AT91_PIN_PD0, 1);
	/* configure NANDWE */
	at91_set_A_periph(AT91_PIN_PD1, 1);
	/* configure ALE */
	at91_set_A_periph(AT91_PIN_PD2, 1);
	/* configure CLE */
	at91_set_A_periph(AT91_PIN_PD3, 1);

	/* configure multiplexed pins for D16~D31 */
	if (!data->bus_on_d0) {
		at91_set_A_periph(AT91_PIN_PD6, 1);
		at91_set_A_periph(AT91_PIN_PD7, 1);
		at91_set_A_periph(AT91_PIN_PD8, 1);
		at91_set_A_periph(AT91_PIN_PD9, 1);
		at91_set_A_periph(AT91_PIN_PD10, 1);
		at91_set_A_periph(AT91_PIN_PD11, 1);
		at91_set_A_periph(AT91_PIN_PD12, 1);
		at91_set_A_periph(AT91_PIN_PD13, 1);

		if (data->bus_width_16) {
			at91_set_A_periph(AT91_PIN_PD14, 1);
			at91_set_A_periph(AT91_PIN_PD15, 1);
			at91_set_A_periph(AT91_PIN_PD16, 1);
			at91_set_A_periph(AT91_PIN_PD17, 1);
			at91_set_A_periph(AT91_PIN_PD18, 1);
			at91_set_A_periph(AT91_PIN_PD19, 1);
			at91_set_A_periph(AT91_PIN_PD20, 1);
			at91_set_A_periph(AT91_PIN_PD21, 1);
		}

	}

	nand_data = *data;
	platform_device_register(&at91sam9n12_nand_device);
}
#else
void __init at91_add_device_nand(struct atmel_nand_data *data) {}
#endif

/* --------------------------------------------------------------------
 *  TWI (i2c)
 * -------------------------------------------------------------------- */

/*
 * Prefer the GPIO code since the TWI controller isn't robust
 * (gets overruns and underruns under load) and can only issue
 * repeated STARTs in one scenario (the driver doesn't yet handle them).
 */
#if defined(CONFIG_I2C_GPIO) || defined(CONFIG_I2C_GPIO_MODULE)
static struct i2c_gpio_platform_data pdata_i2c0 = {
	.sda_pin		= AT91_PIN_PA30,
	.sda_is_open_drain	= 1,
	.scl_pin		= AT91_PIN_PA31,
	.scl_is_open_drain	= 1,
	.udelay			= 2,		/* ~100 kHz */
};

static struct platform_device at91sam9n12_twi0_device = {
	.name			= "i2c-gpio",
	.id			= 0,
	.dev.platform_data	= &pdata_i2c0,
};

void __init at91_add_device_i2c(short i2c_id, struct i2c_board_info *devices, int nr_devices)
{
	i2c_register_board_info(i2c_id, devices, nr_devices);

	if (i2c_id == 0) {
		at91_set_GPIO_periph(AT91_PIN_PA30, 1);		/* TWD (SDA) */
		at91_set_multi_drive(AT91_PIN_PA30, 1);

		at91_set_GPIO_periph(AT91_PIN_PA31, 1);		/* TWCK (SCL) */
		at91_set_multi_drive(AT91_PIN_PA31, 1);

		platform_device_register(&at91sam9n12_twi0_device);
	}
}

#elif defined(CONFIG_I2C_AT91) || defined(CONFIG_I2C_AT91_MODULE)
static struct resource twi0_resources[] = {
	[0] = {
		.start	= AT91SAM9N12_BASE_TWI0,
		.end	= AT91SAM9N12_BASE_TWI0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_TWI0,
		.end	= AT91SAM9N12_ID_TWI0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9n12_twi0_device = {
	.name		= "at91_i2c",
	.id		= 0,
	.resource	= twi0_resources,
	.num_resources	= ARRAY_SIZE(twi0_resources),
};

void __init at91_add_device_i2c(short i2c_id, struct i2c_board_info *devices, int nr_devices)
{
	i2c_register_board_info(i2c_id, devices, nr_devices);

	/* pins used for TWI interface */
	if (i2c_id == 0) {
		at91_set_A_periph(AT91_PIN_PA30, 0);		/* TWD */
		at91_set_multi_drive(AT91_PIN_PA30, 1);

		at91_set_A_periph(AT91_PIN_PA31, 0);		/* TWCK */
		at91_set_multi_drive(AT91_PIN_PA31, 1);

		platform_device_register(&at91sam9n12_twi0_device);
	}
}
#else
void __init at91_add_device_i2c(short i2c_id, struct i2c_board_info *devices, int nr_devices) {}
#endif

/* --------------------------------------------------------------------
 *  SPI
 * -------------------------------------------------------------------- */
#if defined(CONFIG_SPI_ATMEL) || defined(CONFIG_SPI_ATMEL_MODULE)
static u64 spi_dmamask = DMA_BIT_MASK(32);
static struct at_dma_slave spi0_sdata, spi1_sdata;

static struct resource spi0_resources[] = {
	[0] = {
		.start	= AT91SAM9N12_BASE_SPI0,
		.end	= AT91SAM9N12_BASE_SPI0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_SPI0,
		.end	= AT91SAM9N12_ID_SPI0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9n12_spi0_device = {
	.name		= "atmel_spi",
	.id		= 0,
	.dev		= {
				.dma_mask		= &spi_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &spi0_sdata,
	},
	.resource	= spi0_resources,
	.num_resources	= ARRAY_SIZE(spi0_resources),
};

static const unsigned spi0_standard_cs[4] = { AT91_PIN_PA14, AT91_PIN_PA7, AT91_PIN_PA1, AT91_PIN_PB3 };

static struct resource spi1_resources[] = {
	[0] = {
		.start	= AT91SAM9N12_BASE_SPI1,
		.end	= AT91SAM9N12_BASE_SPI1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_SPI1,
		.end	= AT91SAM9N12_ID_SPI1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9n12_spi1_device = {
	.name		= "atmel_spi",
	.id		= 1,
	.dev		= {
				.dma_mask		= &spi_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &spi1_sdata,
	},
	.resource	= spi1_resources,
	.num_resources	= ARRAY_SIZE(spi1_resources),
};

static const unsigned spi1_standard_cs[4] = { AT91_PIN_PA8, AT91_PIN_PA0, AT91_PIN_PA31, AT91_PIN_PA30 };

void __init at91_add_device_spi(struct spi_board_info *devices, int nr_devices)
{
	int i;
	unsigned long cs_pin;
	short enable_spi0 = 0;
	short enable_spi1 = 0;
#if defined(CONFIG_AT_HDMAC) || defined(CONFIG_AT_HDMAC_MODULE)
	struct at_dma_slave *atslave;
#endif

	/* Choose SPI chip-selects */
	for (i = 0; i < nr_devices; i++) {
		if (devices[i].controller_data)
			cs_pin = (unsigned long) devices[i].controller_data;
		else if (devices[i].bus_num == 0)
			cs_pin = spi0_standard_cs[devices[i].chip_select];
		else
			cs_pin = spi1_standard_cs[devices[i].chip_select];

		if (devices[i].bus_num == 0)
			enable_spi0 = 1;
		else
			enable_spi1 = 1;

		/* enable chip-select pin */
		at91_set_gpio_output(cs_pin, 1);

		/* pass chip-select pin to driver */
		devices[i].controller_data = (void *) cs_pin;
	}

	spi_register_board_info(devices, nr_devices);


	/* Configure SPI bus(es) */
	if (enable_spi0) {
		at91_set_A_periph(AT91_PIN_PA11, 0);	/* SPI0_MISO */
		at91_set_A_periph(AT91_PIN_PA12, 0);	/* SPI0_MOSI */
		at91_set_A_periph(AT91_PIN_PA13, 0);	/* SPI0_SPCK */

#if defined(CONFIG_AT_HDMAC) || defined(CONFIG_AT_HDMAC_MODULE)
		atslave = at91sam9n12_spi0_device.dev.platform_data;

		/* DMA slave channel configuration */
		atslave->dma_dev = &at_hdmac_device.dev;
		atslave->reg_width = AT_DMA_SLAVE_WIDTH_8BIT; /* or 16bits??????? */
		atslave->cfg = ATC_FIFOCFG_HALFFIFO
				| ATC_SRC_H2SEL_HW | ATC_DST_H2SEL_HW
				| ATC_SRC_PER(AT_DMA_ID_SPI0_RX)
				| ATC_DST_PER(AT_DMA_ID_SPI0_TX);
		/*atslave->ctrla = ATC_SCSIZE_16 | ATC_DCSIZE_16;*/ /* Chunk size to 0????? */
#endif

		at91_clock_associate("spi0_clk", &at91sam9n12_spi0_device.dev, "spi_clk");
		platform_device_register(&at91sam9n12_spi0_device);
	}
	if (enable_spi1) {
		at91_set_B_periph(AT91_PIN_PA21, 0);	/* SPI1_MISO */
		at91_set_B_periph(AT91_PIN_PA22, 0);	/* SPI1_MOSI */
		at91_set_B_periph(AT91_PIN_PA23, 0);	/* SPI1_SPCK */

#if defined(CONFIG_AT_HDMAC) || defined(CONFIG_AT_HDMAC_MODULE)
		atslave = at91sam9n12_spi1_device.dev.platform_data;

		/* DMA slave channel configuration */
		atslave->dma_dev = &at_hdmac_device.dev;
		atslave->reg_width = AT_DMA_SLAVE_WIDTH_8BIT; /* or 16bits??????? */
		atslave->cfg = ATC_FIFOCFG_HALFFIFO
				| ATC_SRC_H2SEL_HW | ATC_DST_H2SEL_HW
				| ATC_SRC_PER(AT_DMA_ID_SPI1_RX)
				| ATC_DST_PER(AT_DMA_ID_SPI1_TX);
		/*atslave->ctrla = ATC_SCSIZE_16 | ATC_DCSIZE_16;*/ /* Chunk size to 0????? */
#endif

		at91_clock_associate("spi1_clk", &at91sam9n12_spi1_device.dev, "spi_clk");
		platform_device_register(&at91sam9n12_spi1_device);
	}
}
#else
void __init at91_add_device_spi(struct spi_board_info *devices, int nr_devices) {}
#endif

/* --------------------------------------------------------------------
 *  LCD Controller
 * -------------------------------------------------------------------- */
#if defined(CONFIG_FB_ATMEL) || defined(CONFIG_FB_ATMEL_MODULE)
static u64 lcdc_dmamask = DMA_BIT_MASK(32);
static struct atmel_lcdfb_info lcdc_data;

static struct resource lcdc_base_resources[] = {
	[0] = {
		.start	= AT91SAM9N12_BASE_LCDC,
		.end	= AT91SAM9N12_BASE_LCDC + 0xff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_BASE_LCDC + ATMEL_LCDC_BASECLUT,
		.end	= AT91SAM9N12_BASE_LCDC + ATMEL_LCDC_BASECLUT + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= AT91SAM9N12_ID_LCDC,
		.end	= AT91SAM9N12_ID_LCDC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91_lcdc_base_device = {
	.name		= "atmel_hlcdfb_base",
	.id		= 0,
	.dev		= {
				.dma_mask		= &lcdc_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &lcdc_data,
	},
	.resource	= lcdc_base_resources,
	.num_resources	= ARRAY_SIZE(lcdc_base_resources),
};

void __init at91_add_device_lcdc(struct atmel_lcdfb_info *data)
{
	if (!data)
		return;

	/* Pin definition */
	at91_set_A_periph(AT91_PIN_PC26, 0);	/* LCDPWM */

	at91_set_A_periph(AT91_PIN_PC27, 0);	/* LCDVSYNC */
	at91_set_A_periph(AT91_PIN_PC28, 0);	/* LCDHSYNC */

	at91_set_A_periph(AT91_PIN_PC24, 0);	/* LCDDISP */
	at91_set_A_periph(AT91_PIN_PC29, 0);	/* LCDDEN */
	at91_set_A_periph(AT91_PIN_PC30, 0);	/* LCDPCK */

	at91_set_A_periph(AT91_PIN_PC0, 0);	/* LCDD0 */
	at91_set_A_periph(AT91_PIN_PC1, 0);	/* LCDD1 */
	at91_set_A_periph(AT91_PIN_PC2, 0);	/* LCDD2 */
	at91_set_A_periph(AT91_PIN_PC3, 0);	/* LCDD3 */
	at91_set_A_periph(AT91_PIN_PC4, 0);	/* LCDD4 */
	at91_set_A_periph(AT91_PIN_PC5, 0);	/* LCDD5 */
	at91_set_A_periph(AT91_PIN_PC6, 0);	/* LCDD6 */
	at91_set_A_periph(AT91_PIN_PC7, 0);	/* LCDD7 */
	at91_set_A_periph(AT91_PIN_PC8, 0);	/* LCDD8 */
	at91_set_A_periph(AT91_PIN_PC9, 0);	/* LCDD9 */
	at91_set_A_periph(AT91_PIN_PC10, 0);	/* LCDD10 */
	at91_set_A_periph(AT91_PIN_PC11, 0);	/* LCDD11 */
	at91_set_A_periph(AT91_PIN_PC12, 0);	/* LCDD12 */
	at91_set_A_periph(AT91_PIN_PC13, 0);	/* LCDD13 */
	at91_set_A_periph(AT91_PIN_PC14, 0);	/* LCDD14 */
	at91_set_A_periph(AT91_PIN_PC15, 0);	/* LCDD15 */
	at91_set_A_periph(AT91_PIN_PC16, 0);	/* LCDD16 */
	at91_set_A_periph(AT91_PIN_PC17, 0);	/* LCDD17 */
	at91_set_A_periph(AT91_PIN_PC18, 0);	/* LCDD18 */
	at91_set_A_periph(AT91_PIN_PC19, 0);	/* LCDD19 */
	at91_set_A_periph(AT91_PIN_PC20, 0);	/* LCDD20 */
	at91_set_A_periph(AT91_PIN_PC21, 0);	/* LCDD21 */
	at91_set_A_periph(AT91_PIN_PC22, 0);	/* LCDD22 */
	at91_set_A_periph(AT91_PIN_PC23, 0);	/* LCDD23 */

	lcdc_data = *data;
	platform_device_register(&at91_lcdc_base_device);
}
#else
void __init at91_add_device_lcdc(struct atmel_lcdfb_info *data) {}
#endif

/* --------------------------------------------------------------------
 *  Timer/Counter block
 * -------------------------------------------------------------------- */

#ifdef CONFIG_ATMEL_TCLIB
static struct resource tcb0_resources[] = {
	[0] = {
		.start	= AT91SAM9N12_BASE_TCB0,
		.end	= AT91SAM9N12_BASE_TCB0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_TCB,
		.end	= AT91SAM9N12_ID_TCB,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9n12_tcb0_device = {
	.name		= "atmel_tcb",
	.id		= 0,
	.resource	= tcb0_resources,
	.num_resources	= ARRAY_SIZE(tcb0_resources),
};

/* TCB1 begins with TC3 */
static struct resource tcb1_resources[] = {
	[0] = {
		.start	= AT91SAM9N12_BASE_TCB1,
		.end	= AT91SAM9N12_BASE_TCB1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_TCB,
		.end	= AT91SAM9N12_ID_TCB,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9n12_tcb1_device = {
	.name		= "atmel_tcb",
	.id		= 1,
	.resource	= tcb1_resources,
	.num_resources	= ARRAY_SIZE(tcb1_resources),
};

static void __init at91_add_device_tc(void)
{
	/* this chip has one clock and irq for all six TC channels */
	at91_clock_associate("tcb0_clk", &at91sam9n12_tcb0_device.dev, "t0_clk");
	platform_device_register(&at91sam9n12_tcb0_device);
	at91_clock_associate("tcb1_clk", &at91sam9n12_tcb1_device.dev, "t0_clk");
	platform_device_register(&at91sam9n12_tcb1_device);
}
#else
static void __init at91_add_device_tc(void) { }
#endif

/* --------------------------------------------------------------------
 *  RTC
 * -------------------------------------------------------------------- */

#if defined(CONFIG_RTC_DRV_AT91RM9200) || defined(CONFIG_RTC_DRV_AT91RM9200_MODULE)
static struct platform_device at91sam9n12_rtc_device = {
	.name		= "at91_rtc",
	.id		= -1,
	.num_resources	= 0,
};

static void __init at91_add_device_rtc(void)
{
	platform_device_register(&at91sam9n12_rtc_device);
}
#else
static void __init at91_add_device_rtc(void) {}
#endif

/* --------------------------------------------------------------------
 *  Touchscreen
 * -------------------------------------------------------------------- */

#if defined(CONFIG_TOUCHSCREEN_ATMEL_TSADCC) || defined(CONFIG_TOUCHSCREEN_ATMEL_TSADCC_MODULE)
static u64 tsadcc_dmamask = DMA_BIT_MASK(32);
static struct at91_tsadcc_data tsadcc_data;

static struct resource tsadcc_resources[] = {
	[0] = {
		.start	= AT91SAM9N12_BASE_ADC,
		.end	= AT91SAM9N12_BASE_ADC + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_ADC,
		.end	= AT91SAM9N12_ID_ADC,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device at91sam9n12_tsadcc_device = {
	.name		= "atmel_tsadcc",
	.id		= -1,
	.dev		= {
				.dma_mask		= &tsadcc_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &tsadcc_data,
	},
	.resource	= tsadcc_resources,
	.num_resources	= ARRAY_SIZE(tsadcc_resources),
};

void __init at91_add_device_tsadcc(struct at91_tsadcc_data *data)
{
	if (!data)
		return;

	/* In AT91SAM9N12-EK, using default pins for touch screen. */

	tsadcc_data = *data;
	at91_clock_associate("adc_clk", &at91sam9n12_tsadcc_device.dev, "tsc_clk");
	platform_device_register(&at91sam9n12_tsadcc_device);
}
#else
void __init at91_add_device_tsadcc(struct at91_tsadcc_data *data) {}
#endif

/* --------------------------------------------------------------------
 *  Watchdog
 * -------------------------------------------------------------------- */

#if defined(CONFIG_AT91SAM9X_WATCHDOG) || defined(CONFIG_AT91SAM9X_WATCHDOG_MODULE)
static struct platform_device at91sam9n12_wdt_device = {
	.name		= "at91_wdt",
	.id		= -1,
	.num_resources	= 0,
};

static void __init at91_add_device_watchdog(void)
{
	platform_device_register(&at91sam9n12_wdt_device);
}
#else
static void __init at91_add_device_watchdog(void) {}
#endif


/* --------------------------------------------------------------------
 *  PWM
 * --------------------------------------------------------------------*/

#if defined(CONFIG_ATMEL_PWM) || defined(CONFIG_ATMEL_PWM_MODULE)
static u32 pwm_mask;

static struct resource pwm_resources[] = {
	[0] = {
		.start	= AT91SAM9N12_BASE_PWMC,
		.end	= AT91SAM9N12_BASE_PWMC + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_PWMC,
		.end	= AT91SAM9N12_ID_PWMC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9n12_pwm_device = {
	.name	= "atmel_pwm",
	.id	= -1,
	.dev	= {
		.platform_data		= &pwm_mask,
	},
	.resource	= pwm_resources,
	.num_resources	= ARRAY_SIZE(pwm_resources),
};

void __init at91_add_device_pwm(u32 mask)
{
	if (mask & (1 << AT91_PWM0))
		at91_set_B_periph(AT91_PIN_PB11, 1);	/* enable PWM0 */

	if (mask & (1 << AT91_PWM1))
		at91_set_B_periph(AT91_PIN_PB12, 1);	/* enable PWM1 */

	if (mask & (1 << AT91_PWM2))
		at91_set_B_periph(AT91_PIN_PB13, 1);	/* enable PWM2 */

	if (mask & (1 << AT91_PWM3))
		at91_set_B_periph(AT91_PIN_PB14, 1);	/* enable PWM3 */

	pwm_mask = mask;

	platform_device_register(&at91sam9n12_pwm_device);
}
#else
void __init at91_add_device_pwm(u32 mask) {}
#endif


/* --------------------------------------------------------------------
 *  SSC -- Synchronous Serial Controller
 * -------------------------------------------------------------------- */

#if defined(CONFIG_ATMEL_SSC) || defined(CONFIG_ATMEL_SSC_MODULE)
static u64 ssc_dmamask = DMA_BIT_MASK(32);
static struct at_dma_slave ssc_sdata;

static struct resource ssc_resources[] = {
	[0] = {
		.start	= AT91SAM9N12_BASE_SSC,
		.end	= AT91SAM9N12_BASE_SSC + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_SSC,
		.end	= AT91SAM9N12_ID_SSC,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device at91sam9n12_ssc_device = {
	.name	= "ssc",
	.id	= 0,
	.dev	= {
		.dma_mask		= &ssc_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
		.platform_data		= &ssc_sdata,
	},
	.resource	= ssc_resources,
	.num_resources	= ARRAY_SIZE(ssc_resources),
};

static inline void configure_ssc_pins(unsigned pins)
{
	if (pins & ATMEL_SSC_TF)
		at91_set_B_periph(AT91_PIN_PA25, 1);
	if (pins & ATMEL_SSC_TK)
		at91_set_B_periph(AT91_PIN_PA24, 1);
	if (pins & ATMEL_SSC_TD)
		at91_set_B_periph(AT91_PIN_PA26, 1);
	if (pins & ATMEL_SSC_RD)
		at91_set_B_periph(AT91_PIN_PA27, 1);
	if (pins & ATMEL_SSC_RK)
		at91_set_B_periph(AT91_PIN_PA28, 1);
	if (pins & ATMEL_SSC_RF)
		at91_set_B_periph(AT91_PIN_PA29, 1);
}

/*
 * SSC controllers are accessed through library code, instead of any
 * kind of all-singing/all-dancing driver.  For example one could be
 * used by a particular I2S audio codec's driver, while another one
 * on the same system might be used by a custom data capture driver.
 */
void __init at91_add_device_ssc(unsigned id, unsigned pins)
{
	struct platform_device *pdev;

	/*
	 * NOTE: caller is responsible for passing information matching
	 * "pins" to whatever will be using each particular controller.
	 */
	if (id == AT91SAM9N12_ID_SSC) {
#if defined(CONFIG_AT_HDMAC) || defined(CONFIG_AT_HDMAC_MODULE)
		struct at_dma_slave *atslave;

		atslave = at91sam9n12_ssc_device.dev.platform_data;

		/* DMA slave channel configuration */
		atslave->dma_dev = &at_hdmac_device.dev;
		atslave->reg_width = AT_DMA_SLAVE_WIDTH_16BIT;
		atslave->cfg = ATC_FIFOCFG_HALFFIFO
				| ATC_SRC_H2SEL_HW | ATC_DST_H2SEL_HW
				| ATC_SRC_PER(AT_DMA_ID_SSC_RX & 0xf)
				| (((AT_DMA_ID_SSC_RX & 0x30) >> 4) << 10)
				| ATC_DST_PER(AT_DMA_ID_SSC_TX & 0xf)
				| (((AT_DMA_ID_SSC_TX & 0x30) >> 4) << 14);
#endif

		pdev = &at91sam9n12_ssc_device;
		configure_ssc_pins(pins);
		at91_clock_associate("ssc_clk", &pdev->dev, "pclk");
	}
	else
		return;

	platform_device_register(pdev);
}

#else
void __init at91_add_device_ssc(unsigned id, unsigned pins) {}
#endif


/* --------------------------------------------------------------------
 *  UART
 * -------------------------------------------------------------------- */

#if defined(CONFIG_SERIAL_ATMEL)
static struct resource dbgu_resources[] = {
	[0] = {
		.start	= AT91_VA_BASE_SYS + AT91_DBGU,
		.end	= AT91_VA_BASE_SYS + AT91_DBGU + SZ_512 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91_ID_SYS,
		.end	= AT91_ID_SYS,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data dbgu_data = {
	.use_dma_tx	= 0,
	.use_dma_rx	= 0,
	.regs		= (void __iomem *)(AT91_VA_BASE_SYS + AT91_DBGU),
};

static u64 dbgu_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9n12_dbgu_device = {
	.name		= "atmel_usart",
	.id		= 0,
	.dev		= {
				.dma_mask		= &dbgu_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &dbgu_data,
	},
	.resource	= dbgu_resources,
	.num_resources	= ARRAY_SIZE(dbgu_resources),
};

static inline void configure_dbgu_pins(void)
{
	at91_set_A_periph(AT91_PIN_PA10, 1);		/* DTXD */
	at91_set_A_periph(AT91_PIN_PA9, 0);		/* DRXD */
}

static struct resource usart0_resources[] = {
	[0] = {
		.start	= AT91SAM9N12_BASE_USART0,
		.end	= AT91SAM9N12_BASE_USART0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_USART0,
		.end	= AT91SAM9N12_ID_USART0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data usart0_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 0,				/* doesn't support */
};

static u64 usart0_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9n12_usart0_device = {
	.name		= "atmel_usart",
	.id		= 1,
	.dev		= {
				.dma_mask		= &usart0_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &usart0_data,
	},
	.resource	= usart0_resources,
	.num_resources	= ARRAY_SIZE(usart0_resources),
};

static inline void configure_usart0_pins(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PA0, 1);		/* TXD0 */
	at91_set_A_periph(AT91_PIN_PA1, 0);		/* RXD0 */

	if (pins & ATMEL_UART_RTS)
		at91_set_A_periph(AT91_PIN_PA2, 0);	/* RTS0 */
	if (pins & ATMEL_UART_CTS)
		at91_set_A_periph(AT91_PIN_PA3, 0);	/* CTS0 */
}

static struct resource usart1_resources[] = {
	[0] = {
		.start	= AT91SAM9N12_BASE_USART1,
		.end	= AT91SAM9N12_BASE_USART1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_USART1,
		.end	= AT91SAM9N12_ID_USART1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data usart1_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 usart1_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9n12_usart1_device = {
	.name		= "atmel_usart",
	.id		= 2,
	.dev		= {
				.dma_mask		= &usart1_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &usart1_data,
	},
	.resource	= usart1_resources,
	.num_resources	= ARRAY_SIZE(usart1_resources),
};

static inline void configure_usart1_pins(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PA5, 1);		/* TXD1 */
	at91_set_A_periph(AT91_PIN_PA6, 0);		/* RXD1 */

	if (pins & ATMEL_UART_RTS)
		at91_set_C_periph(AT91_PIN_PC27, 0);	/* RTS1 */
	if (pins & ATMEL_UART_CTS)
		at91_set_C_periph(AT91_PIN_PC28, 0);	/* CTS1 */
}

static struct resource usart2_resources[] = {
	[0] = {
		.start	= AT91SAM9N12_BASE_USART2,
		.end	= AT91SAM9N12_BASE_USART2 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_USART2,
		.end	= AT91SAM9N12_ID_USART2,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data usart2_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 usart2_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9n12_usart2_device = {
	.name		= "atmel_usart",
	.id		= 3,
	.dev		= {
				.dma_mask		= &usart2_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &usart2_data,
	},
	.resource	= usart2_resources,
	.num_resources	= ARRAY_SIZE(usart2_resources),
};

static inline void configure_usart2_pins(unsigned pins)
{
	at91_set_A_periph(AT91_PIN_PA7, 1);		/* TXD2 */
	at91_set_A_periph(AT91_PIN_PA8, 0);		/* RXD2 */

	if (pins & ATMEL_UART_RTS)
		at91_set_B_periph(AT91_PIN_PB0, 0);	/* RTS2 */
	if (pins & ATMEL_UART_CTS)
		at91_set_B_periph(AT91_PIN_PB1, 0);	/* CTS2 */
}

static struct resource usart3_resources[] = {
	[0] = {
		.start	= AT91SAM9N12_BASE_USART3,
		.end	= AT91SAM9N12_BASE_USART3 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_USART3,
		.end	= AT91SAM9N12_ID_USART3,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data usart3_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 usart3_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9n12_usart3_device = {
	.name		= "atmel_usart",
	.id		= 4,
	.dev		= {
				.dma_mask		= &usart3_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &usart3_data,
	},
	.resource	= usart3_resources,
	.num_resources	= ARRAY_SIZE(usart3_resources),
};

static inline void configure_usart3_pins(unsigned pins)
{
	at91_set_B_periph(AT91_PIN_PC22, 1);		/* TXD3 */
	at91_set_B_periph(AT91_PIN_PC23, 0);		/* RXD3 */

	if (pins & ATMEL_UART_RTS)
		at91_set_B_periph(AT91_PIN_PC24, 0);	/* RTS3 */
	if (pins & ATMEL_UART_CTS)
		at91_set_B_periph(AT91_PIN_PC25, 0);	/* CTS3 */
}

static struct resource uart0_resources[] = {
	[0] = {
		.start	= AT91SAM9N12_BASE_UART0,
		.end	= AT91SAM9N12_BASE_UART0 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_UART0,
		.end	= AT91SAM9N12_ID_UART0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart0_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart0_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9n12_uart0_device = {
	.name		= "atmel_usart",
	.id		= 5,
	.dev		= {
				.dma_mask		= &uart0_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &uart0_data,
	},
	.resource	= uart0_resources,
	.num_resources	= ARRAY_SIZE(uart0_resources),
};

static inline void configure_uart0_pins(unsigned pins)
{
	at91_set_C_periph(AT91_PIN_PC8, 1);		/* UTXD0 */
	at91_set_C_periph(AT91_PIN_PC9, 0);		/* URXD0 */
}

static struct resource uart1_resources[] = {
	[0] = {
		.start	= AT91SAM9N12_BASE_UART1,
		.end	= AT91SAM9N12_BASE_UART1 + SZ_16K - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= AT91SAM9N12_ID_UART1,
		.end	= AT91SAM9N12_ID_UART1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct atmel_uart_data uart1_data = {
	.use_dma_tx	= 1,
	.use_dma_rx	= 1,
};

static u64 uart1_dmamask = DMA_BIT_MASK(32);

static struct platform_device at91sam9n12_uart1_device = {
	.name		= "atmel_usart",
	.id		= 6,
	.dev		= {
				.dma_mask		= &uart1_dmamask,
				.coherent_dma_mask	= DMA_BIT_MASK(32),
				.platform_data		= &uart1_data,
	},
	.resource	= uart1_resources,
	.num_resources	= ARRAY_SIZE(uart1_resources),
};

static inline void configure_uart1_pins(unsigned pins)
{
	at91_set_C_periph(AT91_PIN_PC16, 1);		/* UTXD1 */
	at91_set_C_periph(AT91_PIN_PC17, 0);		/* URXD1 */
}

static struct platform_device *__initdata at91_usarts[ATMEL_MAX_UART];	/* the USARTs to use */
struct platform_device *atmel_default_console_device;	/* the serial console device */

void __init at91_register_uart(unsigned id, unsigned portnr, unsigned pins)
{
	struct platform_device *pdev;

	switch (id) {
		case 0:		/* DBGU */
			pdev = &at91sam9n12_dbgu_device;
			configure_dbgu_pins();
			at91_clock_associate("mck", &pdev->dev, "usart");
			break;
		case AT91SAM9N12_ID_USART0:
			pdev = &at91sam9n12_usart0_device;
			configure_usart0_pins(pins);
			at91_clock_associate("usart0_clk", &pdev->dev, "usart");
			break;
		case AT91SAM9N12_ID_USART1:
			pdev = &at91sam9n12_usart1_device;
			configure_usart1_pins(pins);
			at91_clock_associate("usart1_clk", &pdev->dev, "usart");
			break;
		case AT91SAM9N12_ID_USART2:
			pdev = &at91sam9n12_usart2_device;
			configure_usart2_pins(pins);
			at91_clock_associate("usart2_clk", &pdev->dev, "usart");
			break;
		case AT91SAM9N12_ID_USART3:
			pdev = &at91sam9n12_usart3_device;
			configure_usart3_pins(pins);
			at91_clock_associate("usart3_clk", &pdev->dev, "usart");
			break;
		case AT91SAM9N12_ID_UART0:
			pdev = &at91sam9n12_uart0_device;
			configure_uart0_pins(pins);
			at91_clock_associate("uart0_clk", &pdev->dev, "usart");
			break;
		case AT91SAM9N12_ID_UART1:
			pdev = &at91sam9n12_uart1_device;
			configure_uart1_pins(pins);
			at91_clock_associate("uart1_clk", &pdev->dev, "usart");
			break;
		default:
			return;
	}
	pdev->id = portnr;		/* update to mapped ID */

	if (portnr < ATMEL_MAX_UART)
		at91_usarts[portnr] = pdev;
}

void __init at91_set_serial_console(unsigned portnr)
{
	if (portnr < ATMEL_MAX_UART)
		atmel_default_console_device = at91_usarts[portnr];
}

void __init at91_add_device_serial(void)
{
	int i;

	for (i = 0; i < ATMEL_MAX_UART; i++) {
		if (at91_usarts[i]) {
#if defined(CONFIG_AT_HDMAC) || defined(CONFIG_AT_HDMAC_MODULE)
			struct atmel_uart_data *pdata	= at91_usarts[i]->dev.platform_data;

			if (pdata->use_dma_tx) {
				struct at_dma_slave	*atslave;

				atslave = kzalloc(sizeof(struct at_dma_slave), GFP_KERNEL);

				/* DMA slave channel configuration */
				/*
				if (peripheral_id == AT91SAM9N12_ID_USART0
				    || peripheral_id == AT91SAM9N12_ID_USART1
				    || peripheral_id == AT91SAM9N12_ID_UART0)
				 */
				atslave->dma_dev = &at_hdmac_device.dev;

				atslave->reg_width = DW_DMA_SLAVE_WIDTH_8BIT;
				atslave->cfg = ATC_FIFOCFG_HALFFIFO
						| ATC_SRC_H2SEL_SW | ATC_DST_H2SEL_HW
						| (AT_DMA_ID_USART0_TX << 4); /*ATC_DST_PER(peripheral_id);*/

				pdata->dma_tx_slave = atslave;
			}
#endif
			platform_device_register(at91_usarts[i]);
		}
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
	at91_add_device_rtc();
	at91_add_device_watchdog();
	at91_add_device_tc();
	return 0;
}

arch_initcall(at91_add_standard_devices);
