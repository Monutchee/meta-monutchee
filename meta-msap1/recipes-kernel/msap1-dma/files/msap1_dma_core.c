// SPDX-License-Identifier: GPL-2.0-only
/**
 * @file msap1_dma_core.c
 * @brief Shared AXI DMA S2MM transport core for MSAP1 PL acquisition streams.
 *
 * One platform driver serves both acquisition compatibles; the device-tree
 * match data selects a &struct msap1_dma_variant that fixes the period
 * geometry and provides the device-specific hooks.  See msap1_dma.h for the
 * split between core and variants.
 *
 * Design rules the core enforces for every variant:
 *
 *  1. The driver owns only a bounded transport ring.  Long history and
 *     capture-session policy belong to the Linux acquisition daemon, where
 *     retention can be changed without a new bitstream.
 *  2. One ring period is always reserved for the DMA engine's active write
 *     position.  Userspace can consume at most ring_periods - 1 completed
 *     periods, so a period is never exposed while the cyclic DMA may be
 *     overwriting it.
 *  3. read() only ever copies out of a per-open staging buffer, never
 *     straight from the live ring, because copy_to_user() may fault and
 *     sleep for longer than one producer cadence.
 *  4. Transport loss is counted, never silent.  Userspace distinguishes
 *     kernel-side loss (overrun_blocks) from PL-side loss (payload sequence
 *     gaps) via %MSAP1_DMA_IOC_TRANSPORT_STATUS.
 */

#include <linux/atomic.h>
#include <linux/build_bug.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/fs.h>
#include <linux/minmax.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include "msap1_dma.h"

/*
 * The acquisition daemon mirrors these layouts; both are part of the frozen
 * ioctl numbers.  A size change here would silently change the ioctl request
 * values, so pin them at compile time.
 */
static_assert(sizeof(struct msap1_dma_correlation) == 32);
static_assert(sizeof(struct msap1_dma_transport_status) == 32);

/**
 * msap1_dma_usable_periods() - consumer-visible ring capacity.
 * @variant: variant describing the ring geometry.
 *
 * One period is reserved for the DMA engine's active write position (design
 * rule 2 above).
 *
 * Return: number of completed periods a reader may lag behind the producer
 * without loss.
 */
static u32 msap1_dma_usable_periods(const struct msap1_dma_variant *variant)
{
	return variant->ring_periods - 1U;
}

/**
 * msap1_dma_oldest_safe() - first period index still outside the DMA's reach.
 * @produced: absolute completed-period count.
 * @usable:   consumer-visible ring capacity from msap1_dma_usable_periods().
 *
 * Periods older than the returned index either never existed (early after
 * open()) or sit in the ring slot the cyclic DMA has already reclaimed.
 *
 * Return: absolute index of the oldest period a reader may still copy.
 */
static u64 msap1_dma_oldest_safe(u64 produced, u32 usable)
{
	return produced > usable ? produced - usable : 0U;
}

/**
 * msap1_dma_catch_up() - clamp a lagging consumer back into the safe window.
 * @mfile:       consumer state to adjust.
 * @oldest_safe: current lower bound of the safe window.
 *
 * Every period skipped over is genuine kernel-transport loss and is added to
 * the handle's overrun counter (design rule 4 above).
 */
static void msap1_dma_catch_up(struct msap1_dma_file *mfile, u64 oldest_safe)
{
	if (mfile->consumed < oldest_safe) {
		mfile->overrun_periods += oldest_safe - mfile->consumed;
		mfile->consumed = oldest_safe;
	}
}

/**
 * msap1_dma_period_complete() - cyclic DMA completion callback.
 * @parameter: the &struct msap1_dma_device that armed the transaction.
 *
 * Runs in the DMA driver's tasklet context on every completed period.
 *
 * The Xilinx AXI DMA driver first invokes a cyclic callback only after the
 * final descriptor in the ring completes.  That first callback therefore
 * represents one complete ring, not one completed period; subsequent
 * callbacks represent one additional completed period each.  Jumping
 * @produced to ring_periods on the first callback keeps it aligned with the
 * hardware's absolute period count, so that produced %% ring_periods always
 * identifies the period the DMA is currently writing.
 */
static void msap1_dma_period_complete(void *parameter)
{
	struct msap1_dma_device *mdev = parameter;

	if (atomic64_cmpxchg(&mdev->produced, 0,
			     mdev->variant->ring_periods) != 0)
		atomic64_inc(&mdev->produced);
	wake_up_interruptible(&mdev->wait);
}

