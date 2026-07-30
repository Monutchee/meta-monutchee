// SPDX-License-Identifier: GPL-2.0-only
/*
 * MSAP1 raw waveform AXI DMA S2MM consumer.
 *
 * The driver deliberately owns only a bounded transport ring.
 * Long pre-trigger history and capture-session policy belong to the Linux
 * acquisition daemon, where retention can be changed without a new bitstream.
 *
 * One period is always reserved for the DMA engine's active write position.
 * Userspace can therefore consume at most RING_BLOCKS - 1 completed periods.
 * This avoids exposing a block while the cyclic DMA is overwriting it.
 */

#include <linux/atomic.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define MSAP1_WAVEFORM_BLOCK_SIZE 32832U
#define MSAP1_WAVEFORM_RING_BLOCKS 64U
#define MSAP1_WAVEFORM_RING_SIZE \
	(MSAP1_WAVEFORM_BLOCK_SIZE * MSAP1_WAVEFORM_RING_BLOCKS)

#define MSAP1_WAVEFORM_CONTROL 0x08U
#define MSAP1_WAVEFORM_LATCHED_TICK_LO 0x10U
#define MSAP1_WAVEFORM_LATCHED_TICK_HI 0x14U
#define MSAP1_WAVEFORM_LATCHED_SEQUENCE_LO 0x18U
#define MSAP1_WAVEFORM_LATCHED_SEQUENCE_HI 0x1cU

struct msap1_waveform_correlation {
	__u64 tai_before_nanoseconds;
	__u64 tai_after_nanoseconds;
	__u64 pl_tick;
	__u64 frame_sequence;
};

struct msap1_waveform_transport_status {
	__u64 produced_blocks;
	__u64 consumed_blocks;
	__u64 overrun_blocks;
	__u32 ring_blocks;
	__u32 reserved;
};

#define MSAP1_WAVEFORM_IOC_CORRELATE \
	_IOR('W', 0x01, struct msap1_waveform_correlation)
#define MSAP1_WAVEFORM_IOC_TRANSPORT_STATUS \
	_IOR('W', 0x02, struct msap1_waveform_transport_status)

struct msap1_waveform_dma {
	struct device *dev;
	struct dma_chan *rx;
	void *buffer;
	dma_addr_t buffer_dma;
	void __iomem *registers;
	struct miscdevice misc;
	wait_queue_head_t wait;
	atomic64_t produced;
	atomic_t opened;
};

struct msap1_waveform_file {
	struct msap1_waveform_dma *waveform;
	u64 consumed;
	u64 overrun_blocks;
	void *staging;
};

static u64 msap1_read_u64(void __iomem *registers, unsigned int low_offset)
{
	u32 high_before;
	u32 high_after;
	u32 low;

	do {
		high_before = readl(registers + low_offset + 4U);
		low = readl(registers + low_offset);
		high_after = readl(registers + low_offset + 4U);
	} while (high_before != high_after);
	return ((u64)high_after << 32) | low;
}

static void msap1_waveform_period_complete(void *parameter)
{
	struct msap1_waveform_dma *waveform = parameter;

	atomic64_inc(&waveform->produced);
	wake_up_interruptible(&waveform->wait);
}

static int msap1_waveform_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct msap1_waveform_dma *waveform =
		container_of(misc, struct msap1_waveform_dma, misc);
	struct msap1_waveform_file *context;
	struct dma_async_tx_descriptor *descriptor;
	struct dma_slave_config configuration = {
		.direction = DMA_DEV_TO_MEM,
	};
	dma_cookie_t cookie;
	int error;

	if (atomic_cmpxchg(&waveform->opened, 0, 1) != 0)
		return -EBUSY;
	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context) {
		error = -ENOMEM;
		goto clear_opened;
	}
	context->staging = kmalloc(MSAP1_WAVEFORM_BLOCK_SIZE, GFP_KERNEL);
	if (!context->staging) {
		error = -ENOMEM;
		goto free_context;
	}
	context->waveform = waveform;
	atomic64_set(&waveform->produced, 0);
	memset(waveform->buffer, 0, MSAP1_WAVEFORM_RING_SIZE);

	error = dmaengine_slave_config(waveform->rx, &configuration);
	if (error)
		goto free_context;
	descriptor = dmaengine_prep_dma_cyclic(waveform->rx,
		waveform->buffer_dma, MSAP1_WAVEFORM_RING_SIZE,
		MSAP1_WAVEFORM_BLOCK_SIZE, DMA_DEV_TO_MEM,
		DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!descriptor) {
		error = -EIO;
		goto free_context;
	}
	descriptor->callback = msap1_waveform_period_complete;
	descriptor->callback_param = waveform;
	cookie = dmaengine_submit(descriptor);
	error = dma_submit_error(cookie);
	if (error)
		goto terminate;

	file->private_data = context;
	dma_async_issue_pending(waveform->rx);
	/*
	 * Arm PL only after descriptors are visible to the DMA.  This ordering
	 * prevents the nonblocking PL branch from filling its short elasticity
	 * FIFO before Linux has somewhere to place a complete WFM1 block.
	 */
	writel(0x1U, waveform->registers + MSAP1_WAVEFORM_CONTROL);
	(void)readl(waveform->registers + MSAP1_WAVEFORM_CONTROL);
	return 0;

