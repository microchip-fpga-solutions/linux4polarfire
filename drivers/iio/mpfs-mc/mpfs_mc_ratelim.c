// SPDX-License-Identifier: GPL-2.0
/*
 * Motor Rate Limiter IIO Driver
 *
 * This driver provides IIO interface for motor rate limiter and
 * direction configuration registers for BLDC and Stepper motor
 * control applications.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/of.h>
#include <linux/of_address.h>

/* Register Offsets - BLDC */
#define REG_RATE_LIMIT_REF_BLDC          0x00
#define REG_RATE_LIMIT_SLEW_CNT_BLDC     0x04
#define REG_RATE_LIMIT_RATE_CNT_BLDC     0x08
#define REG_DIRECTION_CONFIG_BLDC        0x0C
#define REG_SQMNG_DV_BLDC                0x10
#define REG_SQMNG_IQ_REF_BLDC            0x14
#define REG_SQMNG_THETA_FACTOR_BLDC      0x18

/* Output registers - BLDC */
#define REG_OLMNG_THETA_BLDC             0x100
#define REG_RLIMIT_RATE_BLDC             0x104

/* Register Offsets - Stepper */
#define REG_RATE_LIMIT_REF_STEPPER       0x200
#define REG_RATE_LIMIT_SLEW_CNT_STEPPER  0x204
#define REG_RATE_LIMIT_RATE_CNT_STEPPER  0x208
#define REG_DIRECTION_CONFIG_STEPPER     0x20C
#define REG_SQMNG_DV_STEPPER             0x210
#define REG_SQMNG_IQ_REF_STEPPER         0x214
#define REG_SQMNG_THETA_FACTOR_STEPPER   0x218

/* Output registers - Stepper */
#define REG_OLMNG_THETA_STEPPER          0x300
#define REG_RLIMIT_RATE_STEPPER          0x304

struct rate_ctrl_dev {
	void __iomem *base;
	struct device *dev;
};

