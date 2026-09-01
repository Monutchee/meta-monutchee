DESCRIPTION = "Minimal MNCOS image for MSAP1"
LICENSE = "MIT"

require recipes-core/images/include/mncos-image-common.inc

COMPATIBLE_MACHINE = "^msap1$"

MNCOS_IMAGE_ROLE = "main"
MNCOS_IMAGE_LABEL = "MNCOS MSAP1 MAIN SYSTEM IMAGE"

MSAP1_ENABLE_DEBUGAI ?= "0"

IMAGE_INSTALL:append = " \
    msap1-apu-app \
    msap1-web \
    msap1-openapi-doc \
    msap1-modbus-register-doc \
    msap1-dma \
    dfx-mgr \
    msap1-dfx-firmware \
    lmsensors-config-kria-fancontrol \
    devmem2 \
    msap1-data-mount \
    ${@'msap1-debugai' if d.getVar('MSAP1_ENABLE_DEBUGAI') == '1' else ''} \
"

IMAGE_FEATURES:remove = "hwcodecs"

IMAGE_CLASSES:append = " export-tftpboot-file"
JTAG_LOADER_TCL = "${XILINX_ADDON_LAYERDIR}/recipes-core/images/files/load-jtag-image.tcl"
JTAG_LOADER_FORCE_JTAG_BOOT = "1"
do_copy_tftpboot[file-checksums] += "${JTAG_LOADER_TCL}:True"

# Export the same immutable product documentation installed in the rootfs.
do_build[depends] += " \
    msap1-openapi-doc:do_export_docs \
    msap1-modbus-register-doc:do_export_docs \
    msap1-jtag-image:do_export_provision_image \
"
do_populate_sdk[depends] += " \
    msap1-openapi-doc:do_export_docs \
    msap1-modbus-register-doc:do_export_docs \
"
