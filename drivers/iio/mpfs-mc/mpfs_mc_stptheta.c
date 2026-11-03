// SPDX-License-Identifier: GPL-2.0
/*
 * Motor Stepper Theta IIO Driver
 *
 * This driver provides IIO interface for stepper motor position control
 * registers including slew count, rate limit, and step commands.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/of.h>
#include <linux/of_address.h>

/* Register offsets */
#define REG_SLEW_CNT      0x0
#define REG_RATE_LIMIT    0x4
#define REG_CMD_STEP      0x8

struct stptheta_dev {
	void __iomem *base;
	struct device *dev;
};

/* Read/write helpers */
#define IIO_REG_RW(_name, _reg)                                             \
static ssize_t _name##_show(struct device *dev,                             \
		struct device_attribute *attr, char *buf)                       \
{                                                                           \
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);                    \
	struct stptheta_dev *data = iio_priv(indio_dev);                    \
	u32 val = readl(data->base + (_reg));                               \
	return sysfs_emit(buf, "%u\n", val);                                \
}                                                                           \
static ssize_t _name##_store(struct device *dev,                            \
		struct device_attribute *attr, const char *buf, size_t len) \
{                                                                           \
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);                    \
	struct stptheta_dev *data = iio_priv(indio_dev);                    \
	u32 val;                                                            \
	if (kstrtou32(buf, 0, &val))                                        \
		return -EINVAL;                                             \
	writel(val, data->base + (_reg));                                   \
	return len;                                                         \
}                                                                           \
IIO_DEVICE_ATTR(_name, 0664, _name##_show, _name##_store, 0)

/* Define IIO attributes */
IIO_REG_RW(slew_cnt, REG_SLEW_CNT);
IIO_REG_RW(rate_limit, REG_RATE_LIMIT);
IIO_REG_RW(cmd_step, REG_CMD_STEP);

static struct attribute *stptheta_attrs[] = {
	&iio_dev_attr_slew_cnt.dev_attr.attr,
	&iio_dev_attr_rate_limit.dev_attr.attr,
	&iio_dev_attr_cmd_step.dev_attr.attr,
	NULL,
};

static const struct attribute_group stptheta_attr_group = {
	.attrs = stptheta_attrs,
};

static const struct iio_info stptheta_info = {
	.attrs = &stptheta_attr_group,
};

static int stptheta_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct stptheta_dev *data;
	struct resource *res;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->dev = &pdev->dev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	indio_dev->name = "mpfs_mc_stptheta";
	indio_dev->info = &stptheta_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static const struct of_device_id mpfs_mc_stptheta_of_match[] = {
	{ .compatible = "microchip,stepper-theta-rtl-v4.2", },
	{ }
};
MODULE_DEVICE_TABLE(of, mpfs_mc_stptheta_of_match);

static struct platform_driver mpfs_mc_stptheta_driver = {
	.probe = stptheta_probe,
	.driver = {
		.name = "mpfs_mc_stptheta",
		.of_match_table = mpfs_mc_stptheta_of_match,
	},
};
module_platform_driver(mpfs_mc_stptheta_driver);

MODULE_AUTHOR("sunny bezawada <sunny.bezawada@microchip.com>");
MODULE_DESCRIPTION("MPFS Motor Control Stepper Theta IIO driver for position and step control");
MODULE_LICENSE("GPL");