/* Define read/write helpers */
#define IIO_REG_RW(_name, _reg)                                             \
static ssize_t _name##_show(struct device *dev,                             \
		struct device_attribute *attr, char *buf)                       \
{                                                                           \
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);                    \
	struct rate_ctrl_dev *data = iio_priv(indio_dev);                   \
	u32 val = readl(data->base + (_reg));                               \
	return sysfs_emit(buf, "%u\n", val);                                \
}                                                                           \
static ssize_t _name##_store(struct device *dev,                            \
		struct device_attribute *attr, const char *buf, size_t len) \
{                                                                           \
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);                    \
	struct rate_ctrl_dev *data = iio_priv(indio_dev);                   \
	u32 val;                                                            \
	if (kstrtou32(buf, 0, &val))                                        \
		return -EINVAL;                                             \
	writel(val, data->base + (_reg));                                   \
	return len;                                                         \
}                                                                           \
IIO_DEVICE_ATTR(_name, 0664, _name##_show, _name##_store, 0)

/* Define IIO attributes - BLDC */
IIO_REG_RW(rate_limit_ref_bldc,        REG_RATE_LIMIT_REF_BLDC);
IIO_REG_RW(rate_limit_slew_cnt_bldc,   REG_RATE_LIMIT_SLEW_CNT_BLDC);
IIO_REG_RW(rate_limit_rate_cnt_bldc,   REG_RATE_LIMIT_RATE_CNT_BLDC);
IIO_REG_RW(direction_config_bldc,      REG_DIRECTION_CONFIG_BLDC);
IIO_REG_RW(sqmng_dv_bldc,              REG_SQMNG_DV_BLDC);
IIO_REG_RW(sqmng_iq_ref_bldc,          REG_SQMNG_IQ_REF_BLDC);
IIO_REG_RW(sqmng_theta_factor_bldc,    REG_SQMNG_THETA_FACTOR_BLDC);
IIO_REG_RW(olmng_theta_bldc,           REG_OLMNG_THETA_BLDC);
IIO_REG_RW(rlimit_rate_bldc,           REG_RLIMIT_RATE_BLDC);

/* Define IIO attributes - Stepper */
IIO_REG_RW(rate_limit_ref_stepper,        REG_RATE_LIMIT_REF_STEPPER);
IIO_REG_RW(rate_limit_slew_cnt_stepper,   REG_RATE_LIMIT_SLEW_CNT_STEPPER);
IIO_REG_RW(rate_limit_rate_cnt_stepper,   REG_RATE_LIMIT_RATE_CNT_STEPPER);
IIO_REG_RW(direction_config_stepper,      REG_DIRECTION_CONFIG_STEPPER);
IIO_REG_RW(sqmng_dv_stepper,              REG_SQMNG_DV_STEPPER);
IIO_REG_RW(sqmng_iq_ref_stepper,          REG_SQMNG_IQ_REF_STEPPER);
IIO_REG_RW(sqmng_theta_factor_stepper,    REG_SQMNG_THETA_FACTOR_STEPPER);
IIO_REG_RW(olmng_theta_stepper,           REG_OLMNG_THETA_STEPPER);
IIO_REG_RW(rlimit_rate_stepper,           REG_RLIMIT_RATE_STEPPER);

static struct attribute *rate_ctrl_attrs[] = {
	/* BLDC attributes */
	&iio_dev_attr_rate_limit_ref_bldc.dev_attr.attr,
	&iio_dev_attr_rate_limit_slew_cnt_bldc.dev_attr.attr,
	&iio_dev_attr_rate_limit_rate_cnt_bldc.dev_attr.attr,
	&iio_dev_attr_direction_config_bldc.dev_attr.attr,
	&iio_dev_attr_sqmng_dv_bldc.dev_attr.attr,
	&iio_dev_attr_sqmng_iq_ref_bldc.dev_attr.attr,
	&iio_dev_attr_sqmng_theta_factor_bldc.dev_attr.attr,
	&iio_dev_attr_olmng_theta_bldc.dev_attr.attr,
	&iio_dev_attr_rlimit_rate_bldc.dev_attr.attr,

	/* Stepper attributes */
	&iio_dev_attr_rate_limit_ref_stepper.dev_attr.attr,
	&iio_dev_attr_rate_limit_slew_cnt_stepper.dev_attr.attr,
	&iio_dev_attr_rate_limit_rate_cnt_stepper.dev_attr.attr,
	&iio_dev_attr_direction_config_stepper.dev_attr.attr,
	&iio_dev_attr_sqmng_dv_stepper.dev_attr.attr,
	&iio_dev_attr_sqmng_iq_ref_stepper.dev_attr.attr,
	&iio_dev_attr_sqmng_theta_factor_stepper.dev_attr.attr,
	&iio_dev_attr_olmng_theta_stepper.dev_attr.attr,
	&iio_dev_attr_rlimit_rate_stepper.dev_attr.attr,

	NULL,
};

static const struct attribute_group rate_ctrl_attr_group = {
	.attrs = rate_ctrl_attrs,
};

static const struct iio_info rate_ctrl_info = {
	.attrs = &rate_ctrl_attr_group,
};

static int rate_ctrl_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct rate_ctrl_dev *data;
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

	indio_dev->name = "mpfs_mc_ratelim";
	indio_dev->info = &rate_ctrl_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static const struct of_device_id mpfs_mc_ratelim_of_match[] = {
	{ .compatible = "microchip,rate-limiter-rtl-v4.2", },
	{ }
};
MODULE_DEVICE_TABLE(of, mpfs_mc_ratelim_of_match);

static struct platform_driver mpfs_mc_ratelim_driver = {
	.probe  = rate_ctrl_probe,
	.driver = {
		.name = "mpfs_mc_ratelim",
		.of_match_table = mpfs_mc_ratelim_of_match,
	},
};
module_platform_driver(mpfs_mc_ratelim_driver);

MODULE_AUTHOR("sunny bezawada <sunny.bezawada@microchip.com>");
MODULE_DESCRIPTION("MPFS Motor Control Rate Limiter and OLMNG IIO driver for speed ramp and direction control");
MODULE_LICENSE("GPL");
