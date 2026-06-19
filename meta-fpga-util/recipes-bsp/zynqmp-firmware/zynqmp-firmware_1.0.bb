SUMMARY = "ZynqMP demo FPGA bitstream and Cortex-R5 firmware"
DESCRIPTION = "Packages a Xilinx/AMD ZynqMP PL bitstream and Cortex-R5 (RPU) \
firmware ELF files into ${FIRMWARE_INSTALL_DIR} on the target. Shared by every \
ZynqMP board layer (meta-zuboard, meta-kr260demo, ...); each board supplies its \
own asset URLs, local workspace paths and checksums through a \
zynqmp-firmware_%.bbappend in its own layer."
LICENSE = "CLOSED"

# -----------------------------------------------------------------------------
# This is the shared, board-neutral recipe. It carries only the ZynqMP defaults
# that every demo board has in common. All board-specific data lives in the
# board layer's .bbappend, which sets:
#
#   FPGA_FIRMWARE_RELEASE_TAG                              release tag
#   FPGA_FIRMWARE_PS_BASEURL / FPGA_FIRMWARE_PL_BASEURL    release asset URLs
#   FPGA_FIRMWARE_*_DOWNLOAD_PREFIX                        cached file names
#   FPGA_FIRMWARE_RPU_LOCAL_DIR / FPGA_FIRMWARE_PL_LOCAL_FILE   local "src"
#   SRC_URI[<name>.sha256sum]                             per-file checksums
#   COMPATIBLE_MACHINE                                    opt the board in
#
# Until a board .bbappend sets COMPATIBLE_MACHINE, the class default
# (COMPATIBLE_MACHINE ?= "^$") keeps this recipe inert, so it never builds on
# its own. Each product build is expected to include meta-fpga-util plus exactly
# one board layer.
# -----------------------------------------------------------------------------

# ZynqMP RPU is a dual Cortex-R5; demos typically ship one ELF per core.
FPGA_FIRMWARE_PS_FILES ?= "R5c0.elf R5c1.elf"
FPGA_FIRMWARE_PL_FILE  ?= "fpga.bit"

# Source switch: "cloud" uses release assets, "local" packages sibling build
# outputs from the developer workspace. Flip in local.conf:
#     FPGA_FIRMWARE_SRC = "local"
FPGA_FIRMWARE_SRC ?= "cloud"

inherit fpga-firmware-package
