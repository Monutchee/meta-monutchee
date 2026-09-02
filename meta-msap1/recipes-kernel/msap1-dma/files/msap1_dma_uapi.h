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
 *  - The transport-status ioctl below reports DMA health. Historical time
 *    ioctl numbers remain declared for source compatibility but both DMA
 *    devices return -ENOTTY; new software uses /dev/meter-time.
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
 * Legacy layout retained so old clients still compile. The DMA driver no
 * longer implements this operation; use the neutral meter-time UAPI.
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
 * Legacy layout retained so old clients still compile. Time scheduling is no
 * longer coupled to the waveform register bank.
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

/**
 * struct msap1_dma_frequency_10s_boundary - one UTC ten-second interval.
 *
 * Legacy layout retained so old clients still compile. The dedicated
 * /dev/meter-time endpoint owns the current interval-control ABI.
 *
 * @start_sample_index:          inclusive interval start in the free-running
 *                               conversion sample domain.
 * @end_sample_index:            exclusive interval end in that domain.
 * @utc_start_nanoseconds:       UTC start, aligned to a ten-second boundary.
 * @utc_end_nanoseconds:         UTC end; exactly ten seconds after start.
 * @utc_uncertainty_nanoseconds: combined UTC/correlation uncertainty bound.
 * @measured_sample_rate_millihz: rate used to map UTC into sample indices.
 * @boundary_generation:        non-zero userspace sequence for this tuple.
 * @profile:                    nominal Hz in bits 7:0, reference channel in
 *                               15:8, filter profile in 23:16 and calibration
 *                               profile in 31:24.
 * @flags:                      bit 0 boundary-valid, bit 1 UTC-synchronized;
 *                               bit 2 cancels active/queued tuples and must be
 *                               used alone with an otherwise-zero input tuple.
 * @observer_status:            live PL observer state after the commit.
 * @completed_count:            intervals serialized by the observer.
 * @dropped_count:              overwritten/undeliverable interval count.
 * @overflow_count:             crossing-storage overflow count.
 * @discontinuity_count:        observed conversion discontinuity count.
 * @reserved:                   must be zero.
 */
struct msap1_dma_frequency_10s_boundary {
	__u64 start_sample_index;
	__u64 end_sample_index;
	__u64 utc_start_nanoseconds;
	__u64 utc_end_nanoseconds;
	__u64 utc_uncertainty_nanoseconds;
	__u32 measured_sample_rate_millihz;
	__u32 boundary_generation;
	__u32 profile;
	__u32 flags;
	__u32 observer_status;
	__u32 completed_count;
	__u32 dropped_count;
	__u32 overflow_count;
	__u32 discontinuity_count;
	__u32 reserved;
};

#define MSAP1_DMA_FREQUENCY_10S_BOUNDARY_VALID (1U << 0)
#define MSAP1_DMA_FREQUENCY_10S_TIME_SYNCHRONIZED (1U << 1)
#define MSAP1_DMA_FREQUENCY_10S_CANCEL (1U << 2)

/** Ioctl magic shared by both acquisition devices ('W' predates the merge). */
#define MSAP1_DMA_IOC_MAGIC 'W'

/**
 * MSAP1_DMA_IOC_CORRELATE - latch and return one PL/TAI correlation sample.
 *
 * Reserved legacy request. Both DMA devices return -ENOTTY; use
 * METER_TIME_IOC_CORRELATE on /dev/meter-time.
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
 * Reserved legacy request. Both DMA devices return -ENOTTY.
 */
#define MSAP1_DMA_IOC_SET_TEN_MINUTE_BOUNDARY \
	_IOW(MSAP1_DMA_IOC_MAGIC, 0x03, \
	     struct msap1_dma_ten_minute_boundary)

/**
 * MSAP1_DMA_IOC_SET_FREQUENCY_10S_BOUNDARY - commit one coherent interval.
 *
 * Reserved legacy request. Both DMA devices return -ENOTTY.
 */
#define MSAP1_DMA_IOC_SET_FREQUENCY_10S_BOUNDARY \
	_IOWR(MSAP1_DMA_IOC_MAGIC, 0x04, \
	      struct msap1_dma_frequency_10s_boundary)

#endif /* MSAP1_DMA_UAPI_H */
