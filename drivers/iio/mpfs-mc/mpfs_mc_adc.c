// SPDX-License-Identifier: GPL-2.0
/*
 * Motor ADC IIO Driver
 *
 * This driver provides IIO interface for motor ADC with current sensing,
 * overcurrent threshold configuration, and buffered data acquisition
 * for BLDC and Stepper motor control applications.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/sched/task.h>
#include <linux/kthread.h>
#include <linux/iio/buffer.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/delay.h>

#define UPDATE_INTERVAL_US 100

/* ADC conversion constants */
#define ADC_VREF_MV         1800
#define ADC_MAX_VALUE       16383
#define ADC_SPEED_XOR_MASK  16362

/* Register Offsets - BLDC */
#define REG_ADC_ADDR_BLDC       0x00
#define REG_CH0_VAL_BLDC        0x04
#define REG_CH1_VAL_BLDC        0x08
#define REG_CH2_VAL_BLDC        0x0C
#define REG_CH3_VAL_BLDC        0x10
#define REG_ADC_SCALE_BLDC      0x14
#define REG_OC_THRE_BLDC        0x18

/* Output registers - BLDC */
#define REG_ADC_IB_BLDC         0x100
#define REG_ADC_IA_BLDC         0x104
#define REG_SPEED_BLDC          0x108
#define REG_IQ_BLDC             0x10C

/* Register Offsets - Stepper */
#define REG_ADC_ADDR_STEPPER    0x200
#define REG_CH0_VAL_STEPPER     0x204
#define REG_CH1_VAL_STEPPER     0x208
#define REG_CH2_VAL_STEPPER     0x20C
#define REG_CH3_VAL_STEPPER     0x210
#define REG_ADC_SCALE_STEPPER   0x214
#define REG_OC_THRE_STEPPER     0x218

/* Output registers - Stepper */
#define REG_ADC_IB_STEPPER      0x300
#define REG_ADC_IA_STEPPER      0x304

struct adc_dev {
	void __iomem *base;
	struct device *dev;
	/* Protects register access */
	struct mutex lock;
	struct iio_buffer *buffer;
	struct task_struct *kthread;
	struct iio_dev *indio_dev;
	atomic_t kthread_run;
};

