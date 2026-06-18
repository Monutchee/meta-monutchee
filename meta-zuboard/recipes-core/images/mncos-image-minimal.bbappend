SUMMARY = "For ZUBoard with OpenAMP support"
DESCRIPTION = "An image including OpenAMP, libmetal, and device tree support for ZUBoard."

IMAGE_CLASSES:append = " export-tftpboot-file"
JTAG_LOADER_TCL = "${FPGA_UTIL_LAYERDIR}/recipes-core/images/files/load-jtag-image.tcl"
do_copy_tftpboot[file-checksums] += "${JTAG_LOADER_TCL}:True"

# Board-specific firmware (FPGA bitstream + R5 ELFs) -> /opt/monutchee/${MACHINE}/firmware.
# fwctl provides /usr/bin/fwctl for staging/loading that firmware.
# Scoped to the zudemo machine so it isn't force-installed if this image is ever
# built for another processor system while meta-zuboard is layered in.
IMAGE_INSTALL:append:zudemo = " zuboard-firmware fwctl apu-rpu-ctl"

# (Optional) Change destination directory on machine specific directory
# TFTPBOOT_DEST_DIR = "${TOPDIR}/export/tftpboot/${MACHINE}"
