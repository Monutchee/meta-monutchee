/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/**
 * @file msap1_dma_uapi.h
 * @brief Userspace ABI of the MSAP1 acquisition DMA transport devices.
 *
 * This header describes everything userspace may rely on when talking to
 * /dev/msap1-meter and /dev/msap1-waveform:
 *
 *  - read() returns only whole transport periods (one 256-byte MTR1 record
 *    on the meter device, one 32832-byte WFM1 block on the waveform device).
 *    A read() size smaller than one period fails with -EINVAL.
 *  - poll()/select() signal EPOLLIN when at least one completed period is
 *    available.
 *  - The ioctls below report transport health and, on the waveform device,
 *    correlate the PL timebase with CLOCK_TAI.
 *
 * ABI stability: the structure layouts and ioctl request numbers predate the
 * consolidation of the meter and waveform drivers into one module and are
 * mirrored by the acquisition daemon.  They are frozen; extend the ABI by
 * adding new ioctl numbers, never by changing these.
 */
#ifndef MSAP1_DMA_UAPI_H
#define MSAP1_DMA_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * struct msap1_dma_correlation - one atomic PL/TAI timebase correlation.
 *
 * Produced by %MSAP1_DMA_IOC_CORRELATE.  The driver reads CLOCK_TAI, latches
 * the PL tick counter and frame sequence with a single register write, then
 * reads CLOCK_TAI again.  Userspace derives the correlation midpoint and its
 * uncertainty from the two bracketing timestamps.
 *
 * @tai_before_nanoseconds: CLOCK_TAI immediately before the PL latch write.
 * @tai_after_nanoseconds:  CLOCK_TAI immediately after the latched values
 *                          were read back.
 * @pl_tick:                free-running PL tick counter captured by the latch.
 * @frame_sequence:         ADC frame sequence number captured by the latch.
 */
struct msap1_dma_correlation {
	__u64 tai_before_nanoseconds;
	__u64 tai_after_nanoseconds;
	__u64 pl_tick;
	__u64 frame_sequence;
};

/**
 * struct msap1_dma_transport_status - kernel-side transport accounting.
 *
 * Produced by %MSAP1_DMA_IOC_TRANSPORT_STATUS.  Counters are absolute since
 * open(); "block" is the historical UAPI name for one DMA period (one MTR1
 * record on the meter device, one WFM1 block on the waveform device).
 *
 * @produced_blocks: periods the DMA engine has completed since open().
 * @consumed_blocks: periods this file handle has delivered to userspace.
 * @overrun_blocks:  periods lost in the kernel transport because userspace
 *                   fell more than one ring behind the producer.  PL-side
 *                   loss is not included; compare payload sequence numbers
 *                   to detect it.  The initial phase-synchronization discard
 *                   after open() is deliberately not counted.
 * @ring_blocks:     ring capacity in periods.  Userspace can consume at most
 *                   @ring_blocks - 1 completed periods per revolution; one
 *                   period is always reserved for the active DMA write.
 * @reserved:        always zero; pads the structure to 32 bytes.
 */
struct msap1_dma_transport_status {
	__u64 produced_blocks;
	__u64 consumed_blocks;
	__u64 overrun_blocks;
	__u32 ring_blocks;
	__u32 reserved;
};

/** Ioctl magic shared by both acquisition devices ('W' predates the merge). */
#define MSAP1_DMA_IOC_MAGIC 'W'

/**
 * MSAP1_DMA_IOC_CORRELATE - latch and return one PL/TAI correlation sample.
 *
 * Waveform device only; the meter device returns -ENOTTY because only the
 * waveform PL peripheral exposes the latch register bank.
 */
#define MSAP1_DMA_IOC_CORRELATE \
	_IOR(MSAP1_DMA_IOC_MAGIC, 0x01, struct msap1_dma_correlation)

/**
 * MSAP1_DMA_IOC_TRANSPORT_STATUS - return transport counters for this handle.
 *
 * Supported by both devices.
 */
#define MSAP1_DMA_IOC_TRANSPORT_STATUS \
	_IOR(MSAP1_DMA_IOC_MAGIC, 0x02, struct msap1_dma_transport_status)

#endif /* MSAP1_DMA_UAPI_H */
