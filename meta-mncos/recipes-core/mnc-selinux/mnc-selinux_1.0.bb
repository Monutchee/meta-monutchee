SUMMARY = "Permissive SELinux immutable policy and writable filesystem startup"
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-3.0-only;md5=c79ff39f19dfec6d293b95dea7b07891"
SRC_URI = "file://mnc-selinux file://mnc-selinux-restore.service"
S = "${WORKDIR}"
inherit systemd features_check
REQUIRED_DISTRO_FEATURES = "selinux"
SYSTEMD_SERVICE:${PN} = "mnc-selinux-restore.service"
RDEPENDS:${PN} = "python3-core python3-json python3-shell libselinux-bin policycoreutils-setfiles policycoreutils-sestatus util-linux-findmnt util-linux-mount refpolicy"
do_install() {
    install -d ${D}${libexecdir} ${D}${systemd_system_unitdir}
    install -m 0755 ${WORKDIR}/mnc-selinux ${D}${libexecdir}/mnc-selinux
    install -m 0644 ${WORKDIR}/mnc-selinux-restore.service ${D}${systemd_system_unitdir}/
}
