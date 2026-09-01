// SPDX-License-Identifier: GPL-2.0-only
/**
 * @file msap1_dma_waveform.c
 * @brief Raw-waveform personality of the MSAP1 DMA transport
 *        (/dev/msap1-waveform).
 *
 * The PL waveform branch packetizes full-rate ADC frames into fixed
 * 32832-byte WFM1 blocks (64-byte header + 1024 frames x 32 bytes) behind a
 * nonblocking elasticity FIFO that drops frames rather than backpressure
 * metering.  Unlike the meter stream, the waveform peripheral is armed by
 * Linux through a small AXI-Lite register bank, which also latches the PL
 * tick counter and frame sequence for CLOCK_TAI correlation.
 */

#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/time64.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "msap1_dma.h"

/** Size of one WFM1 block, fixed by the PL packetizer format. */
#define MSAP1_WAVEFORM_BLOCK_BYTES 32832U

/**
 * Default ring depth in blocks (~8 MiB of coherent memory); the device-tree
 * "monutchee,ring-blocks" property overrides it without a driver rebuild.
 *
 * Depth buys consumer-stall tolerance only: availability comes from the
 * ring's completion markers (see msap1_dma_period_ready()), so the first
 * block is readable as soon as the DMA writes it, at any depth.  Block
 * cadence is rate-dependent — 32 ms at the 32 kframe/s base rate, 8 ms at
 * the 128 kframe/s maximum — so 255 usable blocks tolerate ~8.2 s and
 * ~2.0 s of daemon stall respectively.
 *
 * Sizing history: 64 blocks was chosen against the base rate (~2 s) and
 * silently shrank to 0.512 s when deployments moved to 128 kframe/s.  In
 * the field (2026-08-17) that lost ~49 blocks per overrun event during
 * capture sessions, so the default now provides the intended ~2 s at the
 * maximum rate instead of the base rate.
 */
#define MSAP1_WAVEFORM_RING_BLOCKS 256U

/*
 * AXI-Lite register bank of the waveform peripheral.
 *
 * CONTROL bit 0 enables the stream (retained state); bit 1 is a latch
 * command that atomically captures the PL tick counter and frame sequence
 * into the LATCHED_* registers.  The 64-bit latched values are exposed as
 * LO/HI pairs.
 */
#define MSAP1_WAVEFORM_CONTROL 0x08U
#define MSAP1_WAVEFORM_CONTROL_ENABLE 0x1U
#define MSAP1_WAVEFORM_CONTROL_LATCH 0x2U
#define MSAP1_WAVEFORM_LATCHED_TICK_LO 0x10U
#define MSAP1_WAVEFORM_LATCHED_SEQUENCE_LO 0x18U
#define MSAP1_WAVEFORM_TEN_MINUTE_TARGET_LO 0x40U
#define MSAP1_WAVEFORM_TEN_MINUTE_CONTROL 0x48U
#define MSAP1_WAVEFORM_TEN_MINUTE_VALID 0x1U
#define MSAP1_WAVEFORM_TEN_MINUTE_COMMIT 0x2U
#define MSAP1_WAVEFORM_FREQUENCY_10S_START_SAMPLE_LO 0x50U
#define MSAP1_WAVEFORM_FREQUENCY_10S_END_SAMPLE_LO 0x58U
#define MSAP1_WAVEFORM_FREQUENCY_10S_UTC_START_LO 0x60U
#define MSAP1_WAVEFORM_FREQUENCY_10S_UTC_END_LO 0x68U
#define MSAP1_WAVEFORM_FREQUENCY_10S_UNCERTAINTY_LO 0x70U
#define MSAP1_WAVEFORM_FREQUENCY_10S_RATE_MILLIHZ 0x78U
#define MSAP1_WAVEFORM_FREQUENCY_10S_GENERATION 0x7CU
#define MSAP1_WAVEFORM_FREQUENCY_10S_PROFILE 0x80U
#define MSAP1_WAVEFORM_FREQUENCY_10S_CONTROL 0x84U
#define MSAP1_WAVEFORM_FREQUENCY_10S_OBSERVER_STATUS 0x88U
#define MSAP1_WAVEFORM_FREQUENCY_10S_COMPLETED 0x8CU
#define MSAP1_WAVEFORM_FREQUENCY_10S_DROPPED 0x90U
#define MSAP1_WAVEFORM_FREQUENCY_10S_OVERFLOW 0x94U
#define MSAP1_WAVEFORM_FREQUENCY_10S_DISCONTINUITY 0x98U
#define MSAP1_WAVEFORM_FREQUENCY_10S_COMMIT (1U << 2)
#define MSAP1_WAVEFORM_FREQUENCY_10S_CANCEL (1U << 3)
#define MSAP1_WAVEFORM_FREQUENCY_10S_UPDATE (1U << 8)
#define MSAP1_WAVEFORM_FREQUENCY_10S_RETAINED_FLAGS \
	(MSAP1_DMA_FREQUENCY_10S_BOUNDARY_VALID | \
	 MSAP1_DMA_FREQUENCY_10S_TIME_SYNCHRONIZED)
