// SPDX-License-Identifier: GPL-2.0-only
/**
 * @file msap1_dma_waveform.c
 * @brief Raw-waveform DMA transport personality (/dev/msap1-waveform).
 *
 * This personality owns only the WFM1 DMA producer lifecycle. Metrology time
 * correlation and interval scheduling are isolated behind /dev/meter-time.
 */

#include <linux/io.h>
#include <linux/types.h>

#include "msap1_dma.h"

#define MSAP1_WAVEFORM_BLOCK_BYTES 32832U
#define MSAP1_WAVEFORM_RING_BLOCKS 256U
#define MSAP1_WAVEFORM_CONTROL 0x08U
#define MSAP1_WAVEFORM_CONTROL_ENABLE 0x1U

static int msap1_waveform_arm(struct msap1_dma_device *mdev)
{
	writel(MSAP1_WAVEFORM_CONTROL_ENABLE,
	       mdev->registers + MSAP1_WAVEFORM_CONTROL);
	(void)readl(mdev->registers + MSAP1_WAVEFORM_CONTROL);
	return 0;
}

static void msap1_waveform_disarm(struct msap1_dma_device *mdev)
{
	writel(0U, mdev->registers + MSAP1_WAVEFORM_CONTROL);
	(void)readl(mdev->registers + MSAP1_WAVEFORM_CONTROL);
}

const struct msap1_dma_variant msap1_dma_waveform_variant = {
	.device_name = "msap1-waveform",
	.period_bytes = MSAP1_WAVEFORM_BLOCK_BYTES,
	.ring_periods = MSAP1_WAVEFORM_RING_BLOCKS,
	.needs_registers = true,
	.arm = msap1_waveform_arm,
	.disarm = msap1_waveform_disarm,
	.ioctl = NULL,
};
