SUMMARY = "MNC system management SDK and privileged native service"
DESCRIPTION = "Portable system contracts, systemd D-Bus client, service lifecycle, journal logging and durable OS control service. Product policy is supplied separately."
LICENSE = "GPL-3.0-only & Apache-2.0 & MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=1ebbd3e34237af26da5dc08a4e440464 file://LICENSE.runtime;md5=86d3f3a95c324c9479bd8986968f4327 file://${S}/glaze/LICENSE;md5=ea4d29875d83fbbf50485c846dbbbed8"
SRC_URI = "file://source file://mnc-system-manager.service file://mnc-system-bootstrap.service file://com.monutchee.MNCOS.System.conf git://github.com/stephenberry/glaze.git;protocol=https;nobranch=1;name=glaze;destsuffix=source/glaze"
SRCREV_glaze = "be4481f4b106fc82f0b7bc85f6a92202f8c0dd59"
SRCREV_FORMAT = "glaze"
S = "${WORKDIR}/source"
DEPENDS = "systemd openssl"
RDEPENDS:${PN} = "systemd dbus tzdata tzdata-africa tzdata-americas tzdata-antarctica tzdata-arctic tzdata-asia tzdata-atlantic tzdata-australia tzdata-europe tzdata-pacific"
inherit cmake pkgconfig systemd
EXTRA_OECMAKE = "-DMNC_SYSTEM_BUILD_DAEMON=ON -DMNC_SYSTEM_REQUIRE_SYSTEMD=ON -DMNC_GLAZE_SOURCE_DIR=${S}/glaze -DBUILD_TESTING=OFF"
# Products enable both units only after installing a trusted profile.
SYSTEMD_SERVICE:${PN} = "mnc-system-bootstrap.service mnc-system-manager.service"
SYSTEMD_AUTO_ENABLE:${PN} = "disable"
do_install:append() {
    install -d ${D}${systemd_system_unitdir} ${D}${datadir}/dbus-1/system.d
    install -m 0644 ${WORKDIR}/mnc-system-manager.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${WORKDIR}/mnc-system-bootstrap.service ${D}${systemd_system_unitdir}/
    install -m 0644 ${WORKDIR}/com.monutchee.MNCOS.System.conf ${D}${datadir}/dbus-1/system.d/
}
FILES:${PN} += "${datadir}/dbus-1/system.d"
