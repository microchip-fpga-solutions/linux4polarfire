// SPDX-License-Identifier: GPL-2.0
/*
 * Motor Sequencer Manager (SQMNG) IIO Driver
 *
 * This driver provides IIO interface for motor sequencer control registers
 * including start/stop, fault clearing, and state machine configuration
 * for BLDC and Stepper motor control applications.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/of.h>
#include <linux/of_address.h>

/* Register Offsets - BLDC */
#define REG_START_MOTOR_BLDC        0x00
#define REG_SEQ_CTL_BLDC            0x04
#define REG_SQMNG_CL_OMEGA_BLDC     0x08
#define REG_STOP_MOTOR_BLDC         0x0C
#define REG_NUM_AUTO_RESTARTS_BLDC  0x10
#define REG_CLR_FAULT_BLDC          0x14
#define REG_SELECTOR_CHA_BLDC       0x18
#define REG_FSM_DEBUG_BLDC          0x1C

/* Output register - BLDC */
#define REG_SEQ_STATE_BLDC          0x100

/* Register Offsets - Stepper */
#define REG_START_MOTOR_STEPPER        0x200
#define REG_SEQ_CTL_STEPPER            0x204
#define REG_SQMNG_CL_OMEGA_STEPPER     0x208
#define REG_STOP_MOTOR_STEPPER         0x20C
#define REG_NUM_AUTO_RESTARTS_STEPPER  0x210
#define REG_CLR_FAULT_STEPPER          0x214
#define REG_SELECTOR_CHA_STEPPER       0x218
#define REG_FSM_DEBUG_STEPPER          0x21C

/* Output register - Stepper */
#define REG_SEQ_STATE_STEPPER          0x300

struct motor_sqmng_data {
	void __iomem *base;
	struct device *dev;
};

/* Define read/write helpers */
#define IIO_REG_RW(_name, _reg)                                             \
static ssize_t _name##_show(struct device *dev,                             \
		struct device_attribute *attr, char *buf)                       \
{                                                                           \
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);                    \
	struct motor_sqmng_data *data = iio_priv(indio_dev);                \
	u32 val = readl(data->base + (_reg));                               \
	return sysfs_emit(buf, "%u\n", val);                                \
}                                                                           \
static ssize_t _name##_store(struct device *dev,                            \
		struct device_attribute *attr, const char *buf, size_t len) \
{                                                                           \
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);                    \
	struct motor_sqmng_data *data = iio_priv(indio_dev);                \
	u32 val;                                                            \
	if (kstrtou32(buf, 0, &val))                                        \
		return -EINVAL;                                             \
	writel(val, data->base + (_reg));                                   \
	return len;                                                         \
}                                                                           \
IIO_DEVICE_ATTR(_name, 0664, _name##_show, _name##_store, 0)

/* Generate attributes - BLDC */
IIO_REG_RW(start_motor_bldc,       REG_START_MOTOR_BLDC);
IIO_REG_RW(seq_ctl_bldc,           REG_SEQ_CTL_BLDC);
IIO_REG_RW(sqmng_cl_omega_bldc,    REG_SQMNG_CL_OMEGA_BLDC);
IIO_REG_RW(stop_motor_bldc,        REG_STOP_MOTOR_BLDC);
IIO_REG_RW(num_auto_restarts_bldc, REG_NUM_AUTO_RESTARTS_BLDC);
IIO_REG_RW(clr_fault_bldc,         REG_CLR_FAULT_BLDC);
IIO_REG_RW(selector_cha_bldc,      REG_SELECTOR_CHA_BLDC);
IIO_REG_RW(fsm_debug_bldc,         REG_FSM_DEBUG_BLDC);
IIO_REG_RW(seq_state_bldc,         REG_SEQ_STATE_BLDC);

/* Generate attributes - Stepper */
IIO_REG_RW(start_motor_stepper,       REG_START_MOTOR_STEPPER);
IIO_REG_RW(seq_ctl_stepper,           REG_SEQ_CTL_STEPPER);
IIO_REG_RW(sqmng_cl_omega_stepper,    REG_SQMNG_CL_OMEGA_STEPPER);
IIO_REG_RW(stop_motor_stepper,        REG_STOP_MOTOR_STEPPER);
IIO_REG_RW(num_auto_restarts_stepper, REG_NUM_AUTO_RESTARTS_STEPPER);
IIO_REG_RW(clr_fault_stepper,         REG_CLR_FAULT_STEPPER);
IIO_REG_RW(selector_cha_stepper,      REG_SELECTOR_CHA_STEPPER);
IIO_REG_RW(fsm_debug_stepper,         REG_FSM_DEBUG_STEPPER);
IIO_REG_RW(seq_state_stepper,         REG_SEQ_STATE_STEPPER);

static struct attribute *motor_ctrl_attrs[] = {
	/* BLDC attributes */
	&iio_dev_attr_start_motor_bldc.dev_attr.attr,
	&iio_dev_attr_seq_ctl_bldc.dev_attr.attr,
	&iio_dev_attr_sqmng_cl_omega_bldc.dev_attr.attr,
	&iio_dev_attr_stop_motor_bldc.dev_attr.attr,
	&iio_dev_attr_num_auto_restarts_bldc.dev_attr.attr,
	&iio_dev_attr_clr_fault_bldc.dev_attr.attr,
	&iio_dev_attr_selector_cha_bldc.dev_attr.attr,
	&iio_dev_attr_fsm_debug_bldc.dev_attr.attr,
	&iio_dev_attr_seq_state_bldc.dev_attr.attr,

	/* Stepper attributes */
	&iio_dev_attr_start_motor_stepper.dev_attr.attr,
	&iio_dev_attr_seq_ctl_stepper.dev_attr.attr,
	&iio_dev_attr_sqmng_cl_omega_stepper.dev_attr.attr,
	&iio_dev_attr_stop_motor_stepper.dev_attr.attr,
	&iio_dev_attr_num_auto_restarts_stepper.dev_attr.attr,
	&iio_dev_attr_clr_fault_stepper.dev_attr.attr,
	&iio_dev_attr_selector_cha_stepper.dev_attr.attr,
	&iio_dev_attr_fsm_debug_stepper.dev_attr.attr,
	&iio_dev_attr_seq_state_stepper.dev_attr.attr,

	NULL,
};

static const struct attribute_group motor_ctrl_attr_group = {
	.attrs = motor_ctrl_attrs,
};

static const struct iio_info motor_ctrl_info = {
	.attrs = &motor_ctrl_attr_group,
};

static int motor_sqmng_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct motor_sqmng_data *data;
	struct resource *res;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "probe failed: invalid resource\n");
		return -ENODEV;
	}

	data->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	indio_dev->name = "mpfs_mc_sqmng";
	indio_dev->info = &motor_ctrl_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static const struct of_device_id mpfs_mc_sqmng_of_match[] = {
	{ .compatible = "microchip,seq-controller-rtl-v4.2", },
	{ }
};
MODULE_DEVICE_TABLE(of, mpfs_mc_sqmng_of_match);

static struct platform_driver mpfs_mc_sqmng_driver = {
	.driver = {
		.name = "mpfs_mc_sqmng",
		.of_match_table = mpfs_mc_sqmng_of_match,
	},
	.probe = motor_sqmng_probe,
};
module_platform_driver(mpfs_mc_sqmng_driver);

MODULE_AUTHOR("sunny bezawada <sunny.bezawada@microchip.com>");
MODULE_DESCRIPTION("MPFS Motor Control Sequence Controller IIO driver for start/stop and fault control");
MODULE_LICENSE("GPL");
