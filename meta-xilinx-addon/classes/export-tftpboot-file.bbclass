TFTPBOOT_DEST_DIR ?= "${TOPDIR}/export/tftpboot"
JTAG_LOADER_TCL ?= ""
JTAG_LOADER_FORCE_JTAG_BOOT ?= "0"

# Copy the ZynqMP boot bundle used by the JTAG/TFTP test flow.
do_copy_tftpboot() {
    set -eu

    DEST_DIR="${TFTPBOOT_DEST_DIR}"
    mkdir -p "${DEST_DIR}"

    echo "==> Copying TFTP boot files to ${DEST_DIR}"

    retVal=0

    copy_or_warn() {
        src="$1"
        dst="$2"

        if [ -e "${src}" ]; then
            cp -v "${src}" "${dst}" || retVal=1
        else
            echo "WARN: Missing ${src}" >&2
            retVal=1
        fi
    }

    copy_or_warn "${DEPLOY_DIR_IMAGE}/pmu-firmware-${MACHINE}.elf" "${DEST_DIR}/pmufw.elf"
    copy_or_warn "${DEPLOY_DIR_IMAGE}/fsbl-${MACHINE}.elf" "${DEST_DIR}/fsbl.elf"
    copy_or_warn "${DEPLOY_DIR_IMAGE}/system.dtb" "${DEST_DIR}/system.dtb"
    copy_or_warn "${DEPLOY_DIR_IMAGE}/arm-trusted-firmware.elf" "${DEST_DIR}/tfa.elf"
    copy_or_warn "${DEPLOY_DIR_IMAGE}/u-boot.elf" "${DEST_DIR}/u-boot.elf"
    copy_or_warn "${DEPLOY_DIR_IMAGE}/Image" "${DEST_DIR}/Image"
    copy_or_warn "${DEPLOY_DIR_IMAGE}/${PN}-${MACHINE}.rootfs.cpio.gz.u-boot" "${DEST_DIR}/rootfs.cpio.gz.u-boot"

    # Boot script
    copy_or_warn "${DEPLOY_DIR_IMAGE}/boot.scr" "${DEST_DIR}/boot.scr"

    # Optional JTAG loader. Network settings are runtime TCL arguments.
    if [ -n "${JTAG_LOADER_TCL}" ]; then
        if [ -e "${JTAG_LOADER_TCL}" ]; then
            if sed -e 's|@JTAG_LOADER_FORCE_JTAG_BOOT@|${JTAG_LOADER_FORCE_JTAG_BOOT}|g' \
                "${JTAG_LOADER_TCL}" > "${DEST_DIR}/load-jtag-image.tcl"; then
                chmod 0755 "${DEST_DIR}/load-jtag-image.tcl" || retVal=1
            else
                retVal=1
            fi
        else
            echo "WARN: Missing ${JTAG_LOADER_TCL}" >&2
            retVal=1
        fi
    fi

    if [ ${retVal} -eq 0 ]; then
        echo "All required files copied successfully to ${DEST_DIR}"
    else
        echo "Some files were missing or failed to copy; check logs above."
    fi

    return ${retVal}
}

do_copy_tftpboot[nostamp] = "1"
addtask copy_tftpboot after do_image_complete before do_build
