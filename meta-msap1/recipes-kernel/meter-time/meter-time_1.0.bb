SUMMARY = "Meter time-control platform driver"
DESCRIPTION = "Isolated PL time correlation and aggregation-boundary control through /dev/meter-time."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://COPYING;md5=53cbd03cc56142008ba1bda05f2ecea2"

inherit module

COMPATIBLE_MACHINE = "^msap1$"

SRC_URI = " \
    file://COPYING;subdir=src \
    file://Makefile;subdir=src \
    file://meter_time_uapi.h;subdir=src \
    file://meter_time.c;subdir=src \
"

S = "${WORKDIR}/src"

KERNEL_MODULE_AUTOLOAD += "meter_time"
