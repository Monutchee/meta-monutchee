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
 * Ring depth in blocks (~2 MiB of coherent memory).
 *
 * At the 32 kframe/s base rate one block completes every 32 ms, so the
 * 63-block safe window gives the daemon ~2 s of stall tolerance; ~0.5 s
 * remains at the 128 kframe/s maximum rate.  The Xilinx first-callback
 * behaviour (see msap1_dma_period_complete()) delays the first readable
 * block by one ring revolution after arming.
 */
#define MSAP1_WAVEFORM_RING_BLOCKS 64U

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
	struct timespec64 before;
	struct timespec64 after;

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
