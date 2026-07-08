SUMMARY = "ZUBoard dfx-mgr PL application and RPU firmware"
DESCRIPTION = "Packages the ZUBoard programmable-logic bitstream and \
Cortex-R5 firmware into the dfx-mgr firmware layout."
LICENSE = "CLOSED"

inherit xilinx-dfx-firmware

XILINX_DFX_APP_NAME = "zudemo"
XILINX_DFX_AUTOLOAD ?= "0"
XILINX_DFX_RPU_BASE ?= "zudemo-rpu"
XILINX_DFX_RPU_LOAD_NAMES ?= "R5c0 R5c1"
XILINX_DFX_RPU_ELFS = "0:R5c0.elf 1:R5c1.elf"

# Source switch: "cloud" uses release assets, "local" packages sibling build
# outputs from the developer workspace.
ZUDEMO_DFX_SRC ?= "cloud"
ZUDEMO_DFX_RELEASE_TAG ?= "v0.0.1"
ZUDEMO_DFX_PS_BASEURL ?= "https://github.com/lesterlo/ZuBoardDemo_PS/releases/download"
ZUDEMO_DFX_PL_BASEURL ?= "https://github.com/lesterlo/ZuBoardDemo_PL/releases/download"

ZUDEMO_DFX_SRC_URI_cloud = " \
    ${ZUDEMO_DFX_PL_BASEURL}/${ZUDEMO_DFX_RELEASE_TAG}/fpga.bit;name=fpga;subdir=${BPN} \
    ${ZUDEMO_DFX_PS_BASEURL}/${ZUDEMO_DFX_RELEASE_TAG}/R5c0.elf;name=r5c0;subdir=${BPN} \
    ${ZUDEMO_DFX_PS_BASEURL}/${ZUDEMO_DFX_RELEASE_TAG}/R5c1.elf;name=r5c1;subdir=${BPN} \
"

ZUDEMO_DFX_SRC_URI_local = " \
    file://fpga.bit \
    file://R5c0.elf \
    file://R5c1.elf \
"

# Live workspace outputs used when ZUDEMO_DFX_SRC = "local".
FILESEXTRAPATHS:prepend := "${TOPDIR}/../../runtime-generated/bin_file:${TOPDIR}/../../ZuBoardDemo_RPU/R5c0/build:${TOPDIR}/../../ZuBoardDemo_RPU/R5c1/build:"
FW_DIR = "${BPN}"

SRC_URI = "${@d.getVar('ZUDEMO_DFX_SRC_URI_' + (d.getVar('ZUDEMO_DFX_SRC') or 'cloud'))}"

SRC_URI[fpga.sha256sum] = "7b9288b9d9873ec514c532835c63d70e35c57a6820ecfd4e7f67942f896d6e68"
SRC_URI[r5c0.sha256sum] = "739cd012520970f3ab4e2cc23e7cf0021a84b32c6d5a3c680f9d55a46fa946c6"
SRC_URI[r5c1.sha256sum] = "a2f348a4843bc6e5ff7de2e07a46c4f747c69156f4b1756b8a7071c803394a5e"