/* Define read/write helpers */
#define IIO_REG_RW(_name, _reg)                                             \
static ssize_t _name##_show(struct device *dev,                             \
		struct device_attribute *attr, char *buf)                       \
{                                                                           \
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);                    \
	struct adc_dev *data = iio_priv(indio_dev);                         \
	u32 val = readl(data->base + (_reg));                               \
	return sysfs_emit(buf, "%u\n", val);                                \
}                                                                           \
static ssize_t _name##_store(struct device *dev,                            \
		struct device_attribute *attr, const char *buf, size_t len) \
{                                                                           \
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);                    \
	struct adc_dev *data = iio_priv(indio_dev);                         \
	u32 val;                                                            \
	if (kstrtou32(buf, 0, &val))                                        \
		return -EINVAL;                                             \
	writel(val, data->base + (_reg));                                   \
	return len;                                                         \
}                                                                           \
IIO_DEVICE_ATTR(_name, 0664, _name##_show, _name##_store, 0)

/* Define IIO attributes - BLDC */
IIO_REG_RW(adc_addr_bldc,    REG_ADC_ADDR_BLDC);
IIO_REG_RW(ch0_val_bldc,     REG_CH0_VAL_BLDC);
IIO_REG_RW(ch1_val_bldc,     REG_CH1_VAL_BLDC);
IIO_REG_RW(ch2_val_bldc,     REG_CH2_VAL_BLDC);
IIO_REG_RW(ch3_val_bldc,     REG_CH3_VAL_BLDC);
IIO_REG_RW(adc_scale_bldc,   REG_ADC_SCALE_BLDC);
IIO_REG_RW(oc_thre_bldc,     REG_OC_THRE_BLDC);
IIO_REG_RW(adc_ib_bldc,      REG_ADC_IB_BLDC);
IIO_REG_RW(adc_ia_bldc,      REG_ADC_IA_BLDC);
IIO_REG_RW(speed_bldc,       REG_SPEED_BLDC);
IIO_REG_RW(iq_bldc,          REG_IQ_BLDC);

/* Define IIO attributes - Stepper */
IIO_REG_RW(adc_addr_stepper,    REG_ADC_ADDR_STEPPER);
IIO_REG_RW(ch0_val_stepper,     REG_CH0_VAL_STEPPER);
IIO_REG_RW(ch1_val_stepper,     REG_CH1_VAL_STEPPER);
IIO_REG_RW(ch2_val_stepper,     REG_CH2_VAL_STEPPER);
IIO_REG_RW(ch3_val_stepper,     REG_CH3_VAL_STEPPER);
IIO_REG_RW(adc_scale_stepper,   REG_ADC_SCALE_STEPPER);
IIO_REG_RW(oc_thre_stepper,     REG_OC_THRE_STEPPER);
IIO_REG_RW(adc_ib_stepper,      REG_ADC_IB_STEPPER);
IIO_REG_RW(adc_ia_stepper,      REG_ADC_IA_STEPPER);

static struct attribute *adc_attrs[] = {
	/* BLDC */
	&iio_dev_attr_adc_addr_bldc.dev_attr.attr,
	&iio_dev_attr_ch0_val_bldc.dev_attr.attr,
	&iio_dev_attr_ch1_val_bldc.dev_attr.attr,
	&iio_dev_attr_ch2_val_bldc.dev_attr.attr,
	&iio_dev_attr_ch3_val_bldc.dev_attr.attr,
	&iio_dev_attr_adc_scale_bldc.dev_attr.attr,
	&iio_dev_attr_oc_thre_bldc.dev_attr.attr,
	&iio_dev_attr_adc_ib_bldc.dev_attr.attr,
	&iio_dev_attr_adc_ia_bldc.dev_attr.attr,
	&iio_dev_attr_speed_bldc.dev_attr.attr,
	&iio_dev_attr_iq_bldc.dev_attr.attr,

	/* Stepper */
	&iio_dev_attr_adc_addr_stepper.dev_attr.attr,
	&iio_dev_attr_ch0_val_stepper.dev_attr.attr,
	&iio_dev_attr_ch1_val_stepper.dev_attr.attr,
	&iio_dev_attr_ch2_val_stepper.dev_attr.attr,
	&iio_dev_attr_ch3_val_stepper.dev_attr.attr,
	&iio_dev_attr_adc_scale_stepper.dev_attr.attr,
	&iio_dev_attr_oc_thre_stepper.dev_attr.attr,
	&iio_dev_attr_adc_ib_stepper.dev_attr.attr,
	&iio_dev_attr_adc_ia_stepper.dev_attr.attr,

	NULL,
};

static int adc_read_attribute(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan, int *val,
			      int *val2, long mask)
{
	struct adc_dev *st = iio_priv(indio_dev);
	u32 temp;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		mutex_lock(&st->lock);
		switch (chan->channel) {
		case 0:
			/* Speed channel */
			temp = readl(st->base + REG_SPEED_BLDC);
			if ((temp & 0x30000) != 0x30000)
				*val = temp >> 4;
			else
				*val = (temp >> 4) ^ ADC_SPEED_XOR_MASK;
			break;
		case 2:
			/* Current IB channel - convert to mV */
			temp = (s16)((readl(st->base + REG_ADC_IB_BLDC)) >> 2);
			*val = (temp * ADC_VREF_MV) / ADC_MAX_VALUE;
			break;
		case 1:
			/* IQ channel */
			temp = (s16)((readl(st->base + REG_IQ_BLDC)) >> 2);
			*val = temp;
			break;
		default:
			mutex_unlock(&st->lock);
			return -EINVAL;
		}
		mutex_unlock(&st->lock);
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = 1;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static const struct iio_chan_spec current_channels[] = {
	{
		.type = IIO_CURRENT,
		.indexed = 1,
		.channel = 0,
		.address = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.shift = 0,
			.endianness = IIO_CPU,
		},
	},
	{
		.type = IIO_CURRENT,
		.indexed = 1,
		.channel = 1,
		.address = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_index = 1,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.shift = 0,
			.endianness = IIO_CPU,
		},
	},
	{
		.type = IIO_CURRENT,
		.indexed = 1,
		.channel = 2,
		.address = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
		.scan_index = 2,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.shift = 0,
			.endianness = IIO_CPU,
		},
	},
	IIO_CHAN_SOFT_TIMESTAMP(3),
};

