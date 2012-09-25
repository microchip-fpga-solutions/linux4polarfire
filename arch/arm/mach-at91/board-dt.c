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

/************************************/
/* TEMPORARY NON-DT STUFF FOR MIURA */
/************************************/
#include <linux/fb.h>
#include <video/atmel_lcdfb.h>
#include <mach/atmel_hlcdc.h>

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

struct of_dev_auxdata at91_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xf8038000, "atmel_hlcdfb_base", &ek_lcdc_data),
	OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xf8038100, "atmel_hlcdfb_ovl", &ek_lcdc_data),
	OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xf0030000, "atmel_hlcdfb_base", &ek_lcdc_data),
	OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xf0030140, "atmel_hlcdfb_ovl", &ek_lcdc_data),
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
		at91_set_A_periph(AT91_PIN_PA30, 0);    /* TWD0 */
		at91_set_A_periph(AT91_PIN_PA31, 0);    /* TWCK0 */
		at91_set_B_periph(AT91_PIN_PC26, 0);    /* TWD1 */
		at91_set_B_periph(AT91_PIN_PC27, 0);    /* TWCK1 */
		printk("AT91: i2c pin mux done\n");

		phy_register_fixup_for_uid(PHY_ID_KSZ9021, MICREL_PHY_ID_MASK,
					   ksz9021rn_phy_fixup);
		at91_pinmux_lcd();
	}

	of_platform_populate(NULL, of_default_bus_match_table, at91_auxdata_lookup, NULL);
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