terminate:
	dmaengine_terminate_sync(waveform->rx);
free_context:
	kfree(context->staging);
	kfree(context);
clear_opened:
	atomic_set(&waveform->opened, 0);
	return error;
}

static int msap1_waveform_release(struct inode *inode, struct file *file)
{
	struct msap1_waveform_file *context = file->private_data;
	struct msap1_waveform_dma *waveform = context->waveform;

	writel(0x0U, waveform->registers + MSAP1_WAVEFORM_CONTROL);
	(void)readl(waveform->registers + MSAP1_WAVEFORM_CONTROL);
	dmaengine_terminate_sync(waveform->rx);
	kfree(context->staging);
	kfree(context);
	atomic_set(&waveform->opened, 0);
	wake_up_interruptible(&waveform->wait);
	return 0;
}

static ssize_t msap1_waveform_read(struct file *file, char __user *buffer,
				   size_t count, loff_t *offset)
{
	struct msap1_waveform_file *context = file->private_data;
	struct msap1_waveform_dma *waveform = context->waveform;
	u64 produced;
	u64 oldest_safe;
	size_t requested;
	size_t available;
	size_t copied = 0;
	int error;

	requested = count / MSAP1_WAVEFORM_BLOCK_SIZE;
	if (!requested)
		return -EINVAL;
	requested = min_t(size_t, requested,
			  MSAP1_WAVEFORM_RING_BLOCKS - 1U);

	for (;;) {
		produced = atomic64_read(&waveform->produced);
		if (produced != context->consumed)
			break;
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		error = wait_event_interruptible(waveform->wait,
			atomic64_read(&waveform->produced) != context->consumed ||
			atomic_read(&waveform->opened) == 0);
		if (error)
			return error;
		if (atomic_read(&waveform->opened) == 0)
			return 0;
	}

	oldest_safe = produced > MSAP1_WAVEFORM_RING_BLOCKS - 1U
		? produced - (MSAP1_WAVEFORM_RING_BLOCKS - 1U)
		: 0U;
	if (context->consumed < oldest_safe) {
		context->overrun_blocks += oldest_safe - context->consumed;
		context->consumed = oldest_safe;
	}
	available = min_t(u64, produced - context->consumed, requested);
	while (available--) {
		const void *block;
		size_t index;

		/*
		 * Re-evaluate the safe window before every copy. A task may have
		 * been descheduled since the first availability snapshot.
		 */
		produced = atomic64_read(&waveform->produced);
		oldest_safe = produced > MSAP1_WAVEFORM_RING_BLOCKS - 1U
			? produced - (MSAP1_WAVEFORM_RING_BLOCKS - 1U)
			: 0U;
		if (context->consumed < oldest_safe) {
			context->overrun_blocks += oldest_safe - context->consumed;
			context->consumed = oldest_safe;
		}
		if (context->consumed >= produced)
			break;

		index = context->consumed % MSAP1_WAVEFORM_RING_BLOCKS;
		block = waveform->buffer +
			index * MSAP1_WAVEFORM_BLOCK_SIZE;
		/*
		 * Snapshot coherent DMA memory before copy_to_user(), which may
		 * fault and sleep. With 63 completed periods retained, this memcpy
		 * cannot be lapped by the 32 kframe/s producer.
		 */
		memcpy(context->staging, block, MSAP1_WAVEFORM_BLOCK_SIZE);
		if (copy_to_user(buffer + copied, context->staging,
				 MSAP1_WAVEFORM_BLOCK_SIZE))
			return copied ? (ssize_t)copied : -EFAULT;
		copied += MSAP1_WAVEFORM_BLOCK_SIZE;
		context->consumed++;
	}
	return copied;
}

