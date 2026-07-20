// SPDX-License-Identifier: GPL-2.0-only
/* MSAP1 fixed-record AXI DMA S2MM consumer. */

#include <linux/atomic.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define MSAP1_METER_RECORD_SIZE 256U
/*
 * The Xilinx AXI DMAengine driver does not expose a cyclic transaction to its
 * client until the final hardware descriptor in that transaction has
 * completed.  A 64-period ring therefore made the first callback arrive only
 * after 64 meter records and left userspace one complete ring revolution
 * behind the PL (12.8 seconds at the 200 ms result cadence).
 *
 * Two periods provide the required ping-pong ownership without accumulating a
 * user-visible history in the DMA transport.  With the Xilinx callback phase,
 * readers see the preceding completed period while the DMA owns the other
 * period, bounding transport latency to one meter record.
 */
#define MSAP1_METER_RING_RECORDS 2U
#define MSAP1_METER_RING_SIZE \
	(MSAP1_METER_RECORD_SIZE * MSAP1_METER_RING_RECORDS)

struct msap1_meter_dma {
	struct device *dev;
	struct dma_chan *rx;
	void *buffer;
	dma_addr_t buffer_dma;
	struct miscdevice misc;
	wait_queue_head_t wait;
	atomic64_t produced;
	atomic_t opened;
};

struct msap1_meter_file {
	struct msap1_meter_dma *meter;
	u64 consumed;
};

static void msap1_meter_period_complete(void *parameter)
{
	struct msap1_meter_dma *meter = parameter;

	atomic64_inc(&meter->produced);
	wake_up_interruptible(&meter->wait);
}

static int msap1_meter_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct msap1_meter_dma *meter =
		container_of(misc, struct msap1_meter_dma, misc);
	struct msap1_meter_file *context;
	struct dma_async_tx_descriptor *descriptor;
	struct dma_slave_config configuration = {
		.direction = DMA_DEV_TO_MEM,
	};
	dma_cookie_t cookie;
	int error;

	if (atomic_cmpxchg(&meter->opened, 0, 1) != 0)
		return -EBUSY;

	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context) {
		error = -ENOMEM;
		goto clear_opened;
	}
	context->meter = meter;
	atomic64_set(&meter->produced, 0);
	memset(meter->buffer, 0, MSAP1_METER_RING_SIZE);

	error = dmaengine_slave_config(meter->rx, &configuration);
	if (error)
		goto free_context;

	descriptor = dmaengine_prep_dma_cyclic(meter->rx, meter->buffer_dma,
		MSAP1_METER_RING_SIZE, MSAP1_METER_RECORD_SIZE, DMA_DEV_TO_MEM,
		DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!descriptor) {
		error = -EIO;
		goto free_context;
	}
	descriptor->callback = msap1_meter_period_complete;
	descriptor->callback_param = meter;
	cookie = dmaengine_submit(descriptor);
	error = dma_submit_error(cookie);
	if (error)
		goto terminate;

	file->private_data = context;
	dma_async_issue_pending(meter->rx);
	return 0;

terminate:
	dmaengine_terminate_sync(meter->rx);
free_context:
	kfree(context);
clear_opened:
	atomic_set(&meter->opened, 0);
	return error;
}

static int msap1_meter_release(struct inode *inode, struct file *file)
{
	struct msap1_meter_file *context = file->private_data;
	struct msap1_meter_dma *meter = context->meter;

	dmaengine_terminate_sync(meter->rx);
	kfree(context);
	atomic_set(&meter->opened, 0);
	wake_up_interruptible(&meter->wait);
	return 0;
}

