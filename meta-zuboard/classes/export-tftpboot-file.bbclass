TFTPBOOT_DEST_DIR ?= "${TOPDIR}/export/tftpboot"
JTAG_LOADER_TCL ?= ""

# Function to copy required files to export/tftpboot
do_copy_tftpboot() {
    set -eu

    DEST_DIR="${TFTPBOOT_DEST_DIR}"
    mkdir -p "${DEST_DIR}"

    echo "==> Copying TFTP boot files to ${DEST_DIR}"

    # Track success/failure
    local retVal=0

    # Helper: copy and handle missing files gracefully
    copy_or_warn() {
        local src="$1"
        local dst="$2"

        if [ -e "${src}" ]; then
            cp -v "${src}" "${dst}" || retVal=1
        else
            echo "WARN: Missing ${src}" >&2
            retVal=1
        fi
    }

    # FPGA bitstream (manual copy if needed)
    # Example: copy_or_warn "${DEPLOY_DIR_IMAGE}/fpga.bit" "${DEST_DIR}"

    # PMU firmware
    copy_or_warn "${DEPLOY_DIR_IMAGE}/pmu-firmware-${MACHINE}.elf" "${DEST_DIR}/pmufw.elf"

    # FSBL
    copy_or_warn "${DEPLOY_DIR_IMAGE}/fsbl-${MACHINE}.elf" "${DEST_DIR}/fsbl.elf"

    # System Device Tree
    copy_or_warn "${DEPLOY_DIR_IMAGE}/system.dtb" "${DEST_DIR}/system.dtb"

    # TF-A BL31 (sysroots or deploy)
    copy_or_warn "${DEPLOY_DIR_IMAGE}/arm-trusted-firmware.elf" "${DEST_DIR}/tfa.elf"

    # U-Boot
    copy_or_warn "${DEPLOY_DIR_IMAGE}/u-boot.elf" "${DEST_DIR}/u-boot.elf"

    # Kernel Image
    copy_or_warn "${DEPLOY_DIR_IMAGE}/Image" "${DEST_DIR}/Image"

    # Rootfs
    copy_or_warn "${DEPLOY_DIR_IMAGE}/${PN}-${MACHINE}.rootfs.cpio.gz.u-boot" "${DEST_DIR}/rootfs.cpio.gz.u-boot"

    # Boot script
    copy_or_warn "${DEPLOY_DIR_IMAGE}/boot.scr" "${DEST_DIR}/boot.scr"

    # Optional JTAG loader. Network settings are runtime TCL arguments.
    if [ -n "${JTAG_LOADER_TCL}" ]; then
        if [ -e "${JTAG_LOADER_TCL}" ]; then
            install -m 0755 "${JTAG_LOADER_TCL}" "${DEST_DIR}/load-jtag-image.tcl" || retVal=1
        else
            echo "WARN: Missing ${JTAG_LOADER_TCL}" >&2
            retVal=1
        fi
    fi

    # Summary
    if [ ${retVal} -eq 0 ]; then
        echo "✅ All required files copied successfully to ${DEST_DIR}"
    else
        echo "⚠️  Some files were missing or failed to copy — check logs above."
    fi

    return ${retVal}
}
#Ensures the task always runs.
do_copy_tftpboot[nostamp] = "1"
addtask copy_tftpboot after do_image_complete before do_build
