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
 *  5. Period availability is observed in the ring, never inferred from
 *     completion callbacks.  See "Why markers" below.
 *
 * Why markers
 * -----------
 * This core used to advance an absolute period counter by one per cyclic
 * completion callback.  That is not a valid model of hardware progress:
 * the Xilinx AXI DMA driver invokes the cyclic callback from a tasklet,
 * once per interrupt rather than once per completed period, and
 * tasklet_schedule() is idempotent — two periods that complete close
 * together yield a single callback.  The counter then under-counts, and
 * because every bound in this file derived from it (safe window, catch-up,
 * overrun) the loss was invisible: read() handed out a slot the DMA had
 * already overwritten while overrun_blocks stayed zero.
 *
 * That is exactly what the meter stream hits.  Its two producers emit a
 * record back to back once per 150/180-cycle aggregation window (the 15th
 * basic record and the aggregate that folds it), so one interrupt covered
 * two periods once per window, forever, phase-locked.  Residue cannot
 * substitute for the counter either: xilinx_dma_get_residue() sums
 * (control - status) over the descriptor segments, and nothing resets a
 * segment's status between laps of a cyclic transfer, so residue collapses
 * to zero after the first lap.
 *
 * So the consumer stamps a marker into the tail of every period it takes,
 * and treats a period as complete only once that marker is gone.  The DMA
 * writes a period in ascending address order, so the tail lands last: a
 * partially written period still holds its marker and is correctly withheld.
 * Detection is then a property of the data that actually arrived, immune to
 * interrupt coalescing, and callbacks are demoted to wake-ups.
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
static_assert(sizeof(struct msap1_dma_ten_minute_boundary) == 16);
static_assert(sizeof(struct msap1_dma_frequency_10s_boundary) == 80);

/**
 * msap1_dma_usable_periods() - consumer-visible ring capacity.
 * @mdev: transport device; its effective ring depth is fixed at probe.
 *
 * One period is reserved for the DMA engine's active write position (design
 * rule 2 above).
 *
 * Return: number of completed periods a reader may lag behind the producer
 * without loss.
 */
static u32 msap1_dma_usable_periods(const struct msap1_dma_device *mdev)
{
	return mdev->ring_periods - 1U;
}

/*
 * Completion marker stamped into the tail of every period the consumer
 * takes.  Four words carrying a fixed signature plus the absolute period
 * that will next land in the slot: the DMA overwriting the slot with any
 * payload clears it, and the odds of real payload reproducing all sixteen
 * bytes are negligible even for the waveform stream, whose periods are raw
 * samples with no reserved fields.
 */
#define MSAP1_DMA_MARKER_WORDS 4
#define MSAP1_DMA_MARKER_BYTES (MSAP1_DMA_MARKER_WORDS * sizeof(u32))

static void msap1_dma_build_marker(u64 period, u32 marker[])
{
	marker[0] = 0x5041534du;			/* "MSAP" */
	marker[1] = 0x4c415453u;			/* "STAL" */
	marker[2] = (u32)period;
	marker[3] = (u32)(period >> 32);
}

/**
 * msap1_dma_period_at() - ring address of an absolute period.
 * @mdev:   transport device.
 * @period: absolute period index.
 *
 * Return: pointer to the period's slot in the cyclic ring.
 */
static void *msap1_dma_period_at(struct msap1_dma_device *mdev, u64 period)
{
	size_t index = period % mdev->ring_periods;

	return mdev->ring + index * mdev->variant->period_bytes;
}

static void *msap1_dma_marker_at(struct msap1_dma_device *mdev, u64 period)
{
	return msap1_dma_period_at(mdev, period) +
	       mdev->variant->period_bytes - MSAP1_DMA_MARKER_BYTES;
}

/**
 * msap1_dma_mark_pending() - declare a slot empty until @period arrives.
 * @mdev:   transport device.
 * @period: absolute period the slot will receive next.
 *
 * Called on the ring at open() and on each slot as it is consumed.  The
 * marker names the period the reader expects next in that slot, so a slot
 * left over from the previous lap can never be mistaken for a fresh one.
 */
static void msap1_dma_mark_pending(struct msap1_dma_device *mdev, u64 period)
{
	u32 marker[MSAP1_DMA_MARKER_WORDS];

	msap1_dma_build_marker(period, marker);
	memcpy(msap1_dma_marker_at(mdev, period), marker, sizeof(marker));
}

