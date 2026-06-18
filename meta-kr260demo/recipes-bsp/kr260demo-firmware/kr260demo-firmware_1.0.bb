SUMMARY = "ZuBoard demo FPGA bitstream and Cortex-R5 firmware"
DESCRIPTION = "Packages the ZuBoard demo PL bitstream and Cortex-R5 firmware ELF files into ${FIRMWARE_INSTALL_DIR} on the target."
LICENSE = "CLOSED"

# -----------------------------------------------------------------------------
# Release selection
#
# To pull a new set of binaries:
#   1. Bump ZUBOARD_RELEASE_TAG to the new GitHub release tag.
#   2. Update the SRC_URI[...sha256sum] values below.
#
# Tip: after changing the tag (or adding an ELF), let bitbake tell you the new
#      checksums:
#        bitbake -c fetch zuboard-firmware
#      it fails on the checksum mismatch and prints the correct
#      SRC_URI[...sha256sum] lines to paste back in here.
#
# Both repos are expected to ship the same tag. If they ever diverge, override
# ZUBOARD_PS_TAG / ZUBOARD_PL_TAG individually.
# -----------------------------------------------------------------------------
ZUBOARD_RELEASE_TAG ?= "v0.0.1"
ZUBOARD_PS_TAG ?= "${ZUBOARD_RELEASE_TAG}"
ZUBOARD_PL_TAG ?= "${ZUBOARD_RELEASE_TAG}"

# Source switch: "cloud" uses GitHub release assets, "local" uses live outputs
# from sibling checkouts.
#
# Flip in local.conf:
#     ZUBOARD_FIRMWARE_SRC = "local"
ZUBOARD_FIRMWARE_SRC ?= "cloud"
ZUBOARD_RPU_LOCAL_DIR ?= "${TOPDIR}/../../ZuBoardDemo_RPU"
ZUBOARD_PL_LOCAL_DIR ?= "${TOPDIR}/../../ZuBoardDemo_PL"
ZUBOARD_PL_LOCAL_FILE ?= "${TOPDIR}/../../runtime-generated/bin_file/fpga.bit"

ZUBOARD_PS_FILES ?= "R5c0.elf R5c1.elf"
ZUBOARD_PL_FILE ?= "fpga.bit"

ZUBOARD_PS_BASEURL = "https://github.com/lesterlo/ZuBoardDemo_PS/releases/download"
ZUBOARD_PL_BASEURL = "https://github.com/lesterlo/ZuBoardDemo_PL/releases/download"

FPGA_FIRMWARE_RELEASE_TAG ?= "${ZUBOARD_RELEASE_TAG}"
FPGA_FIRMWARE_PS_TAG ?= "${ZUBOARD_PS_TAG}"
FPGA_FIRMWARE_PL_TAG ?= "${ZUBOARD_PL_TAG}"
FPGA_FIRMWARE_SRC ?= "${ZUBOARD_FIRMWARE_SRC}"
FPGA_FIRMWARE_PS_FILES ?= "${ZUBOARD_PS_FILES}"
FPGA_FIRMWARE_PL_FILE ?= "${ZUBOARD_PL_FILE}"
FPGA_FIRMWARE_PS_BASEURL ?= "${ZUBOARD_PS_BASEURL}"
FPGA_FIRMWARE_PL_BASEURL ?= "${ZUBOARD_PL_BASEURL}"
FPGA_FIRMWARE_RPU_LOCAL_DIR ?= "${ZUBOARD_RPU_LOCAL_DIR}"
FPGA_FIRMWARE_PL_LOCAL_FILE ?= "${ZUBOARD_PL_LOCAL_FILE}"
FPGA_FIRMWARE_PS_DOWNLOAD_PREFIX ?= "zuboard-ps"
FPGA_FIRMWARE_PL_DOWNLOAD_PREFIX ?= "zuboard-pl"

FIRMWARE_INSTALL_DIR ?= "/opt/monutchee/${MACHINE}/firmware"

# One checksum per remote file. Names: 'fpga' for the bitstream, and the ELF
# file name minus extension (lower-cased) for each PS core.
SRC_URI[fpga.sha256sum] = "7b9288b9d9873ec514c532835c63d70e35c57a6820ecfd4e7f67942f896d6e68"
SRC_URI[r5c0.sha256sum] = "739cd012520970f3ab4e2cc23e7cf0021a84b32c6d5a3c680f9d55a46fa946c6"
SRC_URI[r5c1.sha256sum] = "a2f348a4843bc6e5ff7de2e07a46c4f747c69156f4b1756b8a7071c803394a5e"

COMPATIBLE_MACHINE = "^zudemo$"

inherit fpga-firmware-package
