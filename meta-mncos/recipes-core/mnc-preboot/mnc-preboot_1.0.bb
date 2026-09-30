SUMMARY = "MNC early boot setup before the main init system"
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-3.0-only;md5=c79ff39f19dfec6d293b95dea7b07891"
SRC_URI = "file://mnc-preboot file://ram-init"
S = "${WORKDIR}"
RDEPENDS:${PN} = "mnc-storage util-linux-mount util-linux-mountpoint systemd"

do_install() {
    install -d ${D}${base_sbindir} ${D}${datadir}/mnc/preboot
    install -m 0755 ${WORKDIR}/mnc-preboot ${D}${base_sbindir}/mnc-preboot
    install -m 0755 ${WORKDIR}/ram-init ${D}${datadir}/mnc/preboot/ram-init
}
FILES:${PN} += "${datadir}/mnc/preboot"
