SUMMARY = "MNCOS JTAG provisioning image for the ZUBoard"

IMAGE_CLASSES:append = " export-tftpboot-file"
TFTPBOOT_DEST_DIR = "${TOPDIR}/export/jtag-tftpboot"
JTAG_LOADER_TCL = "${ZUBOARD_LAYERDIR}/recipes-core/images/files/load-jtag-image.tcl"

# Building the provisioning target also builds and exports the production WIC
# that the RAM image will write to eMMC.
do_copy_provision_target() {
    install -d "${TFTPBOOT_DEST_DIR}"
    install -m 0644 \
        "${DEPLOY_DIR_IMAGE}/mncos-image-minimal-${MACHINE}.rootfs.wic.xz" \
        "${TFTPBOOT_DEST_DIR}/target.wic.xz"
    cd "${TFTPBOOT_DEST_DIR}"
    sha256sum target.wic.xz > target.wic.xz.sha256
}

do_copy_provision_target[depends] += "mncos-image-minimal:do_image_complete"
do_copy_tftpboot[file-checksums] += "${JTAG_LOADER_TCL}:True"
do_copy_provision_target[nostamp] = "1"
addtask copy_provision_target after do_copy_tftpboot before do_build
