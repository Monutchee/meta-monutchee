SUMMARY = "MSAP1 dfx-mgr PL application and RPU firmware"
DESCRIPTION = "Packages the MSAP1 full bitstream, generated PL overlay, and Cortex-R5 firmware."
LICENSE = "CLOSED"

inherit xilinx-dfx-firmware

COMPATIBLE_MACHINE = "^msap1$"

XILINX_DFX_APP_NAME = "msap1"
XILINX_DFX_RPU_BASE ?= "msap1-rpu"
XILINX_DFX_RPU_LOAD_NAMES ?= "R5c0 R5c1"
XILINX_DFX_RPU_ELFS = "0:R5c0.elf 1:R5c1.elf"

FILESEXTRAPATHS:prepend := "${TOPDIR}/conf/dts/msap1/pl-overlay-full:${TOPDIR}/../../runtime-generated/vivado_SDT_out:"

SRC_URI = " \
    file://MSAP1_PL.bit \
    file://pl.dtso \
    file://R5c0.elf \
    file://R5c1.elf \
"
