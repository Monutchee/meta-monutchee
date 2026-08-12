SUMMARY = "MSAP1 APU application"
DESCRIPTION = "Builds the meter acquisition daemon, service manager, mnc diagnostic CLI, and authenticated MSAP1 web backend."
HOMEPAGE = "https://github.com/Monutchee/MSAP1_APU"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"

# Source switch:
#   cloud     - fetch the selected branch from GitHub (default)
#   local     - fetch the committed state of a local Git checkout
#   local_inst - build the local working tree directly, including uncommitted edits
MSAP1_APU_APP_SRC ?= "cloud"
MSAP1_APU_APP_GIT_BRANCH ?= "main"
MSAP1_APU_APP_LOCAL_DIR ?= "${TOPDIR}/../../applications/MSAP1_APU"

MSAP1_APU_APP_REPO_cloud = "gitsm://github.com/Monutchee/MSAP1_APU.git;protocol=https;branch=${MSAP1_APU_APP_GIT_BRANCH};name=msap1-apu-app;destsuffix=git"
MSAP1_APU_APP_REPO_local = "gitsm://${MSAP1_APU_APP_LOCAL_DIR};protocol=file;branch=${MSAP1_APU_APP_GIT_BRANCH};name=msap1-apu-app;destsuffix=git"
MSAP1_APU_APP_REPO_local_inst = ""

SRC_URI = "${@d.getVar('MSAP1_APU_APP_REPO_' + (d.getVar('MSAP1_APU_APP_SRC') or 'cloud'))}"
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append = " \
    file://msap1-fpga-acquisition.service \
    file://msap1-meter-stream.service \
    file://msap1-meter-historian.service \
    file://msap1-settings.service \
    file://msap1-service-manager.service \
    file://msap1-web-backend.service \
    file://msap1-web-tls-setup \
    file://msap1-nginx.conf \
    file://msap1-runtime.conf \
    file://70-msap1-meter.rules \
    file://60-msap1-journal.conf \
"
SRCREV_msap1-apu-app ?= "${AUTOREV}"
PV = "${@'1.0+local' if d.getVar('MSAP1_APU_APP_SRC') == 'local_inst' else '1.0+git' + (d.getVar('SRCPV') or '')}"

S = "${WORKDIR}/git"

DEPENDS:append = " boost openssl sqlite3 systemd"
RDEPENDS:${PN}:append = " boost-system sqlite3 worker-user nginx openssl-bin libsystemd msap1-web msap1-dfx-firmware ${PN}-bash-completion"

inherit bash-completion cmake externalsrc pkgconfig systemd useradd

USERADD_PACKAGES = "${PN}"

# The identities below own state on /data, which is a persistent SD card that
# outlives any rootfs. Their numeric IDs are therefore part of the on-disk
# format and MUST be pinned: with a bare "--system" the ids are allocated at
# rootfs-assembly time by counting down from 999 in package-install order, so
# adding a service user or an RDEPENDS silently renumbers the existing ones and
# every file already on /data becomes unreadable to its owning daemon.
#
# 780-789 is reserved for MSAP1 service identities. It sits well clear of the
# dynamic band that systemd's own users (systemd-network, -resolve, -timesync)
# are drawn from, so a distro update cannot collide with it.
# Never recycle or renumber an id in this block - only append.
GROUPADD_PARAM:${PN} = "--system --gid 780 msap1-data; --system --gid 781 msap1-settings"
USERADD_PARAM:${PN} = "--system --uid 781 --home /nonexistent --no-create-home --shell /sbin/nologin --gid msap1-settings --groups msap1-data msap1-settings; \
    --system --uid 782 --home /nonexistent --no-create-home --shell /sbin/nologin --gid msap1-data msap1-stream; \
    --system --uid 783 --home /nonexistent --no-create-home --shell /sbin/nologin --gid msap1-data msap1-historian"

# Keep the external source tree clean: CMake configures and builds in WORKDIR.
EXTERNALSRC = "${@d.getVar('MSAP1_APU_APP_LOCAL_DIR') if d.getVar('MSAP1_APU_APP_SRC') == 'local_inst' else ''}"
EXTERNALSRC_BUILD = "${WORKDIR}/build"

