/*
 *  Setup code for AT91SAM Evaluation Kits with Device Tree support
 *
 *  Copyright (C) 2011 Atmel,
 *                2011 Nicolas Ferre <nicolas.ferre@atmel.com>
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/phy.h>
#include <linux/micrel_phy.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

#include <mach/board.h>
#include <mach/at91_aic.h>

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include "generic.h"
#include "clock.h"

/************************************/
/* TEMPORARY NON-DT STUFF FOR MIURA */
/************************************/
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/fb.h>

#include <video/atmel_lcdfb.h>
#include <mach/atmel_hlcdc.h>

#include <media/soc_camera.h>
#include <media/atmel-isi.h>

#include <mach/sama5d3.h>

/*
 * LCD Controller
 */
static struct fb_videomode at91_tft_vga_modes[] = {
	{
		.name		= "LG",
		.refresh	= 60,
		.xres		= 800,		.yres		= 480,
		.pixclock	= KHZ2PICOS(33260),

		.left_margin	= 88,		.right_margin	= 168,
		.upper_margin	= 8,		.lower_margin	= 37,
		.hsync_len	= 128,		.vsync_len	= 2,

		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

static struct fb_monspecs at91fb_default_monspecs = {
	.manufacturer	= "LG",
	.monitor	= "LB043WQ1",

