SUMMARY = "MNCOS production flashing image for ZUBoard"

# This is the concrete ZynqMP implementation of the generic image contract.
MNCOS_PRODUCTION_FLASH_EXTRA_INSTALL = "zuboard-production-flasher"
MNCOS_PRODUCTION_FLASH_IMAGE_FSTYPES = "cpio.gz cpio.gz.u-boot"

# zynqmp-generic schedules a WIC task through its image class. This target is a
# RAM-only initramfs; the production WIC is built separately and bundled below.
do_image_wic[noexec] = "1"

IMAGE_CLASSES:append = " export-tftpboot-file"
TFTPBOOT_DEST_DIR = "${TOPDIR}/export/jtag-tftpboot"
JTAG_LOADER_TCL = "${ZUBOARD_LAYERDIR}/recipes-core/images/files/load-jtag-image.tcl"

# Building the flashing target also builds and exports the production WIC that
# the RAM-resident ZUBoard implementation writes to eMMC.
do_copy_production_image() {
    install -d "${TFTPBOOT_DEST_DIR}"
    install -m 0644 \
        "${DEPLOY_DIR_IMAGE}/mncos-image-minimal-${MACHINE}.rootfs.wic.xz" \
        "${TFTPBOOT_DEST_DIR}/target.wic.xz"
    cd "${TFTPBOOT_DEST_DIR}"
    sha256sum target.wic.xz > target.wic.xz.sha256
}

do_copy_production_image[depends] += "mncos-image-minimal:do_image_complete"
do_copy_tftpboot[file-checksums] += "${JTAG_LOADER_TCL}:True"
do_copy_production_image[nostamp] = "1"
addtask copy_production_image after do_copy_tftpboot before do_build
