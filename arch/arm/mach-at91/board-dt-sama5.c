/*
 *  Setup code for SAMA5 Evaluation Kits with Device Tree support
 *
 *  Copyright (C) 2013 Atmel,
 *                2013 Ludovic Desroches <ludovic.desroches@atmel.com>
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/micrel_phy.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/phy.h>

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/firmware.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/system_misc.h>

#include "at91_aic.h"
#include "generic.h"

/************************************/
/* TEMPORARY NON-DT STUFF FOR ISLERO */
/************************************/
#include <linux/fb.h>
#include <video/atmel_lcdfb.h>
#include <mach/atmel_hlcdc.h>
/*
 * LCD Controller
 */
static struct fb_videomode at91_tft_vga_modes[] = {
	{
      .name		= "ingenico",
      .refresh	= 60,
      .xres		= 320,
      .yres		= 480,
      .pixclock	= KHZ2PICOS(11125),

		.left_margin	= 10, .right_margin	= 40,
		.upper_margin	= 2,  .lower_margin	= 4,
		.hsync_len     = 10, .vsync_len     = 2,

		.sync		= 0,
		.vmode		= FB_VMODE_NONINTERLACED,
	},
};

static struct fb_monspecs at91fb_default_monspecs = {
	.manufacturer	= "ILI",
	.monitor	= "ILI9481",

	.modedb		= at91_tft_vga_modes,
	.modedb_len	= ARRAY_SIZE(at91_tft_vga_modes),
	.hfmin		= 15000,
   .hfmax		= 35000,
   .vfmin		= 55,
   .vfmax		= 65,
};

/* Default output mode is TFT 24 bit */
#define BPP_OUT_DEFAULT_LCDCFG5	(LCDC_LCDCFG5_MODE_OUTPUT_24BPP)

/* Driver datas */
static struct atmel_lcdfb_info __initdata ek_lcdc_primary_dev_data = {
   .lcdcon_is_backlight    = true,
   .alpha_enabled          = false,
   .default_bpp            = 32,
	/* Reserve enough memory for 32bpp and full res, with at least, some auxiliary back buffer (directfb) */
	.smem_len			      = (320 * 480 * 4) + (320 * 480 * 4),
   /* default_lcdcon2 is used for LCDCFG5 */
   .default_lcdcon2		= BPP_OUT_DEFAULT_LCDCFG5,
   .default_monspecs		= &at91fb_default_monspecs,
   .guard_time          = 30,
   .lcd_wiring_mode		= ATMEL_LCDC_WIRING_RGB,
};

static struct atmel_lcdfb_info __initdata ek_lcdc_overlay_dev_data = {
	.lcdcon_is_backlight    = true,
	.alpha_enabled          = false,
	.default_bpp            = 32,
	/* Reserve enough memory for 32bpp and full res and that's it (for now) */
	.smem_len			      = (320 * 480 * 4),
	/* In sama5 default_lcdcon2 is used for LCDCFG5 */
	.default_lcdcon2		   = BPP_OUT_DEFAULT_LCDCFG5,
	.default_monspecs		   = &at91fb_default_monspecs,
	.guard_time			      = 30,
	.lcd_wiring_mode		   = ATMEL_LCDC_WIRING_RGB,
};

static struct atmel_lcdfb_info __initdata ek_lcdc_heo_dev_data = {
	.lcdcon_is_backlight    = true,
	.alpha_enabled          = false,
	.default_bpp            = 32,
	/* Reserve enough memory for 32bpp and full res and that's it (for now) */
	.smem_len			      = (320 * 480 * 4),
	/* In sama5 default_lcdcon2 is used for LCDCFG5 */
	.default_lcdcon2		   = BPP_OUT_DEFAULT_LCDCFG5,
	.default_monspecs		   = &at91fb_default_monspecs,
	.guard_time			      = 30,
	.lcd_wiring_mode		   = ATMEL_LCDC_WIRING_RGB,
};


