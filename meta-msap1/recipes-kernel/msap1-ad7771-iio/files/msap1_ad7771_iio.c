// SPDX-License-Identifier: GPL-2.0-only
/*
 * Buffered IIO consumer for the MSAP1 AD7771 PL capture stream.
 *
 * R5 core 0 owns AD7771 SPI and PL capture control. This driver owns only the
 * Linux DMAengine channel named "rx" and describes the fixed frame layout.
 */

#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <linux/iio/buffer-dmaengine.h>
#include <linux/iio/iio.h>

#define MSAP1_AD7771_CHANNEL(_index) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.channel = (_index), \
	.scan_index = (_index), \
	.scan_type = { \
		.sign = 's', \
		.realbits = 24, \
		.storagebits = 32, \
		.shift = 0, \
		.endianness = IIO_LE, \
	}, \
}

static const struct iio_chan_spec msap1_ad7771_channels[] = {
	MSAP1_AD7771_CHANNEL(0),
	MSAP1_AD7771_CHANNEL(1),
	MSAP1_AD7771_CHANNEL(2),
	MSAP1_AD7771_CHANNEL(3),
	MSAP1_AD7771_CHANNEL(4),
	MSAP1_AD7771_CHANNEL(5),
	MSAP1_AD7771_CHANNEL(6),
	MSAP1_AD7771_CHANNEL(7),
};

static const unsigned long msap1_ad7771_scan_masks[] = {
	GENMASK(7, 0),
	0,
};

static const struct iio_info msap1_ad7771_info = {
};

static int msap1_ad7771_probe(struct platform_device *pdev)
{
	struct iio_dev *indio_dev;
	int ret;

	indio_dev = devm_iio_device_alloc(&pdev->dev, 0);
	if (!indio_dev)
		return -ENOMEM;

	indio_dev->name = "msap1-ad7771";
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->info = &msap1_ad7771_info;
	indio_dev->modes = INDIO_BUFFER_HARDWARE;
	indio_dev->channels = msap1_ad7771_channels;
	indio_dev->num_channels = ARRAY_SIZE(msap1_ad7771_channels);
	indio_dev->available_scan_masks = msap1_ad7771_scan_masks;

	ret = devm_iio_dmaengine_buffer_setup(&pdev->dev, indio_dev, "rx");
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to configure RX DMA buffer\n");

	ret = devm_iio_device_register(&pdev->dev, indio_dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register IIO device\n");

	platform_set_drvdata(pdev, indio_dev);
	dev_info(&pdev->dev,
		 "registered eight signed 24-bit channels in 32-bit storage\n");
	return 0;
}

static const struct of_device_id msap1_ad7771_of_match[] = {
	{ .compatible = "monutchee,msap1-ad7771-iio" },
	{ }
};
MODULE_DEVICE_TABLE(of, msap1_ad7771_of_match);

static struct platform_driver msap1_ad7771_driver = {
	.probe = msap1_ad7771_probe,
	.driver = {
		.name = "msap1-ad7771-iio",
		.of_match_table = msap1_ad7771_of_match,
	},
};
module_platform_driver(msap1_ad7771_driver);

MODULE_AUTHOR("Monutchee");
MODULE_DESCRIPTION("MSAP1 AD7771 AXI DMA IIO consumer");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_DMAENGINE_BUFFER");
