// SPDX-License-Identifier: GPL-2.0-only
/**
 * Isolated meter time-control platform driver.
 *
 * The device has no DMA channel. It brackets the PL latch in kernel context
 * and commits UTC interval tuples to one dedicated AXI-Lite register bank.
 */

#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/time64.h>
#include <linux/uaccess.h>

#include "meter_time_uapi.h"

#define METER_TIME_IDENTIFIER 0x3143544dU /* MTC1 */
#define METER_TIME_IDENTIFIER_REG 0x04U
#define METER_TIME_CONTROL 0x08U
#define METER_TIME_CONTROL_LATCH (1U << 1)
#define METER_TIME_STATUS 0x0cU
#define METER_TIME_STATUS_SAMPLE_SEEN (1U << 0)
#define METER_TIME_LATCHED_TICK_LO 0x10U
#define METER_TIME_LATCHED_SAMPLE_LO 0x18U
#define METER_TIME_TEN_MINUTE_TARGET_LO 0x40U
#define METER_TIME_TEN_MINUTE_CONTROL 0x48U
#define METER_TIME_TEN_MINUTE_VALID (1U << 0)
#define METER_TIME_TEN_MINUTE_COMMIT (1U << 1)
#define METER_TIME_TEN_MINUTE_UPDATE (1U << 8)
#define METER_TIME_FREQUENCY_START_SAMPLE_LO 0x50U
#define METER_TIME_FREQUENCY_END_SAMPLE_LO 0x58U
#define METER_TIME_FREQUENCY_UTC_START_LO 0x60U
#define METER_TIME_FREQUENCY_UTC_END_LO 0x68U
#define METER_TIME_FREQUENCY_UNCERTAINTY_LO 0x70U
#define METER_TIME_FREQUENCY_RATE_MILLIHZ 0x78U
#define METER_TIME_FREQUENCY_GENERATION 0x7cU
#define METER_TIME_FREQUENCY_PROFILE 0x80U
#define METER_TIME_FREQUENCY_CONTROL 0x84U
#define METER_TIME_FREQUENCY_OBSERVER_STATUS 0x88U
#define METER_TIME_FREQUENCY_COMPLETED 0x8cU
#define METER_TIME_FREQUENCY_DROPPED 0x90U
#define METER_TIME_FREQUENCY_OVERFLOW 0x94U
#define METER_TIME_FREQUENCY_DISCONTINUITY 0x98U
#define METER_TIME_FREQUENCY_COMMIT (1U << 2)
#define METER_TIME_FREQUENCY_CANCEL (1U << 3)
#define METER_TIME_FREQUENCY_UPDATE (1U << 8)
#define METER_TIME_FREQUENCY_FLAGS \
	(METER_TIME_FREQUENCY_10S_BOUNDARY_VALID | \
	 METER_TIME_FREQUENCY_10S_SYNCHRONIZED)

struct meter_time_device {
	void __iomem *registers;
	struct miscdevice misc;
	struct mutex operation_lock;
	atomic_t opened;
};

static u64 meter_time_read_u64(void __iomem *registers, unsigned int offset)
{
	u32 high_before;
	u32 high_after;
	u32 low;

	do {
		high_before = readl(registers + offset + 4U);
		low = readl(registers + offset);
		high_after = readl(registers + offset + 4U);
	} while (high_before != high_after);
	return ((u64)high_after << 32) | low;
}

static void meter_time_write_u64(void __iomem *registers,
				 unsigned int offset, u64 value)
{
	writel(lower_32_bits(value), registers + offset);
	writel(upper_32_bits(value), registers + offset + 4U);
}

static long meter_time_correlate(struct meter_time_device *device,
				 unsigned long argument)
{
	struct meter_time_correlation correlation;
	struct timespec64 tai_before;
	struct timespec64 tai_after;
	struct timespec64 realtime_before;
	struct timespec64 realtime_after;

	if (!(readl(device->registers + METER_TIME_STATUS) &
	      METER_TIME_STATUS_SAMPLE_SEEN))
		return -EAGAIN;

	ktime_get_clocktai_ts64(&tai_before);
	ktime_get_real_ts64(&realtime_before);
	writel(METER_TIME_CONTROL_LATCH,
	       device->registers + METER_TIME_CONTROL);
	(void)readl(device->registers + METER_TIME_CONTROL);
	correlation.pl_tick = meter_time_read_u64(device->registers,
		METER_TIME_LATCHED_TICK_LO);
	correlation.sample_index = meter_time_read_u64(device->registers,
		METER_TIME_LATCHED_SAMPLE_LO);
	ktime_get_real_ts64(&realtime_after);
	ktime_get_clocktai_ts64(&tai_after);
	correlation.tai_before_nanoseconds = timespec64_to_ns(&tai_before);
	correlation.tai_after_nanoseconds = timespec64_to_ns(&tai_after);
	correlation.realtime_before_nanoseconds = timespec64_to_ns(&realtime_before);
	correlation.realtime_after_nanoseconds = timespec64_to_ns(&realtime_after);

	if (copy_to_user((void __user *)argument, &correlation,
			 sizeof(correlation)))
		return -EFAULT;
	return 0;
}

