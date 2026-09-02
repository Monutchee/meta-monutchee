FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:msap1 = " file://ptp4l.conf"

do_install:append:msap1() {
    install -m 0644 ${WORKDIR}/ptp4l.conf \
        ${D}${sysconfdir}/linuxptp/ptp4l.conf
}
