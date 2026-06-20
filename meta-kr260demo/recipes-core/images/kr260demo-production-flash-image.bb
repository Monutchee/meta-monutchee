SUMMARY = "MNCOS production flashing image for ZuBoard"
DESCRIPTION = "RAM-resident ZuBoard image that writes the production WIC to eMMC."
LICENSE = "MIT"

inherit core-image mncos-image-identity

COMPATIBLE_MACHINE = "^zudemo$"

MNCOS_IMAGE_ROLE = "production-flash"
MNCOS_IMAGE_LABEL = "MNCOS ZUDEMO PRODUCTION FLASH IMAGE"

IMAGE_INSTALL = " \
    packagegroup-core-boot \
    systemd-conf \
    busybox \
    zudemo-production-flasher \
"

IMAGE_FEATURES = ""
IMAGE_LINGUAS = ""
IMAGE_ROOTFS_SIZE = "32768"
IMAGE_ROOTFS_EXTRA_SPACE = "0"
NO_RECOMMENDATIONS = "1"

ZUDEMO_PRODUCTION_FLASH_IMAGE_FSTYPES ?= "cpio.gz cpio.gz.u-boot"

python () {
    d.setVar("IMAGE_FSTYPES", d.getVar("ZUDEMO_PRODUCTION_FLASH_IMAGE_FSTYPES"))
}

# zynqmp-generic schedules a WIC task through its image class. This target is a
# RAM-only initramfs; the production WIC is built separately and bundled below.
do_image_wic[noexec] = "1"

IMAGE_CLASSES:append = " export-tftpboot-file"
TFTPBOOT_DEST_DIR = "${TOPDIR}/export/jtag-tftpboot"
JTAG_LOADER_TCL = "${FPGA_UTIL_LAYERDIR}/recipes-core/images/files/load-jtag-image.tcl"
JTAG_LOADER_FORCE_JTAG_BOOT = "1"

# Building the flashing target also builds and exports the production WIC that
# the RAM-resident ZuBoard implementation writes to eMMC.
do_copy_production_image() {
    install -d "${TFTPBOOT_DEST_DIR}"
    install -m 0644 \
        "${DEPLOY_DIR_IMAGE}/zudemo-image-${MACHINE}.rootfs.wic.xz" \
        "${TFTPBOOT_DEST_DIR}/target.wic.xz"
    cd "${TFTPBOOT_DEST_DIR}"
    sha256sum target.wic.xz > target.wic.xz.sha256
}

do_copy_production_image[depends] += "zudemo-image:do_image_complete"
do_copy_tftpboot[file-checksums] += "${JTAG_LOADER_TCL}:True"
do_copy_production_image[nostamp] = "1"
addtask copy_production_image after do_copy_tftpboot before do_build