/**
 * msap1_dma_open() - arm the transport for exactly one consumer.
 * @inode: unused.
 * @file:  handle being opened; private_data becomes a &struct msap1_dma_file.
 *
 * The file lifetime is the DMA lifetime: opening prepares and issues the
 * cyclic transaction, and only then starts the PL-side producer through the
 * variant's arm() hook.  That ordering prevents a nonblocking PL branch from
 * filling its short elasticity FIFO before Linux has somewhere to place a
 * complete period.
 *
 * A second concurrent open fails with -EBUSY; both PL streams have exactly
 * one legitimate consumer (the acquisition daemon), and each open would
 * otherwise re-arm the single underlying DMA channel.
 *
 * Return: 0 on success or a negative errno.
 */
static int msap1_dma_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct msap1_dma_device *mdev =
		container_of(misc, struct msap1_dma_device, misc);
	const struct msap1_dma_variant *variant = mdev->variant;
	struct msap1_dma_file *mfile;
	struct dma_async_tx_descriptor *descriptor;
	struct dma_slave_config configuration = {
		.direction = DMA_DEV_TO_MEM,
	};
	dma_cookie_t cookie;
	int error;

	if (atomic_cmpxchg(&mdev->opened, 0, 1) != 0)
		return -EBUSY;

	mfile = kzalloc(sizeof(*mfile), GFP_KERNEL);
	if (!mfile) {
		error = -ENOMEM;
		goto clear_opened;
	}
	mfile->staging = kmalloc(variant->period_bytes, GFP_KERNEL);
	if (!mfile->staging) {
		error = -ENOMEM;
		goto free_file;
	}
	mfile->mdev = mdev;

	atomic64_set(&mdev->produced, 0);
	memset(mdev->ring, 0, msap1_dma_ring_bytes(variant));

	error = dmaengine_slave_config(mdev->rx, &configuration);
	if (error)
		goto free_staging;
	descriptor = dmaengine_prep_dma_cyclic(mdev->rx, mdev->ring_dma,
		msap1_dma_ring_bytes(variant), variant->period_bytes,
		DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!descriptor) {
		error = -EIO;
		goto free_staging;
	}
	descriptor->callback = msap1_dma_period_complete;
	descriptor->callback_param = mdev;
	cookie = dmaengine_submit(descriptor);
	error = dma_submit_error(cookie);
	if (error)
		goto terminate;

	dma_async_issue_pending(mdev->rx);
	/* Start the producer only after descriptors are visible to the DMA. */
	if (variant->arm) {
		error = variant->arm(mdev);
		if (error)
			goto terminate;
	}
	file->private_data = mfile;
	return 0;

terminate:
	dmaengine_terminate_sync(mdev->rx);
free_staging:
	kfree(mfile->staging);
free_file:
	kfree(mfile);
clear_opened:
	atomic_set(&mdev->opened, 0);
	return error;
}

/**
 * msap1_dma_release() - stop the producer and tear the transport down.
 * @inode: unused.
 * @file:  handle being closed.
 *
 * Mirror image of msap1_dma_open(): the PL producer is stopped through
 * disarm() before the DMA transaction is terminated, so the stream never
 * runs without a place to land.  The final wake-up lets a reader blocked in
 * another thread of the closing process observe the shutdown and return 0.
 *
 * Return: always 0.
 */
static int msap1_dma_release(struct inode *inode, struct file *file)
{
	struct msap1_dma_file *mfile = file->private_data;
	struct msap1_dma_device *mdev = mfile->mdev;

	if (mdev->variant->disarm)
		mdev->variant->disarm(mdev);
	dmaengine_terminate_sync(mdev->rx);
	kfree(mfile->staging);
	kfree(mfile);
	atomic_set(&mdev->opened, 0);
	wake_up_interruptible(&mdev->wait);
	return 0;
}

/**
 * msap1_dma_read() - deliver completed periods to userspace.
 * @file:   consumer handle.
 * @buffer: destination; must hold at least one whole period.
 * @count:  destination size; only whole multiples of the period size are
 *          used, so short buffers cannot split a record across reads.
 * @offset: unused (the transport is a stream; llseek is a no-op).
 *
 * The first read() of a handle performs phase synchronization: at the first
 * callback, period zero is already the active DMA destination again (see
 * msap1_dma_period_complete()), so consumption begins at period one rather
 * than exposing a period that is being overwritten.  This one startup
 * discard is deliberate phase alignment, not a transport overrun; only
 * periods missed beyond it are counted against the handle.
 *
 * The safe window is re-evaluated before every single copy because this
 * task may be descheduled between copies for longer than a producer cadence.
 *
 * Return: bytes copied (a whole number of periods), 0 on shutdown, -EINVAL
 * for a sub-period @count, -EAGAIN when O_NONBLOCK finds no data, or another
 * negative errno.
 */