EXTRA_OECMAKE = " \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DMNC_LOGGING_REQUIRE_SYSTEMD=ON \
"

SYSTEMD_SERVICE:${PN} = "msap1-settings.service msap1-meter-stream.service msap1-meter-historian.service msap1-fpga-acquisition.service msap1-web-backend.service msap1-service-manager.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

# EXTERNALSRC/local_inst bypasses fetch/unpack checksums. Include the APU-owned
# factory document explicitly so edits invalidate do_install and are reflected
# in the image without copying product defaults into this layer.
do_install[file-checksums] += "${S}/config/settings/factory-defaults.json:True"

do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/msap1-fpga-acquisition.service \
        ${D}${systemd_system_unitdir}/msap1-fpga-acquisition.service
    install -m 0644 ${WORKDIR}/msap1-meter-stream.service \
        ${D}${systemd_system_unitdir}/msap1-meter-stream.service
    install -m 0644 ${WORKDIR}/msap1-meter-historian.service \
        ${D}${systemd_system_unitdir}/msap1-meter-historian.service
    install -m 0644 ${WORKDIR}/msap1-settings.service \
        ${D}${systemd_system_unitdir}/msap1-settings.service
    install -m 0644 ${WORKDIR}/msap1-service-manager.service \
        ${D}${systemd_system_unitdir}/msap1-service-manager.service
    install -m 0644 ${WORKDIR}/msap1-web-backend.service \
        ${D}${systemd_system_unitdir}/msap1-web-backend.service

    install -d ${D}${libexecdir}
    install -m 0755 ${WORKDIR}/msap1-web-tls-setup \
        ${D}${libexecdir}/msap1-web-tls-setup

    install -d ${D}${sysconfdir}/monutchee/msap1
    install -m 0644 ${WORKDIR}/msap1-nginx.conf \
        ${D}${sysconfdir}/monutchee/msap1/nginx.conf
    install -d ${D}${datadir}/monutchee/msap1/settings
    install -m 0644 ${S}/config/settings/factory-defaults.json \
        ${D}${datadir}/monutchee/msap1/settings/factory-defaults.json
    cmp -s ${S}/config/settings/factory-defaults.json \
        ${D}${datadir}/monutchee/msap1/settings/factory-defaults.json || \
        bbfatal "installed factory settings differ from the selected APU source"

    install -d ${D}${sysconfdir}/udev/rules.d
    install -m 0644 ${WORKDIR}/70-msap1-meter.rules \
        ${D}${sysconfdir}/udev/rules.d/70-msap1-meter.rules

    install -d ${D}${nonarch_libdir}/tmpfiles.d
    install -m 0644 ${WORKDIR}/msap1-runtime.conf \
        ${D}${nonarch_libdir}/tmpfiles.d/msap1-runtime.conf

    install -d ${D}${sysconfdir}/systemd/journald.conf.d
    install -m 0644 ${WORKDIR}/60-msap1-journal.conf \
        ${D}${sysconfdir}/systemd/journald.conf.d/60-msap1-journal.conf
}

FILES:${PN}:append = " \
    ${systemd_system_unitdir}/msap1-fpga-acquisition.service \
    ${systemd_system_unitdir}/msap1-meter-stream.service \
    ${systemd_system_unitdir}/msap1-meter-historian.service \
    ${systemd_system_unitdir}/msap1-settings.service \
    ${systemd_system_unitdir}/msap1-service-manager.service \
    ${systemd_system_unitdir}/msap1-web-backend.service \
    ${libexecdir}/msap1-web-tls-setup \
    ${sysconfdir}/monutchee/msap1/nginx.conf \
    ${datadir}/monutchee/msap1/settings/factory-defaults.json \
    ${sysconfdir}/udev/rules.d/70-msap1-meter.rules \
    ${nonarch_libdir}/tmpfiles.d/msap1-runtime.conf \
    ${sysconfdir}/systemd/journald.conf.d/60-msap1-journal.conf \
"
