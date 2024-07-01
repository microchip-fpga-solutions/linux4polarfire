// SPDX-License-Identifier: GPL-2.0
/*
 * SiFive WorldGuard Checker driver
 *
 * Author: Andy Chiu <andy.chiu@sifive.com>
 * Copyright (C) 2023 SiFive, Inc.
 *
 */
#define pr_fmt(fmt) "wgchecker: " fmt

#include <linux/types.h>
#include <linux/printk.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/module.h>

struct wgchecker_struct {
	int irq;
	unsigned long mmio_base;
};

static irqreturn_t sifive_wgchecker_irq(int irq, void *pdev)
{
	pr_alert("detect access violations %d\n", irq);
	return IRQ_HANDLED;
}

static int sifive_wgchecker_probe(struct platform_device *pdev)
{
	struct wgchecker_struct *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq <= 0) {
		pr_err("cannot get irq from device\n");
		return -ENODEV;
	}

	ret = request_irq(priv->irq, sifive_wgchecker_irq, IRQF_SHARED,
			  pdev->name, pdev);

	return 0;
}

static const struct of_device_id sifive_wgchecker_ids[] = {
	{ .compatible = "sifive,wgchecker2" },
	{  }
};

static struct platform_driver sifive_wgchecker_driver = {
	.driver = {
		   .name = "SiFive WorldGuard Checker",
		   .of_match_table = sifive_wgchecker_ids,
		   },
	.probe = sifive_wgchecker_probe,
};

static int __init sifive_wgchecker_init(void)
{
	int ret;

	ret = platform_driver_register(&sifive_wgchecker_driver);
	if (ret)
		pr_err("Failed to register sifive_wgchecker_driver: %d\n", ret);

	return ret;
}
device_initcall(sifive_wgchecker_init);
