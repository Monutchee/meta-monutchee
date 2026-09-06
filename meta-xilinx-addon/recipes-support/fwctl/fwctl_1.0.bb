SUMMARY = "FPGA and remoteproc firmware control utility"
DESCRIPTION = "Installs fwctl, a small shell utility for staging FPGA bitstreams and Cortex-R firmware through Linux fpga_manager and remoteproc."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://fwctl"

S = "${WORKDIR}"

FWCTL_FW_SRC ?= "/opt/monutchee/${MACHINE}/firmware"
FWCTL_FW_DST ?= "/lib/firmware"
FWCTL_FPGA_FILE ?= "fpga.bit"
FWCTL_R5C0_FILE ?= "r5c0.elf"
FWCTL_R5C1_FILE ?= "r5c1.elf"

PACKAGE_ARCH = "${MACHINE_ARCH}"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -D -m 0755 "${WORKDIR}/fwctl" "${D}${bindir}/fwctl"

    install -d "${D}${sysconfdir}/default"
    cat > "${D}${sysconfdir}/default/fwctl" <<EOF
FWCTL_FW_SRC="${FWCTL_FW_SRC}"
FWCTL_FW_DST="${FWCTL_FW_DST}"
FWCTL_FPGA_FILE="${FWCTL_FPGA_FILE}"
FWCTL_R5C0_FILE="${FWCTL_R5C0_FILE}"
FWCTL_R5C1_FILE="${FWCTL_R5C1_FILE}"
EOF
}

FILES:${PN} = "${bindir}/fwctl ${sysconfdir}/default/fwctl"
CONFFILES:${PN} = "${sysconfdir}/default/fwctl"
RDEPENDS:${PN} = "busybox"
