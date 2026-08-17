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
 * Depth buys consumer-stall tolerance (ring - 1 periods) and, since the
 * completion markers landed, costs nothing else.
 *
 * Sizing history: the Xilinx AXI DMAengine driver does not raise a cyclic
 * callback until the final hardware descriptor of the ring has completed
 * once.  While the core derived period availability from that callback, a
 * deep ring blacked out the stream for one full revolution after capture
 * start — 12.8 s at 64 records and the 200 ms cadence — which is why an
 * early 64-record ring was shrunk to 2, then grown back only as far as 8.
 *
 * That coupling is gone: availability now comes from the ring's completion
 * markers (see msap1_dma_period_ready()), so a period is visible as soon as
 * the DMA has written it, whatever the depth and whatever the callback
 * does.  Startup latency is one period plus the consumer's poll interval at
 * any depth; steady-state latency was never depth-dependent.
 *
 * Depth therefore answers one question only: how long may the consumer
 * stall before data is lost.  A 4-record ring's ~600 ms proved too small in
 * the field (a slow-storage episode stalled the daemon's synchronous
 * publish and overran the ring once per aggregate window for 40 minutes),
 * and 8 records' ~1.4 s is not much better.  64 records tolerates ~12.6 s
 * at the default window for 16 KiB of coherent memory.  Deeper is possible
 * (the device-tree "monutchee,ring-blocks" property overrides this default)
 * but starts to mask a chronically slow consumer: overrun_blocks is the
 * early warning, and it should stay able to fire.  All figures scale with
 * the configured RMS window.
 */
#define MSAP1_METER_RING_RECORDS 64U

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