static ssize_t msap1_meter_read(struct file *file, char __user *buffer,
			       size_t count, loff_t *offset)
{
	struct msap1_meter_file *context = file->private_data;
	struct msap1_meter_dma *meter = context->meter;
	u64 produced;
	size_t requested;
	size_t available;
	size_t copied = 0;
	int error;

	requested = count / MSAP1_METER_RECORD_SIZE;
	if (!requested)
		return -EINVAL;
	requested = min_t(size_t, requested, MSAP1_METER_RING_RECORDS);

	for (;;) {
		produced = atomic64_read(&meter->produced);
		if (produced != context->consumed)
			break;
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		error = wait_event_interruptible(meter->wait,
			atomic64_read(&meter->produced) != context->consumed ||
			atomic_read(&meter->opened) == 0);
		if (error)
			return error;
		if (atomic_read(&meter->opened) == 0)
			return 0;
	}

	if (produced - context->consumed > MSAP1_METER_RING_RECORDS)
		context->consumed = produced - MSAP1_METER_RING_RECORDS;
	available = min_t(u64, produced - context->consumed, requested);

	while (available--) {
		const size_t index = context->consumed % MSAP1_METER_RING_RECORDS;
		const void *record = meter->buffer +
			index * MSAP1_METER_RECORD_SIZE;

		if (copy_to_user(buffer + copied, record,
				 MSAP1_METER_RECORD_SIZE))
			return copied ? (ssize_t)copied : -EFAULT;
		copied += MSAP1_METER_RECORD_SIZE;
		context->consumed++;
	}
	return copied;
}

static __poll_t msap1_meter_poll(struct file *file, poll_table *wait)
{
	struct msap1_meter_file *context = file->private_data;
	struct msap1_meter_dma *meter = context->meter;
	__poll_t mask = 0;

	poll_wait(file, &meter->wait, wait);
	if (atomic64_read(&meter->produced) != context->consumed)
		mask |= EPOLLIN | EPOLLRDNORM;
	return mask;
}

static const struct file_operations msap1_meter_fops = {
	.owner = THIS_MODULE,
	.open = msap1_meter_open,
	.release = msap1_meter_release,
	.read = msap1_meter_read,
	.poll = msap1_meter_poll,
	.llseek = noop_llseek,
};

static int msap1_meter_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct msap1_meter_dma *meter;
	int error;

	meter = devm_kzalloc(dev, sizeof(*meter), GFP_KERNEL);
	if (!meter)
		return -ENOMEM;
	meter->dev = dev;
	init_waitqueue_head(&meter->wait);
	atomic_set(&meter->opened, 0);

	meter->rx = devm_dma_request_chan(dev, "rx");
	if (IS_ERR(meter->rx))
		return dev_err_probe(dev, PTR_ERR(meter->rx),
			"failed to request AXI DMA S2MM channel\n");
	meter->buffer = dmam_alloc_coherent(dev, MSAP1_METER_RING_SIZE,
		&meter->buffer_dma, GFP_KERNEL);
	if (!meter->buffer)
		return -ENOMEM;

	meter->misc.minor = MISC_DYNAMIC_MINOR;
	meter->misc.name = "msap1-meter";
	meter->misc.fops = &msap1_meter_fops;
	meter->misc.parent = dev;
	meter->misc.mode = 0660;
	error = misc_register(&meter->misc);
	if (error)
		return dev_err_probe(dev, error,
			"failed to register meter device\n");
	platform_set_drvdata(pdev, meter);
	dev_info(dev, "registered /dev/msap1-meter (%u x %u-byte DMA records)\n",
		 MSAP1_METER_RING_RECORDS, MSAP1_METER_RECORD_SIZE);
	return 0;
}

static void msap1_meter_remove(struct platform_device *pdev)
{
	struct msap1_meter_dma *meter = platform_get_drvdata(pdev);

	dmaengine_terminate_sync(meter->rx);
	misc_deregister(&meter->misc);
}

static const struct of_device_id msap1_meter_of_match[] = {
	{ .compatible = "monutchee,msap1-meter-dma" },
	{}
};
MODULE_DEVICE_TABLE(of, msap1_meter_of_match);

static struct platform_driver msap1_meter_driver = {
	.probe = msap1_meter_probe,
	.remove = msap1_meter_remove,
	.driver = {
		.name = "msap1-meter-dma",
		.of_match_table = msap1_meter_of_match,
	},
};
module_platform_driver(msap1_meter_driver);

MODULE_AUTHOR("Monutchee");
MODULE_DESCRIPTION("MSAP1 fixed-record meter AXI DMA consumer");
MODULE_LICENSE("GPL");
