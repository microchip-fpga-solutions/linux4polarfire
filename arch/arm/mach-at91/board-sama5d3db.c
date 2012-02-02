#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <asm/setup.h>
#include <asm/mach-types.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/board.h>
#include <mach/gpio.h>
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
 * MCI (SD/MMC)
 *
 */
static struct mci_platform_data __initdata mci0_data = {
	.slot[0] = {
		.bus_width      = 4,
		.detect_pin     = 0,
	},
};

static void __init db_board_init(void)
{
	/* Serial */
	at91_add_device_serial();
	/* MMC0 */
	at91_add_device_mci(0, &mci0_data);
}

MACHINE_START(SAMA5D3DB, "Atmel SAMA5D3-DB")
	.timer		= &at91sam926x_timer,
	.map_io		= at91_map_io,
	.init_early	= db_init_early,
	.init_irq	= at91_init_irq_default,
	.init_machine	= db_board_init
MACHINE_END
