// SPDX-License-Identifier: GPL-2.0
/*
 * Motor PI Controller IIO Driver
 *
 * This driver provides IIO interface for the motor PI (Proportional-Integral)
 * controller registers for speed and current loop tuning in BLDC and Stepper
 * motor control applications.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/of.h>
#include <linux/of_address.h>

/* Register Offsets - BLDC */
#define REG_SPEED_PI_KP_BLDC        0x00
#define REG_SPEED_PI_KI_BLDC        0x04
#define REG_IDQ_KP_BLDC             0x08
#define REG_IDQ_KI_BLDC             0x0C

/* Output registers - BLDC */
#define REG_SPEED_PI_BLDC           0x100
#define REG_SPEED_ID_PI_BLDC        0x104
#define REG_SPEED_IQ_PI_BLDC        0x108

/* Register Offsets - Stepper */
#define REG_SPEED_PI_KP_STEPPER     0x200
#define REG_SPEED_PI_KI_STEPPER     0x204
#define REG_IDQ_KP_STEPPER          0x208
#define REG_IDQ_KI_STEPPER          0x20C

/* Output registers - Stepper */
#define REG_SPEED_PI_STEPPER        0x300
#define REG_SPEED_ID_PI_STEPPER     0x304
#define REG_SPEED_IQ_PI_STEPPER     0x308

struct pi_ctrl_dev {
	void __iomem *base;
	struct device *dev;
};

/* Define read/write helpers */
#define IIO_REG_RW(_name, _reg)                                             \
static ssize_t _name##_show(struct device *dev,                             \
		struct device_attribute *attr, char *buf)                       \
{                                                                           \
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);                    \
	struct pi_ctrl_dev *data = iio_priv(indio_dev);                     \
	u32 val = readl(data->base + (_reg));                               \
	return sysfs_emit(buf, "%u\n", val);                                \
}                                                                           \
static ssize_t _name##_store(struct device *dev,                            \
		struct device_attribute *attr, const char *buf, size_t len) \
{                                                                           \
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);                    \
	struct pi_ctrl_dev *data = iio_priv(indio_dev);                     \
	u32 val;                                                            \
	if (kstrtou32(buf, 0, &val))                                        \
		return -EINVAL;                                             \
	writel(val, data->base + (_reg));                                   \
	return len;                                                         \
}                                                                           \
IIO_DEVICE_ATTR(_name, 0664, _name##_show, _name##_store, 0)

/* Define IIO attributes - BLDC */
IIO_REG_RW(speed_pi_kp_bldc,    REG_SPEED_PI_KP_BLDC);
IIO_REG_RW(speed_pi_ki_bldc,    REG_SPEED_PI_KI_BLDC);
IIO_REG_RW(idq_kp_bldc,         REG_IDQ_KP_BLDC);
IIO_REG_RW(idq_ki_bldc,         REG_IDQ_KI_BLDC);
IIO_REG_RW(speed_pi_bldc,       REG_SPEED_PI_BLDC);
IIO_REG_RW(speed_id_pi_bldc,    REG_SPEED_ID_PI_BLDC);
IIO_REG_RW(speed_iq_pi_bldc,    REG_SPEED_IQ_PI_BLDC);

/* Define IIO attributes - Stepper */
IIO_REG_RW(speed_pi_kp_stepper,    REG_SPEED_PI_KP_STEPPER);
IIO_REG_RW(speed_pi_ki_stepper,    REG_SPEED_PI_KI_STEPPER);
IIO_REG_RW(idq_kp_stepper,         REG_IDQ_KP_STEPPER);
IIO_REG_RW(idq_ki_stepper,         REG_IDQ_KI_STEPPER);
IIO_REG_RW(speed_pi_stepper,       REG_SPEED_PI_STEPPER);
IIO_REG_RW(speed_id_pi_stepper,    REG_SPEED_ID_PI_STEPPER);
IIO_REG_RW(speed_iq_pi_stepper,    REG_SPEED_IQ_PI_STEPPER);

static struct attribute *pi_ctrl_attrs[] = {
	/* BLDC */
	&iio_dev_attr_speed_pi_kp_bldc.dev_attr.attr,
	&iio_dev_attr_speed_pi_ki_bldc.dev_attr.attr,
	&iio_dev_attr_idq_kp_bldc.dev_attr.attr,
	&iio_dev_attr_idq_ki_bldc.dev_attr.attr,
	&iio_dev_attr_speed_pi_bldc.dev_attr.attr,
	&iio_dev_attr_speed_id_pi_bldc.dev_attr.attr,
	&iio_dev_attr_speed_iq_pi_bldc.dev_attr.attr,

	/* Stepper */
	&iio_dev_attr_speed_pi_kp_stepper.dev_attr.attr,
	&iio_dev_attr_speed_pi_ki_stepper.dev_attr.attr,
	&iio_dev_attr_idq_kp_stepper.dev_attr.attr,
	&iio_dev_attr_idq_ki_stepper.dev_attr.attr,
	&iio_dev_attr_speed_pi_stepper.dev_attr.attr,
	&iio_dev_attr_speed_id_pi_stepper.dev_attr.attr,
	&iio_dev_attr_speed_iq_pi_stepper.dev_attr.attr,

	NULL,
};

static const struct attribute_group pi_ctrl_attr_group = {
	.attrs = pi_ctrl_attrs,
};

static const struct iio_info pi_ctrl_info = {
	.attrs = &pi_ctrl_attr_group,
};

static int pi_ctrl_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct pi_ctrl_dev *data;
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

	indio_dev->name = "mpfs_mc_picon";
	indio_dev->info = &pi_ctrl_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static const struct of_device_id mpfs_mc_picon_of_match[] = {
	{ .compatible = "microchip,speed-id-iq-pi-rtl-v4.2", },
	{ }
};
MODULE_DEVICE_TABLE(of, mpfs_mc_picon_of_match);

static struct platform_driver mpfs_mc_picon_driver = {
	.probe  = pi_ctrl_probe,
	.driver = {
		.name = "mpfs_mc_picon",
		.of_match_table = mpfs_mc_picon_of_match,
	},
};
module_platform_driver(mpfs_mc_picon_driver);

MODULE_AUTHOR("sunny bezawada <sunny.bezawada@microchip.com>");
MODULE_DESCRIPTION("MPFS Motor Control Speed/Id/Iq PI controller IIO driver for speed and current loop tuning");
MODULE_LICENSE("GPL");
