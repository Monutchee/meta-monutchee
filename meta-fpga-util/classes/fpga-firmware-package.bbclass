# Generic packaging for prebuilt FPGA bitstreams plus Cortex-R firmware ELFs.
#
# Board layers inherit this class from a thin recipe and provide URLs, file
# names, checksums and local checkout paths through FPGA_FIRMWARE_* variables.

FPGA_FIRMWARE_RELEASE_TAG ?= "v0.0.1"
FPGA_FIRMWARE_PS_TAG ?= "${FPGA_FIRMWARE_RELEASE_TAG}"
FPGA_FIRMWARE_PL_TAG ?= "${FPGA_FIRMWARE_RELEASE_TAG}"

# Source switch: "cloud" uses release assets, "local" packages sibling build
# outputs from the developer workspace.
FPGA_FIRMWARE_SRC ?= "cloud"
FPGA_FIRMWARE_PS_FILES ?= ""
FPGA_FIRMWARE_PL_FILE ?= "fpga.bit"
FPGA_FIRMWARE_PS_BASEURL ?= ""
FPGA_FIRMWARE_PL_BASEURL ?= ""
FPGA_FIRMWARE_PS_DOWNLOAD_PREFIX ?= "${BPN}-ps"
FPGA_FIRMWARE_PL_DOWNLOAD_PREFIX ?= "${BPN}-pl"

FPGA_FIRMWARE_RPU_LOCAL_DIR ?= ""
FPGA_FIRMWARE_PS_LOCAL_BUILD_SUBDIR ?= "build"
FPGA_FIRMWARE_PL_LOCAL_FILE ?= ""

FIRMWARE_INSTALL_DIR ?= "/opt/monutchee/${MACHINE}/firmware"

S = "${WORKDIR}"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INHIBIT_SYSROOT_STRIP = "1"
INSANE_SKIP:${PN} += "arch ldflags textrel"

COMPATIBLE_MACHINE ?= "^$"
PACKAGE_ARCH = "${MACHINE_ARCH}"

python () {
    src = (d.getVar('FPGA_FIRMWARE_SRC') or "cloud").strip()
    ps_files = (d.getVar('FPGA_FIRMWARE_PS_FILES') or "").split()
    pl_file = (d.getVar('FPGA_FIRMWARE_PL_FILE') or "").strip()

    if src == "cloud":
        pl_tag = d.getVar('FPGA_FIRMWARE_PL_TAG')
        pl_base = (d.getVar('FPGA_FIRMWARE_PL_BASEURL') or "").rstrip('/')
        pl_prefix = d.getVar('FPGA_FIRMWARE_PL_DOWNLOAD_PREFIX')

        if pl_file:
            if not pl_base:
                bb.fatal('FPGA_FIRMWARE_PL_BASEURL must be set when FPGA_FIRMWARE_SRC="cloud".')
            d.appendVar('SRC_URI', " %s/%s/%s;name=fpga;downloadfilename=%s-%s-%s" %
                        (pl_base, pl_tag, pl_file, pl_prefix, pl_tag, pl_file))

        ps_tag = d.getVar('FPGA_FIRMWARE_PS_TAG')
        ps_base = (d.getVar('FPGA_FIRMWARE_PS_BASEURL') or "").rstrip('/')
        ps_prefix = d.getVar('FPGA_FIRMWARE_PS_DOWNLOAD_PREFIX')

        if ps_files and not ps_base:
            bb.fatal('FPGA_FIRMWARE_PS_BASEURL must be set when FPGA_FIRMWARE_SRC="cloud".')
        for f in ps_files:
            name = f.rsplit('.', 1)[0].lower()
            d.appendVar('SRC_URI', " %s/%s/%s;name=%s;downloadfilename=%s-%s-%s" %
                        (ps_base, ps_tag, f, name, ps_prefix, ps_tag, f))
    elif src == "local":
        d.setVarFlag('do_install', 'nostamp', '1')
    else:
        bb.fatal('Unsupported FPGA_FIRMWARE_SRC="%s"; use "cloud" or "local".' % src)
}

do_install[vardeps] += "FPGA_FIRMWARE_SRC FPGA_FIRMWARE_PS_FILES FPGA_FIRMWARE_PS_TAG FPGA_FIRMWARE_PL_TAG FPGA_FIRMWARE_PL_FILE FPGA_FIRMWARE_RPU_LOCAL_DIR FPGA_FIRMWARE_PS_LOCAL_BUILD_SUBDIR FPGA_FIRMWARE_PL_LOCAL_FILE"

do_install() {
    install -d "${D}${FIRMWARE_INSTALL_DIR}"

    if [ "${FPGA_FIRMWARE_SRC}" = "local" ]; then
        for f in ${FPGA_FIRMWARE_PS_FILES}; do
            core=$(echo "$f" | sed 's/[.][eE][lL][fF]$//')
            dest=$(echo "$f" | tr '[:upper:]' '[:lower:]')
            install -m 0644 \
                "${FPGA_FIRMWARE_RPU_LOCAL_DIR}/$core/${FPGA_FIRMWARE_PS_LOCAL_BUILD_SUBDIR}/$f" \
                "${D}${FIRMWARE_INSTALL_DIR}/$dest"
        done

        if [ -n "${FPGA_FIRMWARE_PL_FILE}" ]; then
            install -m 0644 "${FPGA_FIRMWARE_PL_LOCAL_FILE}" \
                "${D}${FIRMWARE_INSTALL_DIR}/${FPGA_FIRMWARE_PL_FILE}"
        fi
    else
        for f in ${FPGA_FIRMWARE_PS_FILES}; do
            dest=$(echo "$f" | tr '[:upper:]' '[:lower:]')
            install -m 0644 "${WORKDIR}/${FPGA_FIRMWARE_PS_DOWNLOAD_PREFIX}-${FPGA_FIRMWARE_PS_TAG}-$f" \
                "${D}${FIRMWARE_INSTALL_DIR}/$dest"
        done

        if [ -n "${FPGA_FIRMWARE_PL_FILE}" ]; then
            install -m 0644 "${WORKDIR}/${FPGA_FIRMWARE_PL_DOWNLOAD_PREFIX}-${FPGA_FIRMWARE_PL_TAG}-${FPGA_FIRMWARE_PL_FILE}" \
                "${D}${FIRMWARE_INSTALL_DIR}/${FPGA_FIRMWARE_PL_FILE}"
        fi
    fi
}

FILES:${PN} = "${FIRMWARE_INSTALL_DIR}"
