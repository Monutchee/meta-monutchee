/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * @file msap1_dma.h
 * @brief Internal interfaces of the MSAP1 acquisition DMA transport module.
 *
 * The module drives two PL AXI DMA S2MM streams with one shared transport
 * core (msap1_dma_core.c).  Everything device-specific — period geometry,
 * register-bank access and extra ioctls — lives behind one variant
 * descriptor per compatible string:
 *
 *   "monutchee,msap1-meter-dma"    -> msap1_dma_meter_variant
 *                                     (msap1_dma_meter.c, /dev/msap1-meter)
 *   "monutchee,msap1-waveform-dma" -> msap1_dma_waveform_variant
 *                                     (msap1_dma_waveform.c,
 *                                      /dev/msap1-waveform)
 *
 * The core owns the parts that are hard to get right twice: per-period
 * completion detection, the safe-window arithmetic that keeps the active
 * DMA period out of userspace, overrun accounting and the staging copy in
 * read().  Variants stay declarative and small.
 */
#ifndef MSAP1_DMA_H
#define MSAP1_DMA_H

#include <linux/atomic.h>
#include <linux/miscdevice.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "msap1_dma_uapi.h"

struct dma_chan;
struct msap1_dma_device;
struct msap1_dma_file;

/**
 * struct msap1_dma_variant - per-compatible personality of the transport core.
 *
 * A variant parameterizes the shared core; it holds no runtime state.  All
 * hooks are optional (NULL means "not applicable to this device").
 *
 * @device_name:    node name registered under /dev (also used in log lines).
 * @period_bytes:   size of one DMA period.  This is the unit of read():
 *                  one MTR1 record or one WFM1 block, fixed by the PL
 *                  packetizer format.
 * @ring_periods:   cyclic ring capacity in periods.  Must be at least 2;
 *                  one period is always reserved for the active DMA write,
 *                  so userspace lag tolerance is @ring_periods - 1 periods.
 * @needs_registers: true when the device-tree node carries an AXI-Lite
 *                  register bank to ioremap into msap1_dma_device.registers.
 * @arm:            start the PL-side producer.  Called in open() strictly
 *                  after dma_async_issue_pending() so the stream never
 *                  starts before the DMA can accept it.  Failure aborts
 *                  open().
 * @disarm:         stop the PL-side producer.  Called in release() and at
 *                  driver remove, strictly before terminating the DMA.
 * @ioctl:          variant-specific ioctls.  The core handles
 *                  %MSAP1_DMA_IOC_TRANSPORT_STATUS itself and delegates
 *                  every other request here; return -ENOTTY for unknown
 *                  requests.
 */
struct msap1_dma_variant {
	const char *device_name;
	u32 period_bytes;
	u32 ring_periods;
	bool needs_registers;
	int (*arm)(struct msap1_dma_device *mdev);
	void (*disarm)(struct msap1_dma_device *mdev);
	long (*ioctl)(struct msap1_dma_file *mfile, unsigned int command,
		      unsigned long argument);
};

/**
 * struct msap1_dma_device - one probed acquisition transport device.
 *
 * Lifetime: allocated in probe(), released through devres.  The DMA ring is
 * armed in open() and torn down in release(); the file lifetime is the DMA
 * lifetime.
 *
 * @dev:       backing platform device.
 * @variant:   personality selected by the device-tree compatible.
 * @rx:        AXI DMA S2MM channel ("rx" in the device tree).
 * @ring:      coherent cyclic DMA ring of
 *             @variant->ring_periods * @variant->period_bytes bytes.
 * @ring_dma:  bus address of @ring.
 * @registers: AXI-Lite register bank, or NULL when the variant has none.
 * @misc:      character-device registration (mode 0660, dynamic minor).
 * @wait:      wakes readers and pollers on completion interrupts.
 * @callbacks: cyclic completion callbacks observed since open().  This is a
 *             DIAGNOSTIC ONLY and deliberately not used for accounting: the
 *             vendor driver invokes the cyclic callback once per interrupt,
 *             and coalesces (tasklet_schedule() is idempotent), so it
 *             under-counts whenever two periods complete close together.
 *             Period availability comes from the ring markers instead
 *             (see msap1_dma_period_ready()).
 * @opened:    single-open guard; concurrent opens fail with -EBUSY because
 *             each open would re-arm the one underlying DMA channel.
 */
struct msap1_dma_device {
	struct device *dev;
	const struct msap1_dma_variant *variant;
	struct dma_chan *rx;
	void *ring;
	dma_addr_t ring_dma;
	void __iomem *registers;
	struct miscdevice misc;
	wait_queue_head_t wait;
	atomic64_t callbacks;
	atomic_t opened;
};

/**
 * struct msap1_dma_file - per-open consumer state.
 *
 * @mdev:            device this handle reads from.
 * @consumed:        absolute count of periods delivered to userspace.
 * @overrun_periods: periods skipped because the consumer fell out of the
 *                   safe window; reported via
 *                   %MSAP1_DMA_IOC_TRANSPORT_STATUS.
 * @staging:         one-period bounce buffer.  Completed periods are
 *                   memcpy()d here before copy_to_user(), which may fault
 *                   and sleep; copying directly out of the live ring would
 *                   let the producer overwrite the period mid-copy.
 */
struct msap1_dma_file {
	struct msap1_dma_device *mdev;
	u64 consumed;
	u64 overrun_periods;
	void *staging;
};

/**
 * msap1_dma_ring_bytes() - total size of a variant's cyclic DMA ring.
 * @variant: variant to size.
 *
 * Return: ring size in bytes.
 */
static inline u32 msap1_dma_ring_bytes(const struct msap1_dma_variant *variant)
{
	return variant->period_bytes * variant->ring_periods;
}

extern const struct msap1_dma_variant msap1_dma_meter_variant;
extern const struct msap1_dma_variant msap1_dma_waveform_variant;

#endif /* MSAP1_DMA_H */
