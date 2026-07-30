SUMMARY = "MSAP1 waveform DMAengine consumer"
DESCRIPTION = "Exposes raw waveform blocks and PL time correlation through /dev/msap1-waveform."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=5d626f0b94ff878b8c4d1d5a03080381"

inherit module

COMPATIBLE_MACHINE = "^msap1$"

SRC_URI = " \
    file://COPYING \
    file://Makefile \
    file://msap1_waveform_dma.c \
"

S = "${WORKDIR}"

KERNEL_MODULE_AUTOLOAD += "msap1_waveform_dma"
