/*
 * Setup code for SAMA7
 *
 * Copyright (C) 2017 Atmel
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/of.h>
#include <linux/of_platform.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/system_misc.h>

#include "generic.h"
#if 0
#include "soc.h"

static const struct at91_soc sama7_socs[] = {
	AT91_SOC(SAMA7G_CIDR_MATCH, SAMA7G5_EXID_MATCH, 
		 "sama7g5", "sama7g"),
	{ /* sentinel */ },
};
#endif

static void __init sama7_common_init(void)
{
//	struct soc_device *soc;
//	struct device *soc_dev = NULL;

//	soc = at91_soc_init(sama7_socs);
//	if (soc != NULL)
//		soc_dev = soc_device_to_device(soc);

	of_platform_default_populate(NULL, NULL, NULL);
}

static void __init sama7_dt_device_init(void)
{
	sama7_common_init();
	/*sama7_pm_init();*/
}

static const char *const sama7_dt_board_compat[] __initconst = {
	"atmel,sama7",
	NULL
};

DT_MACHINE_START(sama7_dt, "Atmel SAMA7")
	/* Maintainer: Atmel */
	.init_machine	= sama7_dt_device_init,
	.dt_compat	= sama7_dt_board_compat,
MACHINE_END

static const char *const sama7_alt_dt_board_compat[] __initconst = {
	"atmel,sama7g5",
	NULL
};

DT_MACHINE_START(sama7_alt_dt, "Atmel SAMA7")
	/* Maintainer: Atmel */
	.init_machine	= sama7_dt_device_init,
	.dt_compat	= sama7_alt_dt_board_compat,
	/*.l2c_aux_mask	= ~0UL,*/
MACHINE_END

