// SPDX-License-Identifier: GPL-2.0
/*
 * Motor PWM IIO Driver
 *
 * This driver provides IIO interface for PWM configuration registers
 * including period, dead time, delay time, and gain settings for
 * BLDC and Stepper motor control applications.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/of.h>
#include <linux/of_address.h>

/* BLDC register offsets */
#define REG_PWM_PERIOD_VAL_BLDC     0x000
#define REG_DEAD_TIME_BLDC          0x004
#define REG_DELAY_TIME_BLDC         0x008
#define REG_PWM_GAIN_BLDC           0x00C

/* Stepper register offsets */
#define REG_PWM_PERIOD_VAL_STEPPER  0x200
#define REG_DEAD_TIME_STEPPER       0x204
#define REG_DELAY_TIME_STEPPER      0x208
#define REG_PWM_GAIN_STEPPER        0x20C

struct pwm_dev {
	void __iomem *base;
	struct device *dev;
};

/* Define read/write helpers */
#define IIO_REG_RW(_name, _reg)                                             \
static ssize_t _name##_show(struct device *dev,                             \
		struct device_attribute *attr, char *buf)                       \
{                                                                           \
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);                    \
	struct pwm_dev *data = iio_priv(indio_dev);                         \
	u32 val = readl(data->base + (_reg));                               \
	return sysfs_emit(buf, "%u\n", val);                                \
}                                                                           \
static ssize_t _name##_store(struct device *dev,                            \
		struct device_attribute *attr, const char *buf, size_t len) \
{                                                                           \
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);                    \
	struct pwm_dev *data = iio_priv(indio_dev);                         \
	u32 val;                                                            \
	if (kstrtou32(buf, 0, &val))                                        \
		return -EINVAL;                                             \
	writel(val, data->base + (_reg));                                   \
	return len;                                                         \
}                                                                           \
IIO_DEVICE_ATTR(_name, 0664, _name##_show, _name##_store, 0)

/* Define IIO attributes - BLDC */
IIO_REG_RW(pwm_period_val_bldc,    REG_PWM_PERIOD_VAL_BLDC);
IIO_REG_RW(dead_time_bldc,         REG_DEAD_TIME_BLDC);
IIO_REG_RW(delay_time_bldc,        REG_DELAY_TIME_BLDC);
IIO_REG_RW(pwm_gain_bldc,          REG_PWM_GAIN_BLDC);

/* Define IIO attributes - Stepper */
IIO_REG_RW(pwm_period_val_stepper, REG_PWM_PERIOD_VAL_STEPPER);
IIO_REG_RW(dead_time_stepper,      REG_DEAD_TIME_STEPPER);
IIO_REG_RW(delay_time_stepper,     REG_DELAY_TIME_STEPPER);
IIO_REG_RW(pwm_gain_stepper,       REG_PWM_GAIN_STEPPER);

static struct attribute *pwm_attrs[] = {
	/* BLDC */
	&iio_dev_attr_pwm_period_val_bldc.dev_attr.attr,
	&iio_dev_attr_dead_time_bldc.dev_attr.attr,
	&iio_dev_attr_delay_time_bldc.dev_attr.attr,
	&iio_dev_attr_pwm_gain_bldc.dev_attr.attr,

	/* Stepper */
	&iio_dev_attr_pwm_period_val_stepper.dev_attr.attr,
	&iio_dev_attr_dead_time_stepper.dev_attr.attr,
	&iio_dev_attr_delay_time_stepper.dev_attr.attr,
	&iio_dev_attr_pwm_gain_stepper.dev_attr.attr,

	NULL,
};

static const struct attribute_group pwm_attr_group = {
	.attrs = pwm_attrs,
};

static const struct iio_info pwm_info = {
	.attrs = &pwm_attr_group,
};

static int pwm_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct pwm_dev *data;
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

	indio_dev->name = "mpfs_mc_pwm";
	indio_dev->info = &pwm_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static const struct of_device_id mpfs_mc_pwm_of_match[] = {
	{ .compatible = "microchip,pwm3ph-rtl-v4.2", },
	{ }
};
MODULE_DEVICE_TABLE(of, mpfs_mc_pwm_of_match);

static struct platform_driver mpfs_mc_pwm_driver = {
	.probe  = pwm_probe,
	.driver = {
		.name = "mpfs_mc_pwm",
		.of_match_table = mpfs_mc_pwm_of_match,
	},
};
module_platform_driver(mpfs_mc_pwm_driver);

MODULE_AUTHOR("sunny bezawada <sunny.bezawada@microchip.com>");
MODULE_DESCRIPTION("MPFS Motor Control 3-Phase PWM IIO driver for period, dead time, and gain configuration");
MODULE_LICENSE("GPL");