static long msap1_waveform_ioctl(struct file *file, unsigned int command,
				 unsigned long argument)
{
	struct msap1_waveform_file *context = file->private_data;
	struct msap1_waveform_dma *waveform = context->waveform;
	struct msap1_waveform_correlation correlation;
	struct msap1_waveform_transport_status transport;
	struct timespec64 before;
	struct timespec64 after;

	if (command == MSAP1_WAVEFORM_IOC_TRANSPORT_STATUS) {
		transport.produced_blocks =
			atomic64_read(&waveform->produced);
		transport.consumed_blocks = context->consumed;
		transport.overrun_blocks = context->overrun_blocks;
		transport.ring_blocks = MSAP1_WAVEFORM_RING_BLOCKS;
		transport.reserved = 0;
		if (copy_to_user((void __user *)argument, &transport,
				 sizeof(transport)))
			return -EFAULT;
		return 0;
	}
	if (command != MSAP1_WAVEFORM_IOC_CORRELATE)
		return -ENOTTY;
	ktime_get_clocktai_ts64(&before);
	/* ENABLE | LATCH. LATCH is a write command, not retained PL state. */
	writel(0x3U, waveform->registers + MSAP1_WAVEFORM_CONTROL);
	(void)readl(waveform->registers + MSAP1_WAVEFORM_CONTROL);
	correlation.pl_tick = msap1_read_u64(waveform->registers,
		MSAP1_WAVEFORM_LATCHED_TICK_LO);
	correlation.frame_sequence = msap1_read_u64(waveform->registers,
		MSAP1_WAVEFORM_LATCHED_SEQUENCE_LO);
	ktime_get_clocktai_ts64(&after);
	correlation.tai_before_nanoseconds =
		timespec64_to_ns(&before);
	correlation.tai_after_nanoseconds =
		timespec64_to_ns(&after);
	if (copy_to_user((void __user *)argument, &correlation,
			 sizeof(correlation)))
		return -EFAULT;
	return 0;
}

static __poll_t msap1_waveform_poll(struct file *file, poll_table *wait)
{
	struct msap1_waveform_file *context = file->private_data;
	struct msap1_waveform_dma *waveform = context->waveform;
	__poll_t mask = 0;

	poll_wait(file, &waveform->wait, wait);
	if (atomic64_read(&waveform->produced) != context->consumed)
		mask |= EPOLLIN | EPOLLRDNORM;
	return mask;
}

static const struct file_operations msap1_waveform_fops = {
	.owner = THIS_MODULE,
	.open = msap1_waveform_open,
	.release = msap1_waveform_release,
	.read = msap1_waveform_read,
	.unlocked_ioctl = msap1_waveform_ioctl,
	.poll = msap1_waveform_poll,
	.llseek = noop_llseek,
};

static int msap1_waveform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct msap1_waveform_dma *waveform;
	int error;

	waveform = devm_kzalloc(dev, sizeof(*waveform), GFP_KERNEL);
	if (!waveform)
		return -ENOMEM;
	waveform->dev = dev;
	init_waitqueue_head(&waveform->wait);
	atomic_set(&waveform->opened, 0);

	error = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (error)
		return dev_err_probe(dev, error, "32-bit DMA is unavailable\n");
	waveform->registers = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(waveform->registers))
		return PTR_ERR(waveform->registers);
	waveform->rx = devm_dma_request_chan(dev, "rx");
	if (IS_ERR(waveform->rx))
		return dev_err_probe(dev, PTR_ERR(waveform->rx),
			"failed to request waveform AXI DMA S2MM channel\n");
	waveform->buffer = dmam_alloc_coherent(dev, MSAP1_WAVEFORM_RING_SIZE,
		&waveform->buffer_dma, GFP_KERNEL);
	if (!waveform->buffer)
		return -ENOMEM;

	waveform->misc.minor = MISC_DYNAMIC_MINOR;
	waveform->misc.name = "msap1-waveform";
	waveform->misc.fops = &msap1_waveform_fops;
	waveform->misc.parent = dev;
	waveform->misc.mode = 0660;
	error = misc_register(&waveform->misc);
	if (error)
		return dev_err_probe(dev, error,
			"failed to register waveform device\n");
	platform_set_drvdata(pdev, waveform);
	dev_info(dev,
		 "registered /dev/msap1-waveform (%u x %u-byte DMA blocks)\n",
		 MSAP1_WAVEFORM_RING_BLOCKS, MSAP1_WAVEFORM_BLOCK_SIZE);
	return 0;
}

static void msap1_waveform_remove(struct platform_device *pdev)
{
	struct msap1_waveform_dma *waveform = platform_get_drvdata(pdev);

	writel(0x0U, waveform->registers + MSAP1_WAVEFORM_CONTROL);
	dmaengine_terminate_sync(waveform->rx);
	misc_deregister(&waveform->misc);
}

static const struct of_device_id msap1_waveform_of_match[] = {
	{ .compatible = "monutchee,msap1-waveform-dma" },
	{}
};
MODULE_DEVICE_TABLE(of, msap1_waveform_of_match);

static struct platform_driver msap1_waveform_driver = {
	.probe = msap1_waveform_probe,
	.remove = msap1_waveform_remove,
	.driver = {
		.name = "msap1-waveform-dma",
		.of_match_table = msap1_waveform_of_match,
	},
};
module_platform_driver(msap1_waveform_driver);

MODULE_AUTHOR("Monutchee");
MODULE_DESCRIPTION("MSAP1 raw waveform AXI DMA consumer");
MODULE_LICENSE("GPL");
