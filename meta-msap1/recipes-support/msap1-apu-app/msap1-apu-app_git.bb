SUMMARY = "MSAP1 APU application"
DESCRIPTION = "Builds the meter DMA acquisition daemon, diagnostic client, and authenticated MSAP1 web backend."
HOMEPAGE = "https://github.com/Monutchee/MSAP1_APU"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"

# Source switch:
#   cloud     - fetch the selected branch from GitHub (default)
#   local     - fetch the committed state of a local Git checkout
#   local_inst - build the local working tree directly, including uncommitted edits
MSAP1_APU_APP_SRC ?= "cloud"
MSAP1_APU_APP_GIT_BRANCH ?= "main"
MSAP1_APU_APP_LOCAL_DIR ?= "${TOPDIR}/../../MSAP1_APU"

MSAP1_APU_APP_REPO_cloud = "gitsm://github.com/Monutchee/MSAP1_APU.git;protocol=https;branch=${MSAP1_APU_APP_GIT_BRANCH};name=msap1-apu-app;destsuffix=git"
MSAP1_APU_APP_REPO_local = "gitsm://${MSAP1_APU_APP_LOCAL_DIR};protocol=file;branch=${MSAP1_APU_APP_GIT_BRANCH};name=msap1-apu-app;destsuffix=git"
MSAP1_APU_APP_REPO_local_inst = ""

SRC_URI = "${@d.getVar('MSAP1_APU_APP_REPO_' + (d.getVar('MSAP1_APU_APP_SRC') or 'cloud'))}"
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append = " \
    file://msap1-fpga-acquisition.service \
    file://msap1-web-backend.service \
    file://msap1-web-tls-setup \
    file://msap1-nginx.conf \
    file://msap1-runtime.conf \
    file://meter-conversion.json \
    file://70-msap1-meter.rules \
"
SRCREV_msap1-apu-app ?= "${AUTOREV}"
PV = "${@'1.0+local' if d.getVar('MSAP1_APU_APP_SRC') == 'local_inst' else '1.0+git' + (d.getVar('SRCPV') or '')}"

S = "${WORKDIR}/git"

DEPENDS:append = " boost openssl"
RDEPENDS:${PN}:append = " worker-user nginx openssl-bin msap1-web msap1-dfx-firmware"

inherit cmake externalsrc systemd useradd

USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "--system msap1-data"

# Keep the external source tree clean: CMake configures and builds in WORKDIR.
EXTERNALSRC = "${@d.getVar('MSAP1_APU_APP_LOCAL_DIR') if d.getVar('MSAP1_APU_APP_SRC') == 'local_inst' else ''}"
EXTERNALSRC_BUILD = "${WORKDIR}/build"

EXTRA_OECMAKE = "-DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF"

SYSTEMD_SERVICE:${PN} = "msap1-fpga-acquisition.service msap1-web-backend.service"
SYSTEMD_AUTO_ENABLE:${PN} = "enable"

do_install:append() {
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/msap1-fpga-acquisition.service \
        ${D}${systemd_system_unitdir}/msap1-fpga-acquisition.service
    install -m 0644 ${WORKDIR}/msap1-web-backend.service \
        ${D}${systemd_system_unitdir}/msap1-web-backend.service

    install -d ${D}${libexecdir}
    install -m 0755 ${WORKDIR}/msap1-web-tls-setup \
        ${D}${libexecdir}/msap1-web-tls-setup

    install -d ${D}${sysconfdir}/monutchee/msap1
    install -m 0644 ${WORKDIR}/msap1-nginx.conf \
        ${D}${sysconfdir}/monutchee/msap1/nginx.conf
    install -m 0644 ${WORKDIR}/meter-conversion.json \
        ${D}${sysconfdir}/monutchee/msap1/meter-conversion.json

    install -d ${D}${sysconfdir}/udev/rules.d
    install -m 0644 ${WORKDIR}/70-msap1-meter.rules \
        ${D}${sysconfdir}/udev/rules.d/70-msap1-meter.rules

    install -d ${D}${nonarch_libdir}/tmpfiles.d
    install -m 0644 ${WORKDIR}/msap1-runtime.conf \
        ${D}${nonarch_libdir}/tmpfiles.d/msap1-runtime.conf
}

FILES:${PN}:append = " \
    ${systemd_system_unitdir}/msap1-fpga-acquisition.service \
    ${systemd_system_unitdir}/msap1-web-backend.service \
    ${libexecdir}/msap1-web-tls-setup \
    ${sysconfdir}/monutchee/msap1/nginx.conf \
    ${sysconfdir}/monutchee/msap1/meter-conversion.json \
    ${sysconfdir}/udev/rules.d/70-msap1-meter.rules \
    ${nonarch_libdir}/tmpfiles.d/msap1-runtime.conf \
"
