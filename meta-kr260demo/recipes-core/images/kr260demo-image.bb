DESCRIPTION = "Minimal MNCOS image for ZuBoard"
LICENSE = "MIT"

require recipes-core/images/include/mncos-image-common.inc

COMPATIBLE_MACHINE = "^kr260demo$"

MNCOS_IMAGE_ROLE = "main"
MNCOS_IMAGE_LABEL = "MNCOS ZUBOARD MAIN SYSTEM IMAGE"

# Product-specific packages.
#   :append adds board-specific packages on top of the shared MNCOS base set.
#   :remove (if needed) trims packages from the base set, e.g.:
#       IMAGE_INSTALL:remove = " htop"
#
# PL bitstream loading: on the Kria K26 SOM the PL must be loaded via dfx-mgr
# (xmutil loadapp), which applies the PL device-tree overlay and brings up the
# PL clock + PS<->PL AXI bridge. The raw fpga_manager path (fwctl) is intentionally
# NOT installed on this board: it bypasses the overlay, leaving the PL AXI
# interface unbrought-up so any AXI access from the APU/R5 hangs. R5 firmware
# load/start is handled by apu-rpu-ctl.
IMAGE_INSTALL:append = " \
    apu-rpu-ctl \
    dfx-mgr \
    kr260demo-pl-app \
    lmsensors-config-kria-fancontrol \
"

# This product image does not need the generated machine's VCU codec stack.
IMAGE_FEATURES:remove = "hwcodecs"

# Board-specific dev flow: TFTP/JTAG boot export (provided by meta-fpga-util).
IMAGE_CLASSES:append = " export-tftpboot-file"
JTAG_LOADER_TCL = "${FPGA_UTIL_LAYERDIR}/recipes-core/images/files/load-jtag-image.tcl"
JTAG_LOADER_FORCE_JTAG_BOOT = "1"
do_copy_tftpboot[file-checksums] += "${JTAG_LOADER_TCL}:True"

# (Optional) Change destination directory on machine specific directory
# TFTPBOOT_DEST_DIR = "${TOPDIR}/export/tftpboot/${MACHINE}"