	.modedb		= at91_tft_vga_modes,
	.modedb_len	= ARRAY_SIZE(at91_tft_vga_modes),
	.hfmin		= 15000,
	.hfmax		= 17640,
	.vfmin		= 57,
	.vfmax		= 67,
};

/* Default output mode is TFT 24 bit */
#define BPP_OUT_DEFAULT_LCDCFG5	(LCDC_LCDCFG5_MODE_OUTPUT_24BPP)

/* Driver datas */
static struct atmel_lcdfb_info __initdata ek_lcdc_data = {
	.lcdcon_is_backlight		= true,
	.alpha_enabled			= false,
	.default_bpp			= 16,
	/* Reserve enough memory for 32bpp */
	.smem_len			= 800 * 480 * 4,
	/* default_lcdcon2 is used for LCDCFG5 */
	.default_lcdcon2		= BPP_OUT_DEFAULT_LCDCFG5,
	.default_monspecs		= &at91fb_default_monspecs,
	.guard_time			= 9,
	.lcd_wiring_mode		= ATMEL_LCDC_WIRING_RGB,
};

void __init at91sam9x5_pinmux_lcd(void)
{
	at91_set_A_periph(AT91_PIN_PC26, 0);    /* LCDPWM */

	at91_set_A_periph(AT91_PIN_PC27, 0);    /* LCDVSYNC */
	at91_set_A_periph(AT91_PIN_PC28, 0);    /* LCDHSYNC */

	at91_set_A_periph(AT91_PIN_PC24, 0);    /* LCDDISP */
	at91_set_A_periph(AT91_PIN_PC29, 0);    /* LCDDEN */
	at91_set_A_periph(AT91_PIN_PC30, 0);    /* LCDPCK */

	at91_set_A_periph(AT91_PIN_PC0, 0);     /* LCDD0 */
	at91_set_A_periph(AT91_PIN_PC1, 0);     /* LCDD1 */
	at91_set_A_periph(AT91_PIN_PC2, 0);     /* LCDD2 */
	at91_set_A_periph(AT91_PIN_PC3, 0);     /* LCDD3 */
	at91_set_A_periph(AT91_PIN_PC4, 0);     /* LCDD4 */
	at91_set_A_periph(AT91_PIN_PC5, 0);     /* LCDD5 */
	at91_set_A_periph(AT91_PIN_PC6, 0);     /* LCDD6 */
	at91_set_A_periph(AT91_PIN_PC7, 0);     /* LCDD7 */
	at91_set_A_periph(AT91_PIN_PC8, 0);     /* LCDD8 */
	at91_set_A_periph(AT91_PIN_PC9, 0);     /* LCDD9 */
	at91_set_A_periph(AT91_PIN_PC10, 0);    /* LCDD10 */
	at91_set_A_periph(AT91_PIN_PC11, 0);    /* LCDD11 */
	at91_set_A_periph(AT91_PIN_PC12, 0);    /* LCDD12 */
	at91_set_A_periph(AT91_PIN_PC13, 0);    /* LCDD13 */
	at91_set_A_periph(AT91_PIN_PC14, 0);    /* LCDD14 */
	at91_set_A_periph(AT91_PIN_PC15, 0);    /* LCDD15 */
	at91_set_A_periph(AT91_PIN_PC16, 0);    /* LCDD16 */
	at91_set_A_periph(AT91_PIN_PC17, 0);    /* LCDD17 */
	at91_set_A_periph(AT91_PIN_PC18, 0);    /* LCDD18 */
	at91_set_A_periph(AT91_PIN_PC19, 0);    /* LCDD19 */
	at91_set_A_periph(AT91_PIN_PC20, 0);    /* LCDD20 */
	at91_set_A_periph(AT91_PIN_PC21, 0);    /* LCDD21 */
	at91_set_A_periph(AT91_PIN_PC22, 0);    /* LCDD22 */
	at91_set_A_periph(AT91_PIN_PC23, 0);    /* LCDD23 */

	printk("AT91: lcd pin mux done\n");
}

void __init at91_pinmux_lcd(void)
{
	at91_set_A_periph(AT91_PIN_PA24, 0);    /* LCDPWM */

	at91_set_A_periph(AT91_PIN_PA26, 0);    /* LCDVSYNC */
	at91_set_A_periph(AT91_PIN_PA27, 0);    /* LCDHSYNC */

	at91_set_A_periph(AT91_PIN_PA25, 0);    /* LCDDISP */
	at91_set_A_periph(AT91_PIN_PA29, 0);    /* LCDDEN */
	at91_set_A_periph(AT91_PIN_PA28, 0);    /* LCDPCK */

	at91_set_A_periph(AT91_PIN_PA0, 0);     /* LCDD0 */
	at91_set_A_periph(AT91_PIN_PA1, 0);     /* LCDD1 */
	at91_set_A_periph(AT91_PIN_PA2, 0);     /* LCDD2 */
	at91_set_A_periph(AT91_PIN_PA3, 0);     /* LCDD3 */
	at91_set_A_periph(AT91_PIN_PA4, 0);     /* LCDD4 */
	at91_set_A_periph(AT91_PIN_PA5, 0);     /* LCDD5 */
	at91_set_A_periph(AT91_PIN_PA6, 0);     /* LCDD6 */
	at91_set_A_periph(AT91_PIN_PA7, 0);     /* LCDD7 */
	at91_set_A_periph(AT91_PIN_PA8, 0);     /* LCDD8 */
	at91_set_A_periph(AT91_PIN_PA9, 0);     /* LCDD9 */
	at91_set_A_periph(AT91_PIN_PA10, 0);    /* LCDD10 */
	at91_set_A_periph(AT91_PIN_PA11, 0);    /* LCDD11 */
	at91_set_A_periph(AT91_PIN_PA12, 0);    /* LCDD12 */
	at91_set_A_periph(AT91_PIN_PA13, 0);    /* LCDD13 */
	at91_set_A_periph(AT91_PIN_PA14, 0);    /* LCDD14 */
	at91_set_A_periph(AT91_PIN_PA15, 0);    /* LCDD15 */
	at91_set_C_periph(AT91_PIN_PC14, 0);    /* LCDD16 */
	at91_set_C_periph(AT91_PIN_PC13, 0);    /* LCDD17 */
	at91_set_C_periph(AT91_PIN_PC12, 0);    /* LCDD18 */
	at91_set_C_periph(AT91_PIN_PC11, 0);    /* LCDD19 */
	at91_set_C_periph(AT91_PIN_PC10, 0);    /* LCDD20 */
	at91_set_C_periph(AT91_PIN_PC15, 0);    /* LCDD21 */
	at91_set_C_periph(AT91_PIN_PE27, 0);    /* LCDD22 */
	at91_set_C_periph(AT91_PIN_PE28, 0);    /* LCDD23 */
}
/*
 *  ISI
 */
static struct isi_platform_data __initdata isi_data = {
	.frate			= ISI_CFG1_FRATE_CAPTURE_ALL,
	/* to use codec and preview path simultaneously */
	.full_mode		= 1,
	.data_width_flags	= ISI_DATAWIDTH_8 | ISI_DATAWIDTH_10,
	/* ISI_MCK is provided by programmable clock or external clock */
	.mck_hz			= 25000000,
};

static struct clk_lookup isi_mck_lookups[] = {
	CLKDEV_CON_DEV_ID("isi_mck", "atmel_isi", NULL),
};

void __init at91_config_isi(bool use_pck_as_mck)
{
	struct clk *pck;
	struct clk *parent;

	at91_set_C_periph(AT91_PIN_PA16, 0);	/* ISI_D0 */
	at91_set_C_periph(AT91_PIN_PA17, 0);	/* ISI_D1 */
	at91_set_C_periph(AT91_PIN_PA18, 0);	/* ISI_D2 */
	at91_set_C_periph(AT91_PIN_PA19, 0);	/* ISI_D3 */
	at91_set_C_periph(AT91_PIN_PA20, 0);	/* ISI_D4 */
	at91_set_C_periph(AT91_PIN_PA21, 0);	/* ISI_D5 */
	at91_set_C_periph(AT91_PIN_PA22, 0);	/* ISI_D6 */
	at91_set_C_periph(AT91_PIN_PA23, 0);	/* ISI_D7 */
	at91_set_C_periph(AT91_PIN_PC30, 0);	/* ISI_PCK */
	at91_set_C_periph(AT91_PIN_PA31, 0);	/* ISI_HSYNC */
	at91_set_C_periph(AT91_PIN_PA30, 0);	/* ISI_VSYNC */
	at91_set_C_periph(AT91_PIN_PC29, 0);	/* ISI_PD8 */
	at91_set_C_periph(AT91_PIN_PC28, 0);	/* ISI_PD9 */

	if (use_pck_as_mck) {
		at91_set_B_periph(AT91_PIN_PC15, 0);	/* ISI_MCK (PCK2) */

		pck = clk_get(NULL, "pck2");
		parent = clk_get(NULL, "plla");

		BUG_ON(IS_ERR(pck) || IS_ERR(parent));

		if (clk_set_parent(pck, parent)) {
			pr_err("Failed to set PCK's parent\n");
		} else {
			/* Register PCK as ISI_MCK */
			isi_mck_lookups[0].clk = pck;
			clkdev_add_table(isi_mck_lookups,
				ARRAY_SIZE(isi_mck_lookups));
		}

		clk_put(pck);
		clk_put(parent);
	}
}

/*
 * soc-camera OV2640
 */
#if defined(CONFIG_SOC_CAMERA_OV2640) || \
	defined(CONFIG_SOC_CAMERA_OV2640_MODULE)
static unsigned long isi_camera_query_bus_param(struct soc_camera_link *link)
{
	/* ISI board for ek using default 8-bits connection */
	return SOCAM_DATAWIDTH_8;
}

static int i2c_camera_power(struct device *dev, int on)
{
	/* enable or disable the camera */
	pr_debug("%s: %s the camera\n", __func__, on ? "ENABLE" : "DISABLE");
	at91_set_gpio_output(AT91_PIN_PE29 , !on);

	if (!on)
		goto out;

	/* If enabled, give a reset impulse */
	at91_set_gpio_output(AT91_PIN_PE28, 0);
	msleep(20);
	at91_set_gpio_output(AT91_PIN_PE28, 1);
	msleep(100);

out:
	return 0;
}

static struct i2c_board_info i2c_camera = {
	I2C_BOARD_INFO("ov2640", 0x30),
};

static struct soc_camera_link iclink_ov2640 = {
	.bus_id			= -1,
	.board_info		= &i2c_camera,
	.i2c_adapter_id		= 1,
	.power			= i2c_camera_power,
	.query_bus_param	= isi_camera_query_bus_param,
};

static struct platform_device isi_ov2640 = {
	.name	= "soc-camera-pdrv",
	.id	= 0,
	.dev	= {
		.platform_data = &iclink_ov2640,
	},
};

static struct platform_device *devices[] __initdata = {
	&isi_ov2640,
};
#endif

struct of_dev_auxdata at91_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xf8038000, "atmel_hlcdfb_base", &ek_lcdc_data),
	OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xf8038100, "atmel_hlcdfb_ovl", &ek_lcdc_data),
	OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xf0030000, "atmel_hlcdfb_base", &ek_lcdc_data),
	OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xf0030140, "atmel_hlcdfb_ovl", &ek_lcdc_data),
	OF_DEV_AUXDATA("atmel,at91sam9g45-isi", 0xf0034000, "atmel_isi", &isi_data),
	{ /* sentinel */ }
};

