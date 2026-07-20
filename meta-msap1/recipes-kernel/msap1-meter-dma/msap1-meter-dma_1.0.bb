SUMMARY = "MSAP1 meter DMAengine consumer"
DESCRIPTION = "Exposes fixed PL meter records through /dev/msap1-meter using AXI DMA S2MM."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=53cbd03cc56142008ba1bda05f2ecea2"

inherit module

COMPATIBLE_MACHINE = "^msap1$"

SRC_URI = " \
    file://COPYING \
    file://Makefile \
    file://msap1_meter_dma.c \
"

S = "${WORKDIR}"

KERNEL_MODULE_AUTOLOAD += "msap1_meter_dma"