static int adc_kthread_fn(void *arg)
{
	struct adc_dev *data = arg;
	struct iio_dev *indio_dev = data->indio_dev;
	/* Properly aligned buffer for IIO with timestamp support */
	struct {
		s16 channels[3];
		s64 timestamp __aligned(8);
	} sample;
	u32 temp;
	int ret;
	unsigned int chan;

	while (!kthread_should_stop() && atomic_read(&data->kthread_run)) {
		memset(&sample, 0, sizeof(sample));

		for_each_set_bit(chan, indio_dev->active_scan_mask,
				 indio_dev->masklength) {
			if (chan == 0) {
				/* Speed channel */
				temp = readl(data->base + REG_SPEED_BLDC);
				if ((temp & 0x30000) != 0x30000)
					sample.channels[0] = temp >> 4;
				else
					sample.channels[0] = (temp >> 4) ^ ADC_SPEED_XOR_MASK;
			} else if (chan == 2) {
				/* Current IB channel - convert to mV */
				temp = (s16)((readl(data->base + REG_ADC_IB_BLDC)) >> 2);
				sample.channels[2] = (temp * ADC_VREF_MV) / ADC_MAX_VALUE;
			} else if (chan == 1) {
				/* IQ channel */
				sample.channels[1] = (s16)((readl(data->base + REG_IQ_BLDC)) >> 2);
			} else {
				dev_err_ratelimited(indio_dev->dev.parent,
						    "Invalid channel %u in adc_kthread\n",
						    chan);
			}
		}

		/* Push sample to IIO buffer with timestamp */
		ret = iio_push_to_buffers_with_timestamp(indio_dev, &sample,
							 iio_get_time_ns(indio_dev));
		if (ret)
			dev_err_ratelimited(indio_dev->dev.parent,
					    "Failed to push sample %d\n", ret);

		usleep_range(UPDATE_INTERVAL_US, UPDATE_INTERVAL_US + 20);
	}
	return 0;
}

static int adc_buffer_preenable(struct iio_dev *indio_dev)
{
	struct adc_dev *data = iio_priv(indio_dev);
	int ret;

	/* Start the kthread when buffer is enabled */
	atomic_set(&data->kthread_run, 1);
	data->kthread = kthread_run(adc_kthread_fn, data, "adc_kthread");
	if (IS_ERR(data->kthread)) {
		ret = PTR_ERR(data->kthread);
		dev_err(indio_dev->dev.parent, "Failed to start kthread: %d\n", ret);
		data->kthread = NULL;
		atomic_set(&data->kthread_run, 0);
		return ret;
	}
	return 0;
}

static int adc_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct adc_dev *data = iio_priv(indio_dev);

	/* Signal kthread to stop, then wait for it */
	atomic_set(&data->kthread_run, 0);
	if (data->kthread) {
		kthread_stop(data->kthread);
		data->kthread = NULL;
	}
	return 0;
}

static const struct iio_buffer_setup_ops adc_buffer_ops = {
	.preenable = adc_buffer_preenable,
	.postdisable = adc_buffer_postdisable,
};

static const struct attribute_group adc_attr_group = {
	.attrs = adc_attrs,
};

static const struct iio_info adc_info = {
	.read_raw = &adc_read_attribute,
	.attrs = &adc_attr_group,
};

static int adc_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	struct adc_dev *data;
	struct resource *res;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	data->dev = &pdev->dev;
	data->indio_dev = indio_dev;
	mutex_init(&data->lock);
	atomic_set(&data->kthread_run, 0);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	indio_dev->name = "mpfs_mc_adc";
	indio_dev->info = &adc_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	indio_dev->channels = current_channels;
	indio_dev->num_channels = ARRAY_SIZE(current_channels);

	ret = devm_iio_kfifo_buffer_setup(&pdev->dev, indio_dev, &adc_buffer_ops);
	if (ret)
		return ret;

	return devm_iio_device_register(&pdev->dev, indio_dev);
}

static const struct of_device_id mpfs_mc_adc_of_match[] = {
	{ .compatible = "microchip,adc-scaling-rtl-v4.3", },
	{ }
};
MODULE_DEVICE_TABLE(of, mpfs_mc_adc_of_match);

static struct platform_driver mpfs_mc_adc_driver = {
	.probe  = adc_probe,
	.driver = {
		.name = "mpfs_mc_adc",
		.of_match_table = mpfs_mc_adc_of_match,
	},
};
module_platform_driver(mpfs_mc_adc_driver);

MODULE_AUTHOR("sunny bezawada <sunny.bezawada@microchip.com>");
MODULE_DESCRIPTION("MPFS Motor Control ADC Scaling IIO driver for current sensing and speed feedback");
MODULE_LICENSE("GPL");