/************************************/
/*              END                 */
/************************************/

static const struct of_device_id irq_of_match[] __initconst = {

	{ .compatible = "atmel,at91rm9200-aic", .data = at91_aic_of_init },
	{ .compatible = "atmel,sama5d3-aic", .data = at91_aic5_of_init },
	{ /*sentinel*/ }
};

static void __init at91_dt_init_irq(void)
{
	of_irq_init(irq_of_match);
}

static int ksz9021rn_phy_fixup(struct phy_device *phy)
{
	int value;

#define GMII_RCCPSR	260
#define GMII_RRDPSR	261
#define GMII_ERCR	11
#define GMII_ERDWR	12

	/* Set delay values */
	value = GMII_RCCPSR | 0x8000;
	phy_write(phy, GMII_ERCR, value);
	value = 0xF2F4;
	phy_write(phy, GMII_ERDWR, value);
	value = GMII_RRDPSR | 0x8000;
	phy_write(phy, GMII_ERCR, value);
	value = 0x2222;
	phy_write(phy, GMII_ERDWR, value);

	return 0;
}

static void __init at91_dt_device_init(void)
{
	/* Temporary pin mux stuff */
	if (of_machine_is_compatible("atmel,at91sam9x5")) {
		at91_set_gpio_input(AT91_PIN_PA7, 1);
		printk("AT91: qt1070 pin mux done\n");

		at91sam9x5_pinmux_lcd();
	}

	if (of_machine_is_compatible("atmel,sama5ek")) {
		struct device_node *np;

		phy_register_fixup_for_uid(PHY_ID_KSZ9021, MICREL_PHY_ID_MASK,
					   ksz9021rn_phy_fixup);

		np = of_find_compatible_node(NULL, NULL, "atmel,at91sam9g45-isi");
		if (np) {
			if (of_device_is_available(np))
				/* reset and pck2 pins is conflicted with LCD */
				at91_config_isi(true);
			else
				at91_pinmux_lcd();
		} else {
			at91_pinmux_lcd();
		}
	}

	of_platform_populate(NULL, of_default_bus_match_table, at91_auxdata_lookup, NULL);
#if defined(CONFIG_SOC_CAMERA_OV2640) \
	|| defined(CONFIG_SOC_CAMERA_OV2640_MODULE)
	/* add ov2640 camera device */
	platform_add_devices(devices, ARRAY_SIZE(devices));
#endif
}

static const char *sama5_dt_board_compat[] __initdata = {
	"atmel,sama5ek",
	NULL
};

DT_MACHINE_START(sama5_dt, "Atmel SAMA5 (Device Tree)")
	/* Maintainer: Atmel */
	.timer		= &at91sam926x_timer,
	.map_io		= at91_map_io,
	.handle_irq	= at91_aic5_handle_irq,
	.init_early	= at91_dt_initialize,
	.init_irq	= at91_dt_init_irq,
	.init_machine	= at91_dt_device_init,
	.dt_compat	= sama5_dt_board_compat,
MACHINE_END

static const char *at91_dt_board_compat[] __initdata = {
	"atmel,at91sam9",
	NULL
};

DT_MACHINE_START(at91sam_dt, "Atmel AT91SAM (Device Tree)")
	/* Maintainer: Atmel */
	.timer		= &at91sam926x_timer,
	.map_io		= at91_map_io,
	.handle_irq	= at91_aic_handle_irq,
	.init_early	= at91_dt_initialize,
	.init_irq	= at91_dt_init_irq,
	.init_machine	= at91_dt_device_init,
	.dt_compat	= at91_dt_board_compat,
MACHINE_END