#define MSAP1_WAVEFORM_FREQUENCY_10S_REQUEST_FLAGS \
	(MSAP1_WAVEFORM_FREQUENCY_10S_RETAINED_FLAGS | \
	 MSAP1_DMA_FREQUENCY_10S_CANCEL)

/**
 * msap1_waveform_read_u64() - tear-free read of a 64-bit LO/HI register pair.
 * @registers:  mapped register bank.
 * @low_offset: offset of the LO word; the HI word follows at +4.
 *
 * The PL updates the pair non-atomically, so the HI word is read on both
 * sides of the LO word and the read retried until HI is stable.
 *
 * Return: the consistent 64-bit register value.
 */
static u64 msap1_waveform_read_u64(void __iomem *registers,
				   unsigned int low_offset)
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

static void msap1_waveform_write_u64(void __iomem *registers,
				    unsigned int low_offset, u64 value)
{
	writel(lower_32_bits(value), registers + low_offset);
	writel(upper_32_bits(value), registers + low_offset + 4U);
}

static bool msap1_waveform_frequency_10s_matches(
	void __iomem *registers,
	const struct msap1_dma_frequency_10s_boundary *boundary, u32 control,
	u32 previous_control)
{
	return msap1_waveform_read_u64(registers,
			MSAP1_WAVEFORM_FREQUENCY_10S_START_SAMPLE_LO) ==
			boundary->start_sample_index &&
		msap1_waveform_read_u64(registers,
			MSAP1_WAVEFORM_FREQUENCY_10S_END_SAMPLE_LO) ==
			boundary->end_sample_index &&
		msap1_waveform_read_u64(registers,
			MSAP1_WAVEFORM_FREQUENCY_10S_UTC_START_LO) ==
			boundary->utc_start_nanoseconds &&
		msap1_waveform_read_u64(registers,
			MSAP1_WAVEFORM_FREQUENCY_10S_UTC_END_LO) ==
			boundary->utc_end_nanoseconds &&
		msap1_waveform_read_u64(registers,
			MSAP1_WAVEFORM_FREQUENCY_10S_UNCERTAINTY_LO) ==
			boundary->utc_uncertainty_nanoseconds &&
		readl(registers + MSAP1_WAVEFORM_FREQUENCY_10S_RATE_MILLIHZ) ==
			boundary->measured_sample_rate_millihz &&
		readl(registers + MSAP1_WAVEFORM_FREQUENCY_10S_GENERATION) ==
			boundary->boundary_generation &&
		readl(registers + MSAP1_WAVEFORM_FREQUENCY_10S_PROFILE) ==
			boundary->profile &&
		(control & MSAP1_WAVEFORM_FREQUENCY_10S_RETAINED_FLAGS) ==
			(boundary->flags &
			 MSAP1_WAVEFORM_FREQUENCY_10S_RETAINED_FLAGS) &&
		!!(control & MSAP1_WAVEFORM_FREQUENCY_10S_CANCEL) ==
			!!(boundary->flags & MSAP1_DMA_FREQUENCY_10S_CANCEL) &&
		!!(control & MSAP1_WAVEFORM_FREQUENCY_10S_UPDATE) !=
			!!(previous_control & MSAP1_WAVEFORM_FREQUENCY_10S_UPDATE);
}

/**
 * msap1_waveform_arm() - enable the PL waveform stream.
 * @mdev: waveform transport device.
 *
 * Called by the core strictly after dma_async_issue_pending(); enabling
 * earlier would let the nonblocking PL branch fill its short elasticity
 * FIFO before Linux has somewhere to place a complete WFM1 block.  The
 * read-back flushes the posted write so the stream is running when open()
 * returns.
 *
 * Return: always 0; the register write cannot fail.
 */
static int msap1_waveform_arm(struct msap1_dma_device *mdev)
{
	writel(MSAP1_WAVEFORM_CONTROL_ENABLE,
	       mdev->registers + MSAP1_WAVEFORM_CONTROL);
	(void)readl(mdev->registers + MSAP1_WAVEFORM_CONTROL);
	return 0;
}

/**
 * msap1_waveform_disarm() - stop the PL waveform stream.
 * @mdev: waveform transport device.
 *
 * Called by the core before terminating the DMA on release and remove, so
 * the producer never streams into a torn-down transport.
 */