static long meter_time_set_ten_minute(struct meter_time_device *device,
				      unsigned long argument)
{
	struct meter_time_ten_minute_boundary boundary;
	u32 control;
	u32 previous_control;

	if (copy_from_user(&boundary, (void __user *)argument, sizeof(boundary)))
		return -EFAULT;
	if (boundary.reserved != 0U || boundary.valid > 1U)
		return -EINVAL;

	previous_control = readl(device->registers +
		METER_TIME_TEN_MINUTE_CONTROL);
	meter_time_write_u64(device->registers,
		METER_TIME_TEN_MINUTE_TARGET_LO, boundary.target_sample_index);
	control = boundary.valid ? METER_TIME_TEN_MINUTE_VALID : 0U;
	writel(control | METER_TIME_TEN_MINUTE_COMMIT,
	       device->registers + METER_TIME_TEN_MINUTE_CONTROL);
	control = readl(device->registers + METER_TIME_TEN_MINUTE_CONTROL);
	if (meter_time_read_u64(device->registers,
			METER_TIME_TEN_MINUTE_TARGET_LO) !=
			boundary.target_sample_index ||
	    !!(control & METER_TIME_TEN_MINUTE_VALID) != !!boundary.valid ||
	    !!(control & METER_TIME_TEN_MINUTE_UPDATE) ==
		!!(previous_control & METER_TIME_TEN_MINUTE_UPDATE))
		return -EIO;
	return 0;
}

static bool meter_time_frequency_matches(
	struct meter_time_device *device,
	const struct meter_time_frequency_10s_boundary *boundary,
	u32 control, u32 previous_control)
{
	void __iomem *registers = device->registers;

	return meter_time_read_u64(registers,
			METER_TIME_FREQUENCY_START_SAMPLE_LO) ==
			boundary->start_sample_index &&
		meter_time_read_u64(registers,
			METER_TIME_FREQUENCY_END_SAMPLE_LO) ==
			boundary->end_sample_index &&
		meter_time_read_u64(registers,
			METER_TIME_FREQUENCY_UTC_START_LO) ==
			boundary->utc_start_nanoseconds &&
		meter_time_read_u64(registers,
			METER_TIME_FREQUENCY_UTC_END_LO) ==
			boundary->utc_end_nanoseconds &&
		meter_time_read_u64(registers,
			METER_TIME_FREQUENCY_UNCERTAINTY_LO) ==
			boundary->utc_uncertainty_nanoseconds &&
		readl(registers + METER_TIME_FREQUENCY_RATE_MILLIHZ) ==
			boundary->measured_sample_rate_millihz &&
		readl(registers + METER_TIME_FREQUENCY_GENERATION) ==
			boundary->boundary_generation &&
		readl(registers + METER_TIME_FREQUENCY_PROFILE) ==
			boundary->profile &&
		(control & METER_TIME_FREQUENCY_FLAGS) == boundary->flags &&
		!!(control & METER_TIME_FREQUENCY_UPDATE) !=
			!!(previous_control & METER_TIME_FREQUENCY_UPDATE);
}

static long meter_time_set_frequency(struct meter_time_device *device,
				     unsigned long argument)
{
	struct meter_time_frequency_10s_boundary boundary;
	u32 control;
	u32 previous_control;
	void __iomem *registers = device->registers;

	if (copy_from_user(&boundary, (void __user *)argument, sizeof(boundary)))
		return -EFAULT;
	if (boundary.reserved != 0U ||
	    (boundary.flags & ~METER_TIME_FREQUENCY_FLAGS) != 0U)
		return -EINVAL;

	previous_control = readl(registers + METER_TIME_FREQUENCY_CONTROL);
	meter_time_write_u64(registers, METER_TIME_FREQUENCY_START_SAMPLE_LO,
		boundary.start_sample_index);
	meter_time_write_u64(registers, METER_TIME_FREQUENCY_END_SAMPLE_LO,
		boundary.end_sample_index);
	meter_time_write_u64(registers, METER_TIME_FREQUENCY_UTC_START_LO,
		boundary.utc_start_nanoseconds);
	meter_time_write_u64(registers, METER_TIME_FREQUENCY_UTC_END_LO,
		boundary.utc_end_nanoseconds);
	meter_time_write_u64(registers, METER_TIME_FREQUENCY_UNCERTAINTY_LO,
		boundary.utc_uncertainty_nanoseconds);
	writel(boundary.measured_sample_rate_millihz,
	       registers + METER_TIME_FREQUENCY_RATE_MILLIHZ);
	writel(boundary.boundary_generation,
	       registers + METER_TIME_FREQUENCY_GENERATION);
	writel(boundary.profile, registers + METER_TIME_FREQUENCY_PROFILE);
	writel(boundary.flags | METER_TIME_FREQUENCY_COMMIT,
	       registers + METER_TIME_FREQUENCY_CONTROL);
	control = readl(registers + METER_TIME_FREQUENCY_CONTROL);
	if (!meter_time_frequency_matches(device, &boundary, control,
					 previous_control))
		return -EIO;

	boundary.observer_status = readl(registers +
		METER_TIME_FREQUENCY_OBSERVER_STATUS);
	boundary.completed_count = readl(registers +
		METER_TIME_FREQUENCY_COMPLETED);
	boundary.dropped_count = readl(registers +
		METER_TIME_FREQUENCY_DROPPED);
	boundary.overflow_count = readl(registers +
		METER_TIME_FREQUENCY_OVERFLOW);
	boundary.discontinuity_count = readl(registers +
		METER_TIME_FREQUENCY_DISCONTINUITY);
	if (copy_to_user((void __user *)argument, &boundary, sizeof(boundary)))
		return -EFAULT;
	return 0;
}

