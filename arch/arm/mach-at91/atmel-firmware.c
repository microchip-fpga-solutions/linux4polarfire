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

/*
 * L2CC
 */
static int atmel_nwd_l2cache_enable(void)
{
	atmel_smc(SMC_CMD_L2CC_ENABLE, 0, 0, 0);
	return 0;
}

static void atmel_nwd_l2cache_disable(void)
{
	atmel_smc(SMC_CMD_L2CC_DISABLE, 0, 0, 0);
}

/*
 * PMC
 */
static int atmel_nwd_pmc_read_reg(u32 *reg_value, u32 reg_offset)
{
	u32 val;

	val = atmel_smc(SMC_CMD_PMC_READ, reg_offset, 0, 0);

	if (val == -1)
		return -EINVAL;

	*reg_value = val;
	return 0;
}

static int atmel_nwd_pmc_periph_clk(u32 periph_id, u32 is_on)
{
	return atmel_smc(SMC_CMD_PMC_PERIPH_CLK, periph_id, is_on, 0);
}

static int atmel_nwd_pmc_sys_clk(u32 sys_clk_mask, u32 is_on)
{
	return atmel_smc(SMC_CMD_PMC_SYS_CLK, sys_clk_mask, is_on, 0);
}

static int atmel_nwd_pmc_uckr_clk(u32 is_on)
{
	return atmel_smc(SMC_CMD_PMC_UCKR_CLK, is_on, 0, 0);
}

static int atmel_nwd_pmc_usb_setup(void)
{
	return atmel_smc(SMC_CMD_PMC_USB_SETUP, 0, 0, 0);
}

/*
 * Ingenico specific
 */
static int atmel_nwd_idle(void)
{
	return atmel_smc(0, 0, 0, 0);
}

static int atmel_nwd_restart(void)
{
	return atmel_smc(SMC_CMD_REBOOT, 0, 0, 0);
}

#define SMC_CMD_REBOOT		0x80000030
static const struct firmware_ops atmel_firmware_ops = {
	.l2x0_init		= atmel_nwd_l2cache_enable,
	.l2x0_disable		= atmel_nwd_l2cache_disable,
	.pmc_read_reg		= atmel_nwd_pmc_read_reg,
	.pmc_periph_clk		= atmel_nwd_pmc_periph_clk,
	.pmc_sys_clk		= atmel_nwd_pmc_sys_clk,
	.pmc_uckr_clk		= atmel_nwd_pmc_uckr_clk,
	.pmc_usb_setup		= atmel_nwd_pmc_usb_setup,
	.do_idle		= atmel_nwd_idle,
	.do_restart		= atmel_nwd_restart,
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
