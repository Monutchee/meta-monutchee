DESCRIPTION = "Minimal MNCOS image for ZuBoard"
LICENSE = "MIT"

require recipes-core/images/include/mncos-image-common.inc

COMPATIBLE_MACHINE = "^zudemo$"

MNCOS_IMAGE_ROLE = "main"
MNCOS_IMAGE_LABEL = "MNCOS ZUBOARD MAIN SYSTEM IMAGE"

# Product-specific packages.
#   :append adds board-specific packages on top of the shared MNCOS base set.
#   :remove (if needed) trims packages from the base set, e.g.:
#       IMAGE_INSTALL:remove = " htop"
IMAGE_INSTALL:append = " \
    zuboard-firmware \
    fwctl \
    apu-rpu-ctl \
"

# Board-specific dev flow: TFTP/JTAG boot export (provided by meta-fpga-util).
IMAGE_CLASSES:append = " export-tftpboot-file"
JTAG_LOADER_TCL = "${FPGA_UTIL_LAYERDIR}/recipes-core/images/files/load-jtag-image.tcl"
do_copy_tftpboot[file-checksums] += "${JTAG_LOADER_TCL}:True"

# (Optional) Change destination directory on machine specific directory
# TFTPBOOT_DEST_DIR = "${TOPDIR}/export/tftpboot/${MACHINE}"