static ssize_t msap1_dma_read(struct file *file, char __user *buffer,
			      size_t count, loff_t *offset)
{
	struct msap1_dma_file *mfile = file->private_data;
	struct msap1_dma_device *mdev = mfile->mdev;
	const struct msap1_dma_variant *variant = mdev->variant;
	const u32 usable = msap1_dma_usable_periods(variant);
	u64 produced;
	u64 oldest_safe;
	size_t requested;
	size_t available;
	size_t copied = 0;
	int error;

	requested = count / variant->period_bytes;
	if (!requested)
		return -EINVAL;
	requested = min_t(size_t, requested, usable);

	for (;;) {
		produced = atomic64_read(&mdev->produced);
		if (produced != mfile->consumed)
			break;
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		error = wait_event_interruptible(mdev->wait,
			atomic64_read(&mdev->produced) != mfile->consumed ||
			atomic_read(&mdev->opened) == 0);
		if (error)
			return error;
		if (atomic_read(&mdev->opened) == 0)
			return 0;
	}

	oldest_safe = msap1_dma_oldest_safe(produced, usable);
	if (!mfile->synchronized) {
		/*
		 * Startup: skip period zero (already the active DMA target
		 * again), then count only periods genuinely missed because
		 * userspace did not run until later callbacks.
		 */
		if (oldest_safe > 1U)
			mfile->overrun_periods += oldest_safe - 1U;
		mfile->consumed = oldest_safe;
		mfile->synchronized = true;
	} else {
		msap1_dma_catch_up(mfile, oldest_safe);
	}

	available = min_t(u64, produced - mfile->consumed, requested);
	while (available--) {
		const void *period;
		size_t index;

		produced = atomic64_read(&mdev->produced);
		msap1_dma_catch_up(mfile,
				   msap1_dma_oldest_safe(produced, usable));
		if (mfile->consumed >= produced)
			break;

		index = mfile->consumed % variant->ring_periods;
		period = mdev->ring + index * variant->period_bytes;
		/*
		 * Snapshot coherent DMA memory before copy_to_user(), which
		 * may fault and sleep (design rule 3): with usable periods
		 * retained behind the producer, this memcpy cannot be lapped.
		 */
		memcpy(mfile->staging, period, variant->period_bytes);
		if (copy_to_user(buffer + copied, mfile->staging,
				 variant->period_bytes))
			return copied ? (ssize_t)copied : -EFAULT;
		copied += variant->period_bytes;
		mfile->consumed++;
	}
	return copied;
}

/**
 * msap1_dma_poll() - report read readiness.
 * @file: consumer handle.
 * @wait: poll table to register the completion wait queue with.
 *
 * Return: EPOLLIN | EPOLLRDNORM when at least one completed period is
 * pending, otherwise 0.
 */
static __poll_t msap1_dma_poll(struct file *file, poll_table *wait)
{
	struct msap1_dma_file *mfile = file->private_data;
	struct msap1_dma_device *mdev = mfile->mdev;
	__poll_t mask = 0;

	poll_wait(file, &mdev->wait, wait);
	if (atomic64_read(&mdev->produced) != mfile->consumed)
		mask |= EPOLLIN | EPOLLRDNORM;
	return mask;
}

/**
 * msap1_dma_ioctl() - handle common ioctls, delegate the rest to the variant.
 * @file:     consumer handle.
 * @command:  ioctl request number.
 * @argument: userspace pointer argument.
 *
 * %MSAP1_DMA_IOC_TRANSPORT_STATUS is answered here for every variant so
 * both devices expose identical transport diagnostics.  Anything else goes
 * to the variant hook (the waveform correlation latch lives there).
 *
 * Return: 0 on success, -ENOTTY for unknown requests, or a negative errno.
 */
static long msap1_dma_ioctl(struct file *file, unsigned int command,
			    unsigned long argument)
{
	struct msap1_dma_file *mfile = file->private_data;
	struct msap1_dma_device *mdev = mfile->mdev;
	struct msap1_dma_transport_status status;

	if (command == MSAP1_DMA_IOC_TRANSPORT_STATUS) {
		status.produced_blocks = atomic64_read(&mdev->produced);
		status.consumed_blocks = mfile->consumed;
		status.overrun_blocks = mfile->overrun_periods;
		status.ring_blocks = mdev->variant->ring_periods;
		status.reserved = 0;
		if (copy_to_user((void __user *)argument, &status,
				 sizeof(status)))
			return -EFAULT;
		return 0;
	}
	if (mdev->variant->ioctl)
		return mdev->variant->ioctl(mfile, command, argument);
	return -ENOTTY;
}

