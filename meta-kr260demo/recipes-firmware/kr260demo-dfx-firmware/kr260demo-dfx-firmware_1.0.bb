SUMMARY = "KR260Demo dfx-mgr PL application and RPU firmware"
DESCRIPTION = "Packages the KR260Demo programmable-logic full bitstream, \
lopper-generated PL overlay, and Cortex-R5 firmware into the dfx-mgr firmware \
layout."
LICENSE = "CLOSED"

inherit xilinx-dfx-firmware

XILINX_DFX_APP_NAME = "kr260demo"

# Backward compatible local.conf switch from the old product firmware recipe.
KR260DEMO_DFX_AUTOLOAD ?= "0"
XILINX_DFX_AUTOLOAD ?= "${KR260DEMO_DFX_AUTOLOAD}"

XILINX_DFX_RPU_BASE ?= "kr260demo-rpu"
XILINX_DFX_RPU_LOAD_NAMES ?= "R5c0 R5c1"
XILINX_DFX_RPU_ELFS = "0:R5c0.elf 1:R5c1.elf"

# Inputs taken straight from the workspace / gen-machineconf output.
FILESEXTRAPATHS:prepend := "${TOPDIR}/conf/dts/kr260demo/pl-overlay-full:${TOPDIR}/../../runtime-generated/vivado_SDT_out:${TOPDIR}/../../KR260Demo_RPU/R5c0/build:${TOPDIR}/../../KR260Demo_RPU/R5c1/build:"

SRC_URI = " \
    file://KR260Demo_PL.bit \
    file://pl.dtso \
    file://R5c0.elf \
    file://R5c1.elf \
"

do_install[vardeps] += "KR260DEMO_DFX_AUTOLOAD"
