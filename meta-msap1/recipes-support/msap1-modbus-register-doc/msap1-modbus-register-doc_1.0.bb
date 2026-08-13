SUMMARY = "MSAP1 Modbus register-map Excel documentation"
DESCRIPTION = "Runs the compiled MSAP1 Modbus map exporter and renders its JSON as a single-sheet XLSX workbook."
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"

SRC_URI = " \
    file://generate_modbus_registers_xlsx.py \
    file://test_generate_modbus_registers_xlsx.py \
"

S = "${WORKDIR}"
B = "${WORKDIR}/build"

DEPENDS = "msap1-apu-app qemu-native python3-native"

inherit python3native qemu

COMPATIBLE_MACHINE = "^msap1$"
PACKAGE_ARCH = "${MACHINE_ARCH}"

MODBUS_MAP_JSON = "${B}/${MACHINE}_modbus_registers.json"
MODBUS_REGISTER_WORKBOOK = "${B}/${MACHINE}_modbus_registers.xlsx"
MODBUS_DOCUMENT_EXPORT_DIR ?= "${TOPDIR}/export/docs"

do_configure[noexec] = "1"

do_compile() {
    install -d ${B}

    ${@qemu_run_binary(d, '${RECIPE_SYSROOT}', '/sysroot-only${bindir}/modbus-map-dump')} \
        --format json > ${MODBUS_MAP_JSON}

    ${PYTHON} ${WORKDIR}/test_generate_modbus_registers_xlsx.py
    ${PYTHON} ${WORKDIR}/generate_modbus_registers_xlsx.py \
        --input ${MODBUS_MAP_JSON} \
        --output ${MODBUS_REGISTER_WORKBOOK}
}

do_export_docs() {
    install -d ${MODBUS_DOCUMENT_EXPORT_DIR}
    install -m 0644 ${MODBUS_REGISTER_WORKBOOK} \
        ${MODBUS_DOCUMENT_EXPORT_DIR}/${MACHINE}_modbus_registers.xlsx
}

# The export directory is a developer-facing build output, like export/image
# and export/sdk.  Run this cheap copy even when do_compile is restored from
# sstate so deleting export/docs can be repaired by rebuilding the recipe.
do_export_docs[nostamp] = "1"
PSEUDO_IGNORE_PATHS .= ",${MODBUS_DOCUMENT_EXPORT_DIR}"
addtask export_docs after do_compile before do_build

ALLOW_EMPTY:${PN} = "1"
