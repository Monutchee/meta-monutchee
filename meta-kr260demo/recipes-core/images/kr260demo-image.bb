DESCRIPTION = "Minimal MNCOS image for KR260Demo"
LICENSE = "MIT"

require recipes-core/images/include/mncos-image-common.inc

COMPATIBLE_MACHINE = "^kr260demo$"

MNCOS_IMAGE_ROLE = "main"
MNCOS_IMAGE_LABEL = "MNCOS KR260DEMO MAIN SYSTEM IMAGE"

# Product-specific packages.
#   :append adds board-specific packages on top of the shared MNCOS base set.
#   :remove (if needed) trims packages from the base set, e.g.:
#       IMAGE_INSTALL:remove = " htop"
#
# PL/RPU firmware is packaged by the product recipe through the shared
# xilinx-dfx-firmware class and loaded through dfx-mgr.
IMAGE_INSTALL:append = " \
    apu-rpu-ctl \
    dfx-mgr \
    kr260demo-dfx-firmware \
    lmsensors-config-kria-fancontrol \
"

# This product image does not need the generated machine's VCU codec stack.
IMAGE_FEATURES:remove = "hwcodecs"

# Board-specific dev flow: TFTP/JTAG boot export.
IMAGE_CLASSES:append = " export-tftpboot-file"
JTAG_LOADER_TCL = "${XILINX_ADDON_LAYERDIR}/recipes-core/images/files/load-jtag-image.tcl"
JTAG_LOADER_FORCE_JTAG_BOOT = "1"
do_copy_tftpboot[file-checksums] += "${JTAG_LOADER_TCL}:True"

# (Optional) Change destination directory on machine specific directory
# TFTPBOOT_DEST_DIR = "${TOPDIR}/export/tftpboot/${MACHINE}"
