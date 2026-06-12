SUMMARY = "ZuBoard demo FPGA bitstream and Cortex-R5 firmware"
DESCRIPTION = "Fetches the prebuilt FPGA (PL) bitstream and Cortex-R5 (PS) \
firmware ELF(s) from the ZuBoardDemo GitHub releases and installs them into \
${FIRMWARE_INSTALL_DIR} on the target."
LICENSE = "CLOSED"

# -----------------------------------------------------------------------------
# Release selection
#
# To pull a new set of binaries:
#   1. Bump ZUBOARD_RELEASE_TAG to the new GitHub release tag.
#   2. Update the SRC_URI[...sha256sum] values below.
#
# Tip: after changing the tag (or adding an ELF), let bitbake tell you the new
#      checksums:
#        bitbake -c fetch zuboard-firmware
#      it fails on the checksum mismatch and prints the correct
#      SRC_URI[...sha256sum] lines to paste back in here.
#
# Both repos are expected to ship the same tag. If they ever diverge, override
# ZUBOARD_PS_TAG / ZUBOARD_PL_TAG individually (e.g. in your distro/local.conf).
# -----------------------------------------------------------------------------
ZUBOARD_RELEASE_TAG ?= "v0.0.1_t"
ZUBOARD_PS_TAG ?= "${ZUBOARD_RELEASE_TAG}"
ZUBOARD_PL_TAG ?= "${ZUBOARD_RELEASE_TAG}"

# Destination directory on the target rootfs.
FIRMWARE_INSTALL_DIR ?= "/opt/monutchee/msys/firmware"

# -----------------------------------------------------------------------------
# PS (Cortex-R5) firmware ELFs.
#
# Space separated list of asset file names published on the ZuBoardDemo_PS
# release. The dual-core R5 system runs one ELF per core, e.g.:
#     R5c0.elf -> R5 core 0
#     R5c1.elf -> R5 core 1
#
# To add core 1 once that asset exists, just append it here and add its
# checksum below (the checksum flag name is the file name minus ".elf",
# lower-cased: R5c1.elf -> SRC_URI[r5c1.sha256sum]):
#     ZUBOARD_PS_FILES ?= "R5c0.elf R5c1.elf"
# -----------------------------------------------------------------------------
ZUBOARD_PS_FILES ?= "R5c0.elf"

# PL (FPGA) bitstream.
ZUBOARD_PL_FILE ?= "fpga.bit"

ZUBOARD_PS_BASEURL = "https://github.com/lesterlo/ZuBoardDemo_PS/releases/download"
ZUBOARD_PL_BASEURL = "https://github.com/lesterlo/ZuBoardDemo_PL/releases/download"

# The release asset names do not encode the tag, so downloadfilename embeds the
# tag to keep DL_DIR entries unique (avoids stale-cache checksum errors on bump).
SRC_URI = "${ZUBOARD_PL_BASEURL}/${ZUBOARD_PL_TAG}/${ZUBOARD_PL_FILE};name=fpga;downloadfilename=zuboard-pl-${ZUBOARD_PL_TAG}-${ZUBOARD_PL_FILE} \
           file://msys"

# Expand one SRC_URI entry per PS ELF listed in ZUBOARD_PS_FILES.
python () {
    tag = d.getVar('ZUBOARD_PS_TAG')
    base = d.getVar('ZUBOARD_PS_BASEURL')
    for f in (d.getVar('ZUBOARD_PS_FILES') or "").split():
        name = f.rsplit('.', 1)[0].lower()
        d.appendVar('SRC_URI', " %s/%s/%s;name=%s;downloadfilename=zuboard-ps-%s-%s" % (base, tag, f, name, tag, f))
}

# One checksum per remote file. Names: 'fpga' for the bitstream, and the ELF
# file name minus extension (lower-cased) for each PS core.
SRC_URI[fpga.sha256sum] = "c42fec55268f3fb4b613528b40c761219351c72f6deca3538f00a39ba49725c6"
SRC_URI[r5c0.sha256sum] = "9b3df5c26072603ff612f2b13bf35c379e1f3b361ec4d5bb6c597414a133e266"
# SRC_URI[r5c1.sha256sum] = "<fill in once R5c1.elf is published>"

S = "${WORKDIR}"

# Prebuilt blobs: nothing to configure or compile.
do_configure[noexec] = "1"
do_compile[noexec] = "1"

# Don't strip / split-debug / run arch QA on the prebuilt binaries. The R5 ELFs
# are 32-bit ARM images for the RPU, which would otherwise trip the 'arch' check.
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INHIBIT_SYSROOT_STRIP = "1"
INSANE_SKIP:${PN} += "arch ldflags textrel"

# Firmware is board specific: only build for the msys machine, and make the
# package machine-specific so a tag/binary change rebuilds cleanly.
COMPATIBLE_MACHINE = "^msys$"
PACKAGE_ARCH = "${MACHINE_ARCH}"

do_install() {
    install -d ${D}${FIRMWARE_INSTALL_DIR}

    # PS: one ELF per R5 core. Installed under canonical lower-case names
    # (R5c0.elf -> r5c0.elf) so they match the 'msys' tool and sysfs usage.
    for f in ${ZUBOARD_PS_FILES}; do
        dest=$(echo "$f" | tr '[:upper:]' '[:lower:]')
        install -m 0644 ${WORKDIR}/zuboard-ps-${ZUBOARD_PS_TAG}-$f \
            ${D}${FIRMWARE_INSTALL_DIR}/$dest
    done

    # PL: FPGA bitstream.
    install -m 0644 ${WORKDIR}/zuboard-pl-${ZUBOARD_PL_TAG}-${ZUBOARD_PL_FILE} \
        ${D}${FIRMWARE_INSTALL_DIR}/${ZUBOARD_PL_FILE}

    # Firmware management CLI -> /usr/bin/msys
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/msys ${D}${bindir}/msys
}

FILES:${PN} = "${FIRMWARE_INSTALL_DIR} ${bindir}/msys"