/**
 * msap1_dma_period_ready() - has the DMA finished writing this period?
 * @mdev:   transport device.
 * @period: absolute period index.
 *
 * Return: true once the period's tail no longer holds the marker the
 * consumer stamped there, i.e. once the DMA has written the slot through to
 * its final byte.
 */
static bool msap1_dma_period_ready(struct msap1_dma_device *mdev, u64 period)
{
	u32 marker[MSAP1_DMA_MARKER_WORDS];
	bool ready;

	msap1_dma_build_marker(period, marker);
	ready = memcmp(msap1_dma_marker_at(mdev, period), marker,
		       sizeof(marker)) != 0;
	if (ready) {
		/*
		 * Order the payload read after the marker read, so a period
		 * observed complete is copied out complete.
		 */
		dma_rmb();
	}
	return ready;
}

/**
 * msap1_dma_ready_ahead() - completed periods waiting from @from onwards.
 * @mdev: transport device.
 * @from: first absolute period to test.
 *
 * Return: number of consecutive completed periods, at most ring_periods.
 */
static u32 msap1_dma_ready_ahead(struct msap1_dma_device *mdev, u64 from)
{
	u32 ready = 0;

	while (ready < mdev->ring_periods &&
	       msap1_dma_period_ready(mdev, from + ready))
		ready++;
	return ready;
}

/**
 * msap1_dma_skip_incomplete() - step over a period the DMA never finished.
 * @mfile: consumer state.
 *
 * The engine fills periods in order, so a later period being complete while
 * the next one to consume still holds its marker proves that period will
 * never complete: the producer ended the packet before the period was full
 * (a short packet is a PL framing fault) and the descriptor was closed early.
 * Blocking on it would stall the stream permanently, so count it as
 * transport loss and move on (design rule 4) — the marker scheme turns what
 * used to be a silently delivered half-record into counted loss.
 *
 * Return: true when a period was skipped.
 */
static bool msap1_dma_skip_incomplete(struct msap1_dma_file *mfile)
{
	struct msap1_dma_device *mdev = mfile->mdev;

	if (msap1_dma_period_ready(mdev, mfile->consumed) ||
	    !msap1_dma_period_ready(mdev, mfile->consumed + 1))
		return false;

	msap1_dma_mark_pending(mdev,
			       mfile->consumed + mdev->ring_periods);
	mfile->overrun_periods++;
	mfile->consumed++;
	return true;
}

/**
 * msap1_dma_advance() - drop any unfinishable periods ahead of the consumer.
 * @mfile: consumer state.
 *
 * Bounded by the ring size: beyond that the scan would be chasing the
 * producer rather than clearing a gap.
 */
static void msap1_dma_advance(struct msap1_dma_file *mfile)
{
	u32 bound = mfile->mdev->ring_periods;

	while (bound-- && msap1_dma_skip_incomplete(mfile))
		;
}

/**
 * msap1_dma_period_complete() - cyclic DMA completion callback.
 * @parameter: the &struct msap1_dma_device that armed the transaction.
 *
 * A wake-up only.  The callback rate is not the period rate (see "Why
 * markers" at the top of this file), so nothing here may be used for
 * accounting; @callbacks exists purely as a diagnostic.
 */
