// SPDX-License-Identifier: GPL-2.0-only
/**
 * @file msap1_dma_meter.c
 * @brief Meter-result personality of the MSAP1 DMA transport (/dev/msap1-meter).
 *
 * The PL MeterProcessing block computes RMS and frequency over a configurable
 * window (default 6400 frames = 200 ms at 32 kSPS) and emits one fixed
 * 256-byte MTR1 result record per window through its own AXI4-Stream / AXI
 * DMA S2MM pair.  This variant only sets the transport geometry; the meter
 * node carries no register bank, and capture start/stop is owned by the R5
 * core over RPMsg, so no arm/disarm hooks are needed.
 */

#include <linux/types.h>

#include "msap1_dma.h"

/** Size of one MTR1 result record, fixed by the PL packetizer format. */
#define MSAP1_METER_RECORD_BYTES 256U

/**
 * Ring depth in records.
 *
 * Sizing history: the Xilinx AXI DMAengine driver does not raise a cyclic
 * callback until the final hardware descriptor of the ring has completed
 * once (see msap1_dma_period_complete()).  An early 64-record ring therefore
 * delayed the first record by one full ring revolution — 12.8 seconds at the
 * 200 ms result cadence — and was shrunk to 2 records as a workaround.
 *
 * With the shared core's safe-window accounting, a deeper ring is safe
 * again, and 4 records buys real robustness: the 3-record safe window
 * tolerates ~600 ms of consumer stall (for example, transient userspace
 * scheduling latency) at the cost of first-record latency of
 * 4 x cadence (~800 ms at the default window) after capture start.  Both
 * figures scale with the configured RMS window.
 */
#define MSAP1_METER_RING_RECORDS 4U

/**
 * Meter transport personality.
 *
 * One DMA period carries exactly one MTR1 record, so read() returns whole
 * records and userspace never reassembles across read boundaries.
 */
const struct msap1_dma_variant msap1_dma_meter_variant = {
	.device_name = "msap1-meter",
	.period_bytes = MSAP1_METER_RECORD_BYTES,
	.ring_periods = MSAP1_METER_RING_RECORDS,
};
