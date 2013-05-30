/*
 * Copyright (C) 2013 Atmel,
 *                    Nicolas Ferre <nicolas.ferre@atmel.com>
 *
 * From Exynos firmware interface by Samsung Electronics,
 * Kyungmin Park <kyungmin.park@samsung.com>,
 * Tomasz Figa <t.figa@samsung.com>
 *
 * This program is free software,you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/of.h>
/*#include <linux/of_address.h>*/

#include <asm/firmware.h>

#include "atmel-firmware-smc.h"

static int atmel_nwd_l2cache_enable(void)
{
	atmel_smc(SMC_CMD_L2CC_ENABLE, 0, 0, 0);
	return 0;
}

static void atmel_nwd_l2cache_disable(void)
{
	atmel_smc(SMC_CMD_L2CC_DISABLE, 0, 0, 0);
}

static const struct firmware_ops atmel_firmware_ops = {
	.l2x0_init		= atmel_nwd_l2cache_enable,
	.l2x0_disable		= atmel_nwd_l2cache_disable,
};

void atmel_firmware_init(void)
{
	if (of_have_populated_dt()) {
		struct device_node *nd;

		nd = of_find_compatible_node(NULL, NULL,
						"atmel,secure-firmware");
		if (!nd)
			return;

#if 0 /* Not needed for the moment */
		{
		const __be32 *addr;
		addr = of_get_address(nd, 0, NULL, NULL);
		if (!addr) {
			pr_err("%s: No address specified.\n", __func__);
			return;
		}
		}
#endif
		pr_info("AT91: running under secure firmware\n");

		register_firmware_ops(&atmel_firmware_ops);

	}
}
