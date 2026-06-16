SUMMARY = "ZuBoard demo FPGA bitstream and Cortex-R5 firmware"
DESCRIPTION = "Fetches or locally packages the prebuilt FPGA (PL) bitstream \
and Cortex-R5 firmware ELF(s), then installs them into \
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
ZUBOARD_RELEASE_TAG ?= "v0.0.1"
ZUBOARD_PS_TAG ?= "${ZUBOARD_RELEASE_TAG}"
ZUBOARD_PL_TAG ?= "${ZUBOARD_RELEASE_TAG}"

# Source switch: "cloud" (GitHub release assets, default) or "local" (live
# build outputs from sibling checkouts).
#
# Flip in local.conf:
#     ZUBOARD_FIRMWARE_SRC = "local"
#
# Cloud mode is the reproducible release path. Local mode is the fast iteration
# path after rebuilding ZuBoardDemo_RPU / ZuBoardDemo_PL locally.
ZUBOARD_FIRMWARE_SRC ?= "cloud"
ZUBOARD_RPU_LOCAL_DIR ?= "${TOPDIR}/../../ZuBoardDemo_RPU"
ZUBOARD_PL_LOCAL_DIR ?= "${TOPDIR}/../../ZuBoardDemo_PL"
ZUBOARD_PL_LOCAL_FILE ?= "${ZUBOARD_PL_LOCAL_DIR}/vivado_gen/ZuBoardDemo_PL.runs/impl_1/MainBlock_wrapper.bit"

# Destination directory on the target rootfs.
FIRMWARE_INSTALL_DIR ?= "/opt/monutchee/zudemo/firmware"

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
ZUBOARD_PS_FILES ?= "R5c0.elf R5c1.elf"

# PL (FPGA) bitstream.
ZUBOARD_PL_FILE ?= "fpga.bit"

ZUBOARD_PS_BASEURL = "https://github.com/lesterlo/ZuBoardDemo_PS/releases/download"
ZUBOARD_PL_BASEURL = "https://github.com/lesterlo/ZuBoardDemo_PL/releases/download"

# zud is always local to this recipe. Firmware blobs are selected below based on
# ZUBOARD_FIRMWARE_SRC.
SRC_URI = "file://zud"

# In cloud mode, expand one SRC_URI entry per remote firmware asset.
# In local mode, add the sibling build outputs to do_install's checksum inputs
# so BitBake re-runs packaging when those artifacts change.
python () {
    src = (d.getVar('ZUBOARD_FIRMWARE_SRC') or "cloud").strip()
    ps_files = (d.getVar('ZUBOARD_PS_FILES') or "").split()

    if src == "cloud":
        pl_tag = d.getVar('ZUBOARD_PL_TAG')
        pl_base = d.getVar('ZUBOARD_PL_BASEURL')
        pl_file = d.getVar('ZUBOARD_PL_FILE')
        d.appendVar('SRC_URI', " %s/%s/%s;name=fpga;downloadfilename=zuboard-pl-%s-%s" % (pl_base, pl_tag, pl_file, pl_tag, pl_file))

        ps_tag = d.getVar('ZUBOARD_PS_TAG')
        ps_base = d.getVar('ZUBOARD_PS_BASEURL')
        for f in ps_files:
            name = f.rsplit('.', 1)[0].lower()
            d.appendVar('SRC_URI', " %s/%s/%s;name=%s;downloadfilename=zuboard-ps-%s-%s" % (ps_base, ps_tag, f, name, ps_tag, f))
    elif src == "local":
        rpu_dir = d.getVar('ZUBOARD_RPU_LOCAL_DIR')
        for f in ps_files:
            core = f.rsplit('.', 1)[0]
            d.appendVarFlag('do_install', 'file-checksums', ' %s/%s/build/%s:True' % (rpu_dir, core, f))

        d.appendVarFlag('do_install', 'file-checksums', ' %s:True' % d.getVar('ZUBOARD_PL_LOCAL_FILE'))
    else:
        bb.fatal('Unsupported ZUBOARD_FIRMWARE_SRC="%s"; use "cloud" or "local".' % src)
}

# One checksum per remote file. Names: 'fpga' for the bitstream, and the ELF
# file name minus extension (lower-cased) for each PS core.
SRC_URI[fpga.sha256sum] = "7b9288b9d9873ec514c532835c63d70e35c57a6820ecfd4e7f67942f896d6e68"
SRC_URI[r5c0.sha256sum] = "739cd012520970f3ab4e2cc23e7cf0021a84b32c6d5a3c680f9d55a46fa946c6"
SRC_URI[r5c1.sha256sum] = "a2f348a4843bc6e5ff7de2e07a46c4f747c69156f4b1756b8a7071c803394a5e"

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

# Firmware is board specific: only build for the zudemo machine, and make the
# package machine-specific so a tag/binary change rebuilds cleanly.
COMPATIBLE_MACHINE = "^zudemo$"
PACKAGE_ARCH = "${MACHINE_ARCH}"

do_install[vardeps] += "ZUBOARD_FIRMWARE_SRC ZUBOARD_PS_FILES ZUBOARD_PS_TAG ZUBOARD_PL_TAG ZUBOARD_PL_FILE ZUBOARD_RPU_LOCAL_DIR ZUBOARD_PL_LOCAL_FILE"

do_install() {
    install -d ${D}${FIRMWARE_INSTALL_DIR}

    if [ "${ZUBOARD_FIRMWARE_SRC}" = "local" ]; then
        # RPU: package live sibling build outputs, e.g.
        # ../ZuBoardDemo_RPU/R5c0/build/R5c0.elf -> r5c0.elf.
        for f in ${ZUBOARD_PS_FILES}; do
            core=$(echo "$f" | sed 's/[.][eE][lL][fF]$//')
            dest=$(echo "$f" | tr '[:upper:]' '[:lower:]')
            install -m 0644 ${ZUBOARD_RPU_LOCAL_DIR}/$core/build/$f \
                ${D}${FIRMWARE_INSTALL_DIR}/$dest
        done

        # PL: package live Vivado bitstream output as the canonical target name.
        install -m 0644 ${ZUBOARD_PL_LOCAL_FILE} \
            ${D}${FIRMWARE_INSTALL_DIR}/${ZUBOARD_PL_FILE}
    else
        # PS: one ELF per R5 core. Installed under canonical lower-case names
        # (R5c0.elf -> r5c0.elf) so they match the 'zud' tool and sysfs usage.
        for f in ${ZUBOARD_PS_FILES}; do
            dest=$(echo "$f" | tr '[:upper:]' '[:lower:]')
            install -m 0644 ${WORKDIR}/zuboard-ps-${ZUBOARD_PS_TAG}-$f \
                ${D}${FIRMWARE_INSTALL_DIR}/$dest
        done

        # PL: FPGA bitstream.
        install -m 0644 ${WORKDIR}/zuboard-pl-${ZUBOARD_PL_TAG}-${ZUBOARD_PL_FILE} \
            ${D}${FIRMWARE_INSTALL_DIR}/${ZUBOARD_PL_FILE}
    fi

    # Firmware management CLI -> /usr/bin/zud
    install -d ${D}${bindir}
    install -m 0755 ${WORKDIR}/zud ${D}${bindir}/zud
}

FILES:${PN} = "${FIRMWARE_INSTALL_DIR} ${bindir}/zud"
