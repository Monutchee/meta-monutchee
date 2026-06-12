SUMMARY = "For ZUBoard with OpenAMP support"
DESCRIPTION = "An image including OpenAMP, libmetal, and device tree support for ZUBoard."

IMAGE_CLASSES:append = " export-tftpboot-file"

# Board-specific firmware (FPGA bitstream + R5 ELFs) -> /opt/monutchee/firmware.
# Kept here in meta-zuboard so meta-mncos stays a generic, portable OS layer.
# Scoped to the msys machine so it isn't force-installed if this image is ever
# built for another processor system while meta-zuboard is layered in.
IMAGE_INSTALL:append:msys = " zuboard-firmware"

# (Optional) Change destination directory on machine specific directory
# TFTPBOOT_DEST_DIR = "${TOPDIR}/export/tftpboot/${MACHINE}"