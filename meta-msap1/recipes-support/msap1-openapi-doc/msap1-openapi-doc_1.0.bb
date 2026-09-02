SUMMARY = "MSAP1 OpenAPI documentation"
DESCRIPTION = "Runs the target-built typed API contract exporter and packages its deterministic OpenAPI 3.1 YAML."
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"

S = "${WORKDIR}"
B = "${WORKDIR}/build"

DEPENDS = "msap1-apu-app qemu-native"

inherit qemu

COMPATIBLE_MACHINE = "^msap1$"
PACKAGE_ARCH = "${MACHINE_ARCH}"

OPENAPI_DOCUMENT = "${B}/msap1_api.yaml"
OPENAPI_DOCUMENT_EXPORT_DIR ?= "${TOPDIR}/export/docs"

do_configure[noexec] = "1"

do_compile() {
    install -d ${B}
    ${@qemu_run_binary(d, '${RECIPE_SYSROOT}', '/sysroot-only${bindir}/msap1-openapi-dump')} \
        > ${OPENAPI_DOCUMENT}

    test -s ${OPENAPI_DOCUMENT} || \
        bbfatal "the generated OpenAPI document is empty"
    grep -q "openapi: '3.1.0'" ${OPENAPI_DOCUMENT} || \
        bbfatal "the generated document is not OpenAPI 3.1"
    grep -q "/api/v1/documentation/msap1_api.yaml" ${OPENAPI_DOCUMENT} || \
        bbfatal "the generated document omits its OpenAPI download route"
    grep -q "/api/v1/documentation/msap1_modbus_registers.xlsx" \
        ${OPENAPI_DOCUMENT} || \
        bbfatal "the generated document omits the Modbus workbook route"
}

do_install() {
    install -d ${D}${datadir}/monutchee/msap1/docs
    install -m 0644 ${OPENAPI_DOCUMENT} \
        ${D}${datadir}/monutchee/msap1/docs/msap1_api.yaml
}

do_export_docs() {
    install -d ${OPENAPI_DOCUMENT_EXPORT_DIR}
    install -m 0644 ${OPENAPI_DOCUMENT} \
        ${OPENAPI_DOCUMENT_EXPORT_DIR}/msap1_api.yaml
}

# Keep export/docs repairable even when compilation is restored from sstate.
do_export_docs[nostamp] = "1"
PSEUDO_IGNORE_PATHS .= ",${OPENAPI_DOCUMENT_EXPORT_DIR}"
addtask export_docs after do_compile before do_build

FILES:${PN} = "${datadir}/monutchee/msap1/docs/msap1_api.yaml"
