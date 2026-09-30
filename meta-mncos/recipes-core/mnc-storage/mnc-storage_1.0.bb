SUMMARY = "Policy-driven persistent storage maintenance"
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-3.0-only;md5=c79ff39f19dfec6d293b95dea7b07891"
SRC_URI = "file://mnc-storage"
S = "${WORKDIR}"
RDEPENDS:${PN} = "python3-core python3-json python3-fcntl python3-shell python3-compression python3-crypt python3-io python3-misc util-linux-lsblk util-linux-findmnt util-linux-sfdisk util-linux-mount util-linux-umount util-linux-losetup e2fsprogs-mke2fs e2fsprogs-e2fsck psmisc coreutils systemd"
do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/mnc-storage ${D}${sbindir}/mnc-storage
}
