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
 * @produced_blocks: periods the DMA engine has completed since open(), as
 *                   observed in the ring itself (completion markers), not
 *                   inferred from completion callbacks.
 * @consumed_blocks: periods this file handle has delivered to userspace.
 * @overrun_blocks:  kernel-transport loss EVENTS, not lost periods.  A
 *                   period left unfinishable by a short packet counts one
 *                   (exact), but userspace falling a whole ring behind the
 *                   producer also counts one however many periods the lap
 *                   actually destroyed — the markers prove loss happened,
 *                   not how much (observed in the field: ~49 blocks lost
 *                   per increment, 2026-08-17).  Derive true loss from
 *                   payload sequence numbers; PL-side loss is likewise not
 *                   included and detected the same way.
 * @ring_blocks:     effective ring capacity in periods (the device-tree
 *                   "monutchee,ring-blocks" property when present, else the
 *                   driver default).  Userspace can consume at most
 *                   @ring_blocks - 1 completed periods per revolution; one
 *                   period is always reserved for the active DMA write.
 * @callbacks:       cyclic completion callbacks the driver has received.
 *                   DIAGNOSTIC ONLY, never used for accounting: the Xilinx
 *                   AXI DMA driver delivers at most one cyclic callback per
 *                   tasklet run and the hardware's IOC is a latched status
 *                   bit rather than a counter, so this lags the true period
 *                   count whenever periods complete close together.
 *                   @produced_blocks - @callbacks is exactly that deficit,
 *                   and is the measurement that distinguishes callback
 *                   coalescing from other transport faults.
 */
struct msap1_dma_transport_status {
	__u64 produced_blocks;
	__u64 consumed_blocks;
	__u64 overrun_blocks;
	__u32 ring_blocks;
	__u32 callbacks;
};

/**
 * struct msap1_dma_ten_minute_boundary - next UTC-aligned aggregation close.
 *
 * Userspace derives @target_sample_index from an atomic PL/CLOCK_TAI
 * correlation and the next UTC ten-minute boundary.  The waveform register
 * bank is also the system timebase-control bank, so this ioctl is intentionally
 * exposed on /dev/msap1-waveform rather than the meter DMA device.
 *
 * @target_sample_index: first sample index at or after the desired UTC
 *                       boundary.
 * @valid:               one enables the target; zero invalidates it.
 * @reserved:            must be zero.
 */
struct msap1_dma_ten_minute_boundary {
	__u64 target_sample_index;
	__u32 valid;
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

/**
 * MSAP1_DMA_IOC_SET_TEN_MINUTE_BOUNDARY - program the next PL close target.
 *
 * Waveform device only.  The driver commits the 64-bit target atomically to
 * PL and verifies the active readback before returning success.
 */
#define MSAP1_DMA_IOC_SET_TEN_MINUTE_BOUNDARY \
	_IOW(MSAP1_DMA_IOC_MAGIC, 0x03, \
	     struct msap1_dma_ten_minute_boundary)

#endif /* MSAP1_DMA_UAPI_H */