struct of_dev_auxdata at91_auxdata_lookup[] __initdata = {
	//OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xf8038000, "atmel_hlcdfb_base", &ek_lcdc_data), /*ingenico: don't need those alt CS*/
	//OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xf8038100, "atmel_hlcdfb_ovl1", &ek_lcdc_data),
	//OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xf0030000, "atmel_hlcdfb_base", &ek_lcdc_data),
	//OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xf0030140, "atmel_hlcdfb_ovl1", &ek_lcdc_data),
	//OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xf0030240, "atmel_hlcdfb_ovl2", &ek_lcdc_data),
	OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xF0000000, "atmel_hlcd_base", &ek_lcdc_primary_dev_data),
	OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xF0000140, "atmel_hlcd_ovl1", &ek_lcdc_overlay_dev_data),
	OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xF0000240, "atmel_hlcd_ovl2", &ek_lcdc_overlay_dev_data),
   OF_DEV_AUXDATA("atmel,at91sam9x5-lcd", 0xF0030340, "atmel_hlcd_heo",  &ek_lcdc_heo_dev_data),
	{ /* sentinel */ }
};

static const struct of_device_id irq_of_match[] __initconst = {

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

static int ksz8081_phy_reset(struct phy_device *phy)
{
	int value;

	/*
	 * As disconnect the hardware reset, so use software reset
	 *
	 * The basic control (register 0) bit 15 is software reset
	 */
	value = phy_read(phy, 0);
	value |= (1 << 15);
	phy_write(phy, 0, value);

	return 0;
}

#ifdef CONFIG_CACHE_L2X0
static void __init at91_init_l2cache(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "arm,pl310-cache");
	if (!np)
		return;
	of_node_put(np);

	call_firmware_op(l2x0_init);

	outer_cache.disable = firmware_ops->l2x0_disable;

	l2x0_of_init(0, ~0UL);
}
#else
static inline void at91_init_l2cache(void) {}
#endif

static void sama5d4ing_alt_idle(void)
{
	call_firmware_op(do_idle);
}

/* Hardware reset is performed by Secure OS (TrustZone) */
static void sama5d4ing_alt_restart(char mode, const char *cmd)
{
	call_firmware_op(do_restart);
}

static void __init sama5_dt_device_init(void)
{
	at91_init_l2cache();

	if (of_machine_is_compatible("atmel,sama5d3xcm") &&
	    IS_ENABLED(CONFIG_PHYLIB))
		phy_register_fixup_for_uid(PHY_ID_KSZ9021, MICREL_PHY_ID_MASK,
			ksz9021rn_phy_fixup);
	if (of_machine_is_compatible("atmel,sama5d4ek") &&
	    IS_ENABLED(CONFIG_PHYLIB))
		phy_register_fixup_for_uid(PHY_ID_KSZ8081, MICREL_PHY_ID_MASK,
			ksz8081_phy_reset);

	if (of_machine_is_compatible("atmel,sama5d4ing")){
		arm_pm_idle = sama5d4ing_alt_idle;
		arm_pm_restart = sama5d4ing_alt_restart;
	}
	of_platform_populate(NULL, of_default_bus_match_table, at91_auxdata_lookup, NULL);
}


static const char *sama5d4_dt_board_compat[] __initdata = {
	"atmel,sama5d4ek",
	"atmel,sama5d4ing",
	NULL
};

DT_MACHINE_START(sama5d4_dt, "Atmel SAMA5D4 (Device Tree)")
	/* Maintainer: Atmel */
	.init_time	= tcbmmio_init,
	.map_io		= at91_map_io,
	.handle_irq	= at91_aic5_handle_irq,
	.init_early	= at91_dt_initialize,
	.init_irq	= at91_dt_init_irq,
	.init_machine	= sama5_dt_device_init,
	.dt_compat	= sama5d4_dt_board_compat,
MACHINE_END

static const char *sama5_dt_board_compat[] __initdata = {
	"atmel,sama5",
	NULL
};

DT_MACHINE_START(sama5_dt, "Atmel SAMA5 (Device Tree)")
	/* Maintainer: Atmel */
	.init_time	= at91sam926x_pit_init,
	.map_io		= at91_map_io,
	.handle_irq	= at91_aic5_handle_irq,
	.init_early	= at91_dt_initialize,
	.init_irq	= at91_dt_init_irq,
	.init_machine	= sama5_dt_device_init,
	.dt_compat	= sama5_dt_board_compat,
MACHINE_END
