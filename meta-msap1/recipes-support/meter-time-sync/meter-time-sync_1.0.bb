SUMMARY = "Meter system-clock discipline defaults"
DESCRIPTION = "Low-jitter NTP polling defaults while retaining NTP as the product default."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit allarch

COMPATIBLE_MACHINE = "^msap1$"

SRC_URI = "file://50-meter-time.conf"

do_install() {
    install -d ${D}${sysconfdir}/systemd/timesyncd.conf.d
    install -m 0644 ${WORKDIR}/50-meter-time.conf \
        ${D}${sysconfdir}/systemd/timesyncd.conf.d/50-meter-time.conf
}

FILES:${PN} = "${sysconfdir}/systemd/timesyncd.conf.d/50-meter-time.conf"
RDEPENDS:${PN} += "systemd"
