FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:msap1 = " \
    file://dfx-mgr-fw-load-remain-after-exit.conf \
    file://dfx-mgr-fw-load-logging.conf \
"

# The default PL load is non-idempotent. Keep a successful oneshot active so
# restarting a consumer cannot enqueue the firmware loader a second time.
do_install:append:msap1() {
    install -d \
        ${D}${systemd_system_unitdir}/dfx-mgr-fw-load.service.d
    install -m 0644 \
        ${WORKDIR}/dfx-mgr-fw-load-remain-after-exit.conf \
        ${D}${systemd_system_unitdir}/dfx-mgr-fw-load.service.d/10-msap1-remain-after-exit.conf
    install -m 0644 \
        ${WORKDIR}/dfx-mgr-fw-load-logging.conf \
        ${D}${systemd_system_unitdir}/dfx-mgr-fw-load.service.d/20-msap1-logging.conf
}

FILES:${PN}:append:msap1 = " \
    ${systemd_system_unitdir}/dfx-mgr-fw-load.service.d/10-msap1-remain-after-exit.conf \
    ${systemd_system_unitdir}/dfx-mgr-fw-load.service.d/20-msap1-logging.conf \
"
