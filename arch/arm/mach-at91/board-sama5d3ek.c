#include <linux/types.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <video/atmel_lcdfb.h>

#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/cpu.h>
#include <mach/board.h>
#include <mach/gpio.h>
#include <mach/atmel_hlcdc.h>
#include <mach/sama5d3_matrix.h>

#include "generic.h"


static void __init db_init_early(void)
{
	/* Initialiaze processor: 12.000 MHz crystal */
	at91_initialize(12000000);

	/* DBGU on ttyS0 */
	at91_register_uart(0, 0, 0);

	/* Set serial console to ttyS0 (ie DBGU) */
	at91_set_serial_console(0);
}

/*
 * USB HS Host port (common to OHCI & EHCI)
 */
static struct at91_usbh_data __initdata ek_usbh_hs_data = {
	.ports		= 2,
	.vbus_pin	= {AT91_PIN_PD26, AT91_PIN_PD27},
};


/*
 * USB HS Device port
 */
static struct usba_platform_data __initdata ek_usba_udc_data = {
	.vbus_pin	= AT91_PIN_PD29,
};

/*
 * MCI (SD/MMC)
 *
 */
static struct mci_platform_data __initdata mci0_data = {
	.slot[0] = {
		.bus_width      = 4,
		.detect_pin     = 0,
	},
};

/*
 * MACB Ethernet device
 */
static struct macb_platform_data __initdata ek_macb_data = {
	.is_rmii        = 1,
};


/*
 * GMACB Ethernet device
 */
static struct macb_platform_data __initdata ek_gmacb_data = {
	.phy_irq_pin	= AT91_PIN_PB25,
	.is_rmii	= 1,
};

/*
 * LCD Controller
 */
#if defined(CONFIG_FB_ATMEL_HLCD) || defined(CONFIG_FB_ATMEL_HLCD_MODULE)
static struct fb_videomode at91_tft_vga_modes[] = {
	{
		.name           = "LG",
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
	.monitor        = "LB043WQ1",

	.modedb		= at91_tft_vga_modes,
	.modedb_len	= ARRAY_SIZE(at91_tft_vga_modes),
	.hfmin		= 15000,
	.hfmax		= 17640,
	.vfmin		= 57,
	.vfmax		= 67,
};

/* Default output mode is TFT 24 bit */
#define SAMA5D3_DEFAULT_LCDCFG5	(LCDC_LCDCFG5_MODE_OUTPUT_24BPP)

/* Driver datas */
static struct atmel_lcdfb_info __initdata ek_lcdc_data = {
	.lcdcon_is_backlight		= true,
	.alpha_enabled			= false,
	.default_bpp			= 16,
	/* Reserve enough memory for 32bpp */
	.smem_len			= 800 * 480 * 4,
	/* In sama5 default_lcdcon2 is used for LCDCFG5 */
	.default_lcdcon2		= SAMA5D3_DEFAULT_LCDCFG5,
	.default_monspecs		= &at91fb_default_monspecs,
	.guard_time			= 9,
	.lcd_wiring_mode		= ATMEL_LCDC_WIRING_RGB,
};

#else
static struct atmel_lcdfb_info __initdata ek_lcdc_data;
#endif

static void __init db_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* USB HS Host */
	at91_add_device_usbh_ohci(&ek_usbh_hs_data);
	at91_add_device_usbh_ehci(&ek_usbh_hs_data);
	/* USB HS Device */
	at91_add_device_usba(&ek_usba_udc_data);
	/* MMC0 */
	if (!cpu_is_sama5d33())
		at91_add_device_mci(0, &mci0_data);
	/* Ethernet */
	if (cpu_is_sama5d31() || cpu_is_sama5d35())
		at91_add_device_eth(&ek_macb_data);
	if (!cpu_is_sama5d31())
		at91_add_device_eth_giga(&ek_gmacb_data);
	/* LCD Controller */
	if (!cpu_is_sama5d35())
		at91_add_device_lcdc(&ek_lcdc_data);
}

MACHINE_START(SAMA5D3DB, "Atmel SAMA5D3-DB")
	.timer		= &at91sam926x_timer,
	.map_io		= at91_map_io,
	.init_early	= db_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= db_board_init
MACHINE_END