static const struct file_operations msap1_dma_fops = {
	.owner = THIS_MODULE,
	.open = msap1_dma_open,
	.release = msap1_dma_release,
	.read = msap1_dma_read,
	.unlocked_ioctl = msap1_dma_ioctl,
	.poll = msap1_dma_poll,
	.llseek = noop_llseek,
};

/**
 * msap1_dma_probe() - bind one acquisition device to the transport core.
 * @pdev: platform device created from the device-tree node.
 *
 * Acquires every hardware resource up front (DMA mask, optional register
 * bank, S2MM channel, coherent ring) through devres, then publishes the
 * character device.  Nothing is armed here; the DMA and the PL producer
 * start only when the acquisition daemon opens the device.
 *
 * Return: 0 on success or a negative errno.
 */
static int msap1_dma_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct msap1_dma_variant *variant;
	struct msap1_dma_device *mdev;
	int error;

	variant = device_get_match_data(dev);
	if (!variant || !variant->period_bytes || variant->ring_periods < 2U)
		return dev_err_probe(dev, -EINVAL,
			"invalid transport variant description\n");

	mdev = devm_kzalloc(dev, sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;
	mdev->dev = dev;
	mdev->variant = variant;
	init_waitqueue_head(&mdev->wait);
	atomic_set(&mdev->opened, 0);

	error = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (error)
		return dev_err_probe(dev, error, "32-bit DMA is unavailable\n");
	if (variant->needs_registers) {
		mdev->registers = devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(mdev->registers))
			return PTR_ERR(mdev->registers);
	}
	mdev->rx = devm_dma_request_chan(dev, "rx");
	if (IS_ERR(mdev->rx))
		return dev_err_probe(dev, PTR_ERR(mdev->rx),
			"failed to request %s AXI DMA S2MM channel\n",
			variant->device_name);
	mdev->ring = dmam_alloc_coherent(dev, msap1_dma_ring_bytes(variant),
		&mdev->ring_dma, GFP_KERNEL);
	if (!mdev->ring)
		return -ENOMEM;

	mdev->misc.minor = MISC_DYNAMIC_MINOR;
	mdev->misc.name = variant->device_name;
	mdev->misc.fops = &msap1_dma_fops;
	mdev->misc.parent = dev;
	mdev->misc.mode = 0660;
	error = misc_register(&mdev->misc);
	if (error)
		return dev_err_probe(dev, error,
			"failed to register %s device\n", variant->device_name);
	platform_set_drvdata(pdev, mdev);
	dev_info(dev, "registered /dev/%s (%u x %u-byte DMA periods)\n",
		 variant->device_name, variant->ring_periods,
		 variant->period_bytes);
	return 0;
}

/**
 * msap1_dma_remove() - unbind an acquisition device.
 * @pdev: platform device being removed.
 *
 * Follows the release() ordering: producer off first, DMA second.  Devres
 * frees the remaining resources after this returns.
 */
static void msap1_dma_remove(struct platform_device *pdev)
{
	struct msap1_dma_device *mdev = platform_get_drvdata(pdev);

	if (mdev->variant->disarm)
		mdev->variant->disarm(mdev);
	dmaengine_terminate_sync(mdev->rx);
	misc_deregister(&mdev->misc);
}

static const struct of_device_id msap1_dma_of_match[] = {
	{
		.compatible = "monutchee,msap1-meter-dma",
		.data = &msap1_dma_meter_variant,
	},
	{
		.compatible = "monutchee,msap1-waveform-dma",
		.data = &msap1_dma_waveform_variant,
	},
	{}
};
MODULE_DEVICE_TABLE(of, msap1_dma_of_match);

static struct platform_driver msap1_dma_driver = {
	.probe = msap1_dma_probe,
	.remove = msap1_dma_remove,
	.driver = {
		.name = "msap1-dma",
		.of_match_table = msap1_dma_of_match,
	},
};
module_platform_driver(msap1_dma_driver);

MODULE_AUTHOR("Monutchee");
MODULE_DESCRIPTION("MSAP1 PL acquisition DMA transport (meter and waveform)");
MODULE_LICENSE("GPL");
