SUMMARY = "MSAP1 persistent-data SD-card mount"
DESCRIPTION = "Mounts the meter system SD-card partition at /data during system startup."
LICENSE = "CLOSED"

SRC_URI = "file://data.mount"

S = "${WORKDIR}"

inherit systemd

SYSTEMD_SERVICE:${PN} = "data.mount"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/data.mount \
        ${D}${systemd_system_unitdir}/data.mount

    install -d ${D}/data
}

FILES:${PN} += " \
    /data \
    ${systemd_system_unitdir}/data.mount \
"
