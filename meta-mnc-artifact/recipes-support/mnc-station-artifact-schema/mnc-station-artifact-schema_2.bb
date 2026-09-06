SUMMARY = "Monutchee Station artifact manifest schema"
DESCRIPTION = "Installs the public v2 JSON Schema used to validate Monutchee Station artifacts."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS:prepend := "${THISDIR}/../../schema:"
SRC_URI = "file://mnc-station-artifact-v2.schema.json"

S = "${WORKDIR}"

inherit allarch

do_install() {
    install -d "${D}${datadir}/mnc-station-artifact/schema"
    install -m 0644 \
        "${WORKDIR}/mnc-station-artifact-v2.schema.json" \
        "${D}${datadir}/mnc-station-artifact/schema/mnc-station-artifact-v2.schema.json"
}

FILES:${PN} = "${datadir}/mnc-station-artifact/schema/mnc-station-artifact-v2.schema.json"