static void msap1_dma_period_complete(void *parameter)
{
	struct msap1_dma_device *mdev = parameter;

	atomic64_inc(&mdev->callbacks);
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
	u64 period;
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

	atomic64_set(&mdev->callbacks, 0);
	memset(mdev->ring, 0, msap1_dma_ring_bytes(mdev));
	/*
	 * Mark every slot empty before the DMA is armed: slot N awaits period
	 * N on the first lap, so read() blocks until real data lands rather
	 * than handing out the zeroed ring.
	 */
	for (period = 0; period < mdev->ring_periods; period++)
		msap1_dma_mark_pending(mdev, period);

	error = dmaengine_slave_config(mdev->rx, &configuration);
	if (error)
		goto free_staging;
	descriptor = dmaengine_prep_dma_cyclic(mdev->rx, mdev->ring_dma,
		msap1_dma_ring_bytes(mdev), variant->period_bytes,
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
 * Availability is read out of the ring itself: a period is delivered once
 * its completion marker is gone (see "Why markers" at the top of this
 * file).  No phase alignment is needed at startup — the ring is stamped
 * before the DMA is armed, so period zero is delivered when, and only when,
 * the hardware has actually written it.
 *
 * The marker is re-tested before every single copy because this task may be
 * descheduled between copies for longer than a producer cadence.
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
	const u32 usable = msap1_dma_usable_periods(mdev);
	size_t requested;
	size_t copied = 0;
	u32 ready;
	int error;

	requested = count / variant->period_bytes;
	if (!requested)
		return -EINVAL;
	requested = min_t(size_t, requested, usable);

	for (;;) {
		msap1_dma_advance(mfile);
		ready = msap1_dma_ready_ahead(mdev, mfile->consumed);
		if (ready)
			break;
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;
		error = wait_event_interruptible(mdev->wait,
			msap1_dma_period_ready(mdev, mfile->consumed) ||
			msap1_dma_period_ready(mdev, mfile->consumed + 1) ||
			atomic_read(&mdev->opened) == 0);
		if (error)
			return error;
		if (atomic_read(&mdev->opened) == 0)
			return 0;
	}

	/*
	 * A completely fresh ring means the producer wrote at least a whole
	 * ring since the last read, so the period under the write pointer was
	 * overwritten before it could be delivered (design rule 4).  Count one
	 * period: the marker scheme proves loss happened but not how much.
	 */
	if (ready == mdev->ring_periods)
		mfile->overrun_periods++;

	while (copied / variant->period_bytes < requested &&
	       msap1_dma_period_ready(mdev, mfile->consumed)) {
		/*
		 * Snapshot coherent DMA memory before copy_to_user(), which
		 * may fault and sleep (design rule 3); the memcpy itself is
		 * short enough that the producer cannot lap it at any
		 * supported period rate.  A consumer slow enough to be lapped
		 * across calls is caught by the all-fresh check above and by
		 * the payload sequence numbers.
		 */
		memcpy(mfile->staging,
		       msap1_dma_period_at(mdev, mfile->consumed),
		       variant->period_bytes);
		if (copy_to_user(buffer + copied, mfile->staging,
				 variant->period_bytes))
			return copied ? (ssize_t)copied : -EFAULT;
		/* Re-arm the slot for the period that lands there next lap. */
		msap1_dma_mark_pending(mdev,
				       mfile->consumed + mdev->ring_periods);
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
	/*
	 * A ready period behind the next one still means read() will return
	 * data: it skips the unfinishable period first (msap1_dma_advance()).
	 */
	if (msap1_dma_period_ready(mdev, mfile->consumed) ||
	    msap1_dma_period_ready(mdev, mfile->consumed + 1))
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
 * to an optional variant hook; variants without one return -ENOTTY.
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
		/*
		 * Report what the ring proves the DMA has written, not the
		 * completion-callback count, which coalesces (see "Why
		 * markers").
		 */
		status.produced_blocks = mfile->consumed +
			msap1_dma_ready_ahead(mdev, mfile->consumed);
		status.consumed_blocks = mfile->consumed;
		status.overrun_blocks = mfile->overrun_periods;
		status.ring_blocks = mdev->ring_periods;
		/*
		 * Diagnostic: the gap between produced_blocks and this is the
		 * callback deficit caused by completion coalescing.  Nothing
		 * in the transport depends on it.
		 */
		status.callbacks = (__u32)atomic64_read(&mdev->callbacks);
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
	/*
	 * Ring depth is a deployment decision (it converts directly into
	 * consumer-stall tolerance at the deployed sample rate), so the device
	 * tree may override the variant default without a driver rebuild.
	 * The upper bound only guards against a typo'd property exhausting
	 * CMA; it is far above any depth a real deployment would choose.
	 */
	mdev->ring_periods = variant->ring_periods;
	device_property_read_u32(dev, "monutchee,ring-blocks",
				 &mdev->ring_periods);
	if (mdev->ring_periods < 2U || mdev->ring_periods > 4096U)
		return dev_err_probe(dev, -EINVAL,
			"monutchee,ring-blocks %u is outside 2..4096\n",
			mdev->ring_periods);
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
	mdev->ring = dmam_alloc_coherent(dev, msap1_dma_ring_bytes(mdev),
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
	dev_info(dev, "registered /dev/%s (%u x %u-byte DMA periods, %u KiB ring)\n",
		 variant->device_name, mdev->ring_periods,
		 variant->period_bytes, msap1_dma_ring_bytes(mdev) / 1024U);
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
