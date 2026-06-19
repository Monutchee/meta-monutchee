# ZuBoard-specific assets for the shared zynqmp-firmware recipe.
#
# To pull a new set of binaries:
#   1. Bump FPGA_FIRMWARE_RELEASE_TAG to the new GitHub release tag.
#   2. Update the SRC_URI[...sha256sum] values below.
#
# Tip: after changing the tag (or adding an ELF), let bitbake print the new
#      checksums:
#        bitbake -c fetch zynqmp-firmware
#      it fails on the checksum mismatch and prints the correct
#      SRC_URI[...sha256sum] lines to paste back in here.
#
# Both repos are expected to ship the same tag. If they ever diverge, override
# FPGA_FIRMWARE_PS_TAG / FPGA_FIRMWARE_PL_TAG individually.

FPGA_FIRMWARE_RELEASE_TAG = "v0.0.1"

FPGA_FIRMWARE_PS_BASEURL = "https://github.com/lesterlo/ZuBoardDemo_PS/releases/download"
FPGA_FIRMWARE_PL_BASEURL = "https://github.com/lesterlo/ZuBoardDemo_PL/releases/download"
FPGA_FIRMWARE_PS_DOWNLOAD_PREFIX = "zuboard-ps"
FPGA_FIRMWARE_PL_DOWNLOAD_PREFIX = "zuboard-pl"

# Live workspace outputs used when FPGA_FIRMWARE_SRC = "local".
FPGA_FIRMWARE_RPU_LOCAL_DIR = "${TOPDIR}/../../ZuBoardDemo_RPU"
FPGA_FIRMWARE_PL_LOCAL_FILE = "${TOPDIR}/../../runtime-generated/bin_file/fpga.bit"

# One checksum per remote file. Names: 'fpga' for the bitstream, and the ELF
# file name minus extension (lower-cased) for each PS core.
SRC_URI[fpga.sha256sum] = "7b9288b9d9873ec514c532835c63d70e35c57a6820ecfd4e7f67942f896d6e68"
SRC_URI[r5c0.sha256sum] = "739cd012520970f3ab4e2cc23e7cf0021a84b32c6d5a3c680f9d55a46fa946c6"
SRC_URI[r5c1.sha256sum] = "a2f348a4843bc6e5ff7de2e07a46c4f747c69156f4b1756b8a7071c803394a5e"

# Activate the shared recipe for the ZuBoard machine.
COMPATIBLE_MACHINE = "^zudemo$"
