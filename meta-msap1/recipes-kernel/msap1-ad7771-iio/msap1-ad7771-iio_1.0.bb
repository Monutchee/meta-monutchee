SUMMARY = "MSAP1 AD7771 IIO DMAengine consumer"
DESCRIPTION = "Exposes the PL AD7771 AXI stream as eight buffered IIO channels backed by AXI DMA S2MM."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=4b2d56893c9ae93b3da601e265cb9b29"

inherit module

COMPATIBLE_MACHINE = "^msap1$"

SRC_URI = " \
    file://COPYING \
    file://Makefile \
    file://msap1_ad7771_iio.c \
"

S = "${WORKDIR}"

KERNEL_MODULE_AUTOLOAD += "msap1_ad7771_iio"
