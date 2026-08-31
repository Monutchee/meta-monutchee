inherit mnc-artifact

MNC_ARTIFACT_VENDOR = "xilinx"
MNC_ARTIFACT_OPERATION = "jtag-boot"
MNC_ARTIFACT_EXECUTOR_TYPE = "xilinx-xsdb"
MNC_ARTIFACT_ENTRYPOINT = "jtag/load-jtag-image.tcl"
MNC_ARTIFACT_TFTP_ROOT = "tftp"

MNC_XILINX_JTAG_IMAGE_RECIPE ?= ""
MNC_XILINX_JTAG_LOADER_TCL ?= "${XILINX_ADDON_LAYERDIR}/recipes-core/images/files/load-jtag-image-station.tcl"
MNC_XILINX_JTAG_FORCE_JTAG_BOOT ?= "0"

MNC_XILINX_JTAG_FSBL ?= "${DEPLOY_DIR_IMAGE}/fsbl-${MACHINE}.elf"
MNC_XILINX_JTAG_PMUFW ?= "${DEPLOY_DIR_IMAGE}/pmu-firmware-${MACHINE}.elf"
MNC_XILINX_JTAG_TFA ?= "${DEPLOY_DIR_IMAGE}/arm-trusted-firmware.elf"
MNC_XILINX_JTAG_UBOOT ?= "${DEPLOY_DIR_IMAGE}/u-boot.elf"
MNC_XILINX_JTAG_KERNEL ?= "${DEPLOY_DIR_IMAGE}/Image"
MNC_XILINX_JTAG_DTB ?= "${DEPLOY_DIR_IMAGE}/system.dtb"
MNC_XILINX_JTAG_BOOT_SCRIPT ?= "${DEPLOY_DIR_IMAGE}/boot.scr"
MNC_XILINX_JTAG_ROOTFS ?= "${DEPLOY_DIR_IMAGE}/${MNC_XILINX_JTAG_IMAGE_RECIPE}-${MACHINE}.rootfs.cpio.gz.u-boot"

python __anonymous() {
    image_recipe = d.getVar("MNC_XILINX_JTAG_IMAGE_RECIPE")
    if not image_recipe:
        bb.fatal(
            "%s inherits xilinx-jtag-artifact but does not set "
            "MNC_XILINX_JTAG_IMAGE_RECIPE" % d.getVar("PN")
        )
    d.appendVarFlag("do_deploy", "depends", " %s:do_image_complete" % image_recipe)
}

mnc_xilinx_install_required() {
    source="$1"
    destination="$2"
    mode="$3"

    if [ ! -f "${source}" ]; then
        bbfatal "Required Xilinx JTAG artifact input is missing: ${source}"
    fi
    install -m "${mode}" "${source}" "${destination}"
}

# Yocto's versioned image filename is derived from that image's
# SOURCE_DATE_EPOCH. Reuse it so the Station artifact identity follows the
# image it contains instead of this wrapper recipe's fallback epoch.
mnc_artifact_resolve_metadata() {
    resolved_rootfs="$(readlink -f "${MNC_XILINX_JTAG_ROOTFS}")"
    source_name="$(basename "${resolved_rootfs}")"
    source_build_id="$(printf '%s\n' "${source_name}" | \
        sed -n 's/^.*\.rootfs-\([0-9]\{14\}\)\..*$/\1/p')"
    if [ -z "${source_build_id}" ]; then
        bbfatal "Cannot derive the source image build ID from ${resolved_rootfs}"
    fi

    timestamp_metadata="$("${PYTHON}" -c \
        'from datetime import datetime, timezone; import sys; value = datetime.strptime(sys.argv[1], "%Y%m%d%H%M%S").replace(tzinfo=timezone.utc); print(int(value.timestamp()), value.strftime("%Y-%m-%dT%H:%M:%SZ"))' \
        "${source_build_id}")"
    set -- ${timestamp_metadata}
    if [ "$#" -ne 2 ]; then
        bbfatal "Cannot convert source image build ID ${source_build_id}"
    fi
    artifact_epoch="$1"
    artifact_created_utc="$2"
    artifact_build_id="${source_build_id}"
}

mnc_artifact_populate() {
    case "${MNC_XILINX_JTAG_FORCE_JTAG_BOOT}" in
        0|1) ;;
        *) bbfatal "MNC_XILINX_JTAG_FORCE_JTAG_BOOT must be 0 or 1" ;;
    esac

    install -d \
        "${MNC_ARTIFACT_STAGING_DIR}/jtag" \
        "${MNC_ARTIFACT_STAGING_DIR}/tftp"

    if [ ! -f "${MNC_XILINX_JTAG_LOADER_TCL}" ]; then
        bbfatal "Station JTAG loader is missing: ${MNC_XILINX_JTAG_LOADER_TCL}"
    fi
    if ! grep -q '@JTAG_LOADER_FORCE_JTAG_BOOT@' "${MNC_XILINX_JTAG_LOADER_TCL}"; then
        bbfatal "Station JTAG loader lacks the force-boot substitution token"
    fi
    if ! grep -q 'MNC_STATION_TARGET_SELECTOR_V2' "${MNC_XILINX_JTAG_LOADER_TCL}"; then
        bbfatal "Station JTAG loader lacks multi-device target selection"
    fi
    sed \
        -e 's|@JTAG_LOADER_FORCE_JTAG_BOOT@|${MNC_XILINX_JTAG_FORCE_JTAG_BOOT}|g' \
        "${MNC_XILINX_JTAG_LOADER_TCL}" \
        > "${MNC_ARTIFACT_STAGING_DIR}/jtag/load-jtag-image.tcl"
    chmod 0755 "${MNC_ARTIFACT_STAGING_DIR}/jtag/load-jtag-image.tcl"

    mnc_xilinx_install_required \
        "${MNC_XILINX_JTAG_FSBL}" \
        "${MNC_ARTIFACT_STAGING_DIR}/jtag/fsbl.elf" 0644
    mnc_xilinx_install_required \
        "${MNC_XILINX_JTAG_PMUFW}" \
        "${MNC_ARTIFACT_STAGING_DIR}/jtag/pmufw.elf" 0644
    mnc_xilinx_install_required \
        "${MNC_XILINX_JTAG_TFA}" \
        "${MNC_ARTIFACT_STAGING_DIR}/jtag/tfa.elf" 0644
    mnc_xilinx_install_required \
        "${MNC_XILINX_JTAG_UBOOT}" \
        "${MNC_ARTIFACT_STAGING_DIR}/jtag/u-boot.elf" 0644

    mnc_xilinx_install_required \
        "${MNC_XILINX_JTAG_BOOT_SCRIPT}" \
        "${MNC_ARTIFACT_STAGING_DIR}/tftp/boot.scr" 0644
    mnc_xilinx_install_required \
        "${MNC_XILINX_JTAG_KERNEL}" \
        "${MNC_ARTIFACT_STAGING_DIR}/tftp/Image" 0644
    mnc_xilinx_install_required \
        "${MNC_XILINX_JTAG_DTB}" \
        "${MNC_ARTIFACT_STAGING_DIR}/tftp/system.dtb" 0644
    mnc_xilinx_install_required \
        "${MNC_XILINX_JTAG_ROOTFS}" \
        "${MNC_ARTIFACT_STAGING_DIR}/tftp/rootfs.cpio.gz.u-boot" 0644
}

do_deploy[file-checksums] += "${MNC_XILINX_JTAG_LOADER_TCL}:True"