static void msap1_waveform_disarm(struct msap1_dma_device *mdev)
{
	writel(0x0U, mdev->registers + MSAP1_WAVEFORM_CONTROL);
	(void)readl(mdev->registers + MSAP1_WAVEFORM_CONTROL);
}

/**
 * msap1_waveform_ioctl() - waveform-specific ioctls.
 * @mfile:    consumer handle.
 * @command:  ioctl request number (transport status is already handled by
 *            the core).
 * @argument: userspace pointer argument.
 *
 * %MSAP1_DMA_IOC_CORRELATE brackets a single latch command between two
 * CLOCK_TAI reads.  Userspace derives the correlation midpoint and treats
 * the bracket width as its uncertainty, so nothing may sleep between the
 * two ktime_get_clocktai_ts64() calls beyond the register accesses
 * themselves.
 *
 * Return: 0 on success, -ENOTTY for unknown requests, -EFAULT on a bad
 * destination buffer.
 */
static long msap1_waveform_ioctl(struct msap1_dma_file *mfile,
				 unsigned int command, unsigned long argument)
{
	struct msap1_dma_device *mdev = mfile->mdev;
	struct msap1_dma_correlation correlation;
	struct msap1_dma_frequency_10s_boundary frequency_boundary;
	struct msap1_dma_ten_minute_boundary boundary;
	struct timespec64 before;
	struct timespec64 after;
	u32 control;
	u32 previous_control;

	if (command == MSAP1_DMA_IOC_SET_FREQUENCY_10S_BOUNDARY) {
		if (copy_from_user(&frequency_boundary, (void __user *)argument,
				   sizeof(frequency_boundary)))
			return -EFAULT;
		if (frequency_boundary.reserved != 0U ||
		    (frequency_boundary.flags &
		     ~MSAP1_WAVEFORM_FREQUENCY_10S_REQUEST_FLAGS) != 0U)
			return -EINVAL;
		if ((frequency_boundary.flags & MSAP1_DMA_FREQUENCY_10S_CANCEL) !=
		    0U &&
		    (frequency_boundary.flags != MSAP1_DMA_FREQUENCY_10S_CANCEL ||
		     frequency_boundary.start_sample_index != 0U ||
		     frequency_boundary.end_sample_index != 0U ||
		     frequency_boundary.utc_start_nanoseconds != 0U ||
		     frequency_boundary.utc_end_nanoseconds != 0U ||
		     frequency_boundary.utc_uncertainty_nanoseconds != 0U ||
		     frequency_boundary.measured_sample_rate_millihz != 0U ||
		     frequency_boundary.boundary_generation != 0U ||
		     frequency_boundary.profile != 0U))
			return -EINVAL;

		previous_control = readl(mdev->registers +
			MSAP1_WAVEFORM_FREQUENCY_10S_CONTROL);
		msap1_waveform_write_u64(mdev->registers,
			MSAP1_WAVEFORM_FREQUENCY_10S_START_SAMPLE_LO,
			frequency_boundary.start_sample_index);
		msap1_waveform_write_u64(mdev->registers,
			MSAP1_WAVEFORM_FREQUENCY_10S_END_SAMPLE_LO,
			frequency_boundary.end_sample_index);
		msap1_waveform_write_u64(mdev->registers,
			MSAP1_WAVEFORM_FREQUENCY_10S_UTC_START_LO,
			frequency_boundary.utc_start_nanoseconds);
		msap1_waveform_write_u64(mdev->registers,
			MSAP1_WAVEFORM_FREQUENCY_10S_UTC_END_LO,
			frequency_boundary.utc_end_nanoseconds);
		msap1_waveform_write_u64(mdev->registers,
			MSAP1_WAVEFORM_FREQUENCY_10S_UNCERTAINTY_LO,
			frequency_boundary.utc_uncertainty_nanoseconds);
		writel(frequency_boundary.measured_sample_rate_millihz,
		       mdev->registers +
			MSAP1_WAVEFORM_FREQUENCY_10S_RATE_MILLIHZ);
		writel(frequency_boundary.boundary_generation,
		       mdev->registers + MSAP1_WAVEFORM_FREQUENCY_10S_GENERATION);
		writel(frequency_boundary.profile,
		       mdev->registers + MSAP1_WAVEFORM_FREQUENCY_10S_PROFILE);
		control = frequency_boundary.flags &
			MSAP1_WAVEFORM_FREQUENCY_10S_RETAINED_FLAGS;
		if (frequency_boundary.flags & MSAP1_DMA_FREQUENCY_10S_CANCEL)
			control |= MSAP1_WAVEFORM_FREQUENCY_10S_CANCEL;
		writel(control | MSAP1_WAVEFORM_FREQUENCY_10S_COMMIT,
		       mdev->registers + MSAP1_WAVEFORM_FREQUENCY_10S_CONTROL);
		control = readl(mdev->registers +
			MSAP1_WAVEFORM_FREQUENCY_10S_CONTROL);
		if (!msap1_waveform_frequency_10s_matches(mdev->registers,
				&frequency_boundary, control, previous_control))
			return -EIO;

		frequency_boundary.observer_status = readl(mdev->registers +
			MSAP1_WAVEFORM_FREQUENCY_10S_OBSERVER_STATUS);
		frequency_boundary.completed_count = readl(mdev->registers +
			MSAP1_WAVEFORM_FREQUENCY_10S_COMPLETED);
		frequency_boundary.dropped_count = readl(mdev->registers +
			MSAP1_WAVEFORM_FREQUENCY_10S_DROPPED);
		frequency_boundary.overflow_count = readl(mdev->registers +
			MSAP1_WAVEFORM_FREQUENCY_10S_OVERFLOW);
		frequency_boundary.discontinuity_count = readl(mdev->registers +
			MSAP1_WAVEFORM_FREQUENCY_10S_DISCONTINUITY);
		if (copy_to_user((void __user *)argument, &frequency_boundary,
				 sizeof(frequency_boundary)))
			return -EFAULT;
		return 0;
	}

	if (command == MSAP1_DMA_IOC_SET_TEN_MINUTE_BOUNDARY) {
		if (copy_from_user(&boundary, (void __user *)argument,
				   sizeof(boundary)))
			return -EFAULT;
		if (boundary.reserved != 0U || boundary.valid > 1U)
			return -EINVAL;

		writel(lower_32_bits(boundary.target_sample_index),
		       mdev->registers + MSAP1_WAVEFORM_TEN_MINUTE_TARGET_LO);
		writel(upper_32_bits(boundary.target_sample_index),
		       mdev->registers + MSAP1_WAVEFORM_TEN_MINUTE_TARGET_LO + 4U);
		control = boundary.valid ? MSAP1_WAVEFORM_TEN_MINUTE_VALID : 0U;
		writel(control | MSAP1_WAVEFORM_TEN_MINUTE_COMMIT,
		       mdev->registers + MSAP1_WAVEFORM_TEN_MINUTE_CONTROL);
		control = readl(mdev->registers +
				MSAP1_WAVEFORM_TEN_MINUTE_CONTROL);

		if (msap1_waveform_read_u64(mdev->registers,
				MSAP1_WAVEFORM_TEN_MINUTE_TARGET_LO) !=
				boundary.target_sample_index ||
		    !!(control & MSAP1_WAVEFORM_TEN_MINUTE_VALID) !=
				!!boundary.valid)
			return -EIO;
		return 0;
	}

	if (command != MSAP1_DMA_IOC_CORRELATE)
		return -ENOTTY;

	ktime_get_clocktai_ts64(&before);
	/* LATCH is a write command, not retained PL state; keep ENABLE set. */
	writel(MSAP1_WAVEFORM_CONTROL_ENABLE | MSAP1_WAVEFORM_CONTROL_LATCH,
	       mdev->registers + MSAP1_WAVEFORM_CONTROL);
	(void)readl(mdev->registers + MSAP1_WAVEFORM_CONTROL);
	correlation.pl_tick = msap1_waveform_read_u64(mdev->registers,
		MSAP1_WAVEFORM_LATCHED_TICK_LO);
	correlation.frame_sequence = msap1_waveform_read_u64(mdev->registers,
		MSAP1_WAVEFORM_LATCHED_SEQUENCE_LO);
	ktime_get_clocktai_ts64(&after);
	correlation.tai_before_nanoseconds = timespec64_to_ns(&before);
	correlation.tai_after_nanoseconds = timespec64_to_ns(&after);
	if (copy_to_user((void __user *)argument, &correlation,
			 sizeof(correlation)))
		return -EFAULT;
	return 0;
}

/**
 * Waveform transport personality.
 *
 * One DMA period carries exactly one WFM1 block.  The variant owns the
 * register bank (needs_registers), the producer lifecycle (arm/disarm) and
 * the correlation ioctl; everything else is the shared core.
 */
const struct msap1_dma_variant msap1_dma_waveform_variant = {
	.device_name = "msap1-waveform",
	.period_bytes = MSAP1_WAVEFORM_BLOCK_BYTES,
	.ring_periods = MSAP1_WAVEFORM_RING_BLOCKS,
	.needs_registers = true,
	.arm = msap1_waveform_arm,
	.disarm = msap1_waveform_disarm,
	.ioctl = msap1_waveform_ioctl,
};