static long meter_time_cancel_frequency(struct meter_time_device *device)
{
	u32 previous_control = readl(device->registers +
		METER_TIME_FREQUENCY_CONTROL);
	u32 control;

	writel(METER_TIME_FREQUENCY_CANCEL | METER_TIME_FREQUENCY_COMMIT,
	       device->registers + METER_TIME_FREQUENCY_CONTROL);
	control = readl(device->registers + METER_TIME_FREQUENCY_CONTROL);
	if (!(control & METER_TIME_FREQUENCY_CANCEL) ||
	    !!(control & METER_TIME_FREQUENCY_UPDATE) ==
		!!(previous_control & METER_TIME_FREQUENCY_UPDATE))
		return -EIO;
	return 0;
}

static long meter_time_ioctl(struct file *file, unsigned int command,
			     unsigned long argument)
{
	struct meter_time_device *device = file->private_data;
	long result;

	mutex_lock(&device->operation_lock);
	switch (command) {
	case METER_TIME_IOC_CORRELATE:
		result = meter_time_correlate(device, argument);
		break;
	case METER_TIME_IOC_SET_TEN_MINUTE_BOUNDARY:
		result = meter_time_set_ten_minute(device, argument);
		break;
	case METER_TIME_IOC_SET_FREQUENCY_10S_BOUNDARY:
		result = meter_time_set_frequency(device, argument);
		break;
	case METER_TIME_IOC_CANCEL_FREQUENCY_10S:
		result = meter_time_cancel_frequency(device);
		break;
	default:
		result = -ENOTTY;
		break;
	}
	mutex_unlock(&device->operation_lock);
	return result;
}

static int meter_time_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct meter_time_device *device = container_of(misc,
		struct meter_time_device, misc);

	if (atomic_cmpxchg(&device->opened, 0, 1) != 0)
		return -EBUSY;
	file->private_data = device;
	return 0;
}

static int meter_time_release(struct inode *inode, struct file *file)
{
	struct meter_time_device *device = file->private_data;

	atomic_set(&device->opened, 0);
	return 0;
}

static const struct file_operations meter_time_file_operations = {
	.owner = THIS_MODULE,
	.open = meter_time_open,
	.release = meter_time_release,
	.unlocked_ioctl = meter_time_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = meter_time_ioctl,
#endif
};

static int meter_time_probe(struct platform_device *platform)
{
	struct meter_time_device *device;
	int result;

	device = devm_kzalloc(&platform->dev, sizeof(*device), GFP_KERNEL);
	if (!device)
		return -ENOMEM;
	device->registers = devm_platform_ioremap_resource(platform, 0);
	if (IS_ERR(device->registers))
		return PTR_ERR(device->registers);
	if (readl(device->registers + METER_TIME_IDENTIFIER_REG) !=
	    METER_TIME_IDENTIFIER)
		return dev_err_probe(&platform->dev, -ENODEV,
			"unexpected meter-time register identifier\n");

	mutex_init(&device->operation_lock);
	atomic_set(&device->opened, 0);
	device->misc.minor = MISC_DYNAMIC_MINOR;
	device->misc.name = "meter-time";
	device->misc.fops = &meter_time_file_operations;
	device->misc.parent = &platform->dev;
	device->misc.mode = 0660;
	result = misc_register(&device->misc);
	if (result)
		return dev_err_probe(&platform->dev, result,
			"failed to register /dev/meter-time\n");
	platform_set_drvdata(platform, device);
	dev_info(&platform->dev, "registered /dev/meter-time\n");
	return 0;
}

static void meter_time_remove(struct platform_device *platform)
{
	struct meter_time_device *device = platform_get_drvdata(platform);

	misc_deregister(&device->misc);
}

static const struct of_device_id meter_time_of_match[] = {
	{ .compatible = "monutchee,meter-time-control" },
	{ }
};
MODULE_DEVICE_TABLE(of, meter_time_of_match);

static struct platform_driver meter_time_driver = {
	.probe = meter_time_probe,
	.remove = meter_time_remove,
	.driver = {
		.name = "meter-time",
		.of_match_table = meter_time_of_match,
	},
};
module_platform_driver(meter_time_driver);

MODULE_AUTHOR("Monutchee");
MODULE_DESCRIPTION("Isolated meter time-control driver");
MODULE_LICENSE("GPL");
