SUMMARY = "Guarded ZUBoard eMMC production flashing utility"
DESCRIPTION = "Downloads a compressed MNCOS WIC image from TFTP, verifies it, and writes it to ZUBoard eMMC."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = " \
    file://mncos-flash-emmc \
    file://mncos-production-flash.conf \
"

S = "${WORKDIR}"

do_install() {
    install -D -m 0755 ${WORKDIR}/mncos-flash-emmc \
        ${D}${sbindir}/mncos-flash-emmc
    install -D -m 0644 ${WORKDIR}/mncos-production-flash.conf \
        ${D}${sysconfdir}/mncos-production-flash.conf
}

RDEPENDS:${PN} = "busybox xz util-linux-blockdev"
