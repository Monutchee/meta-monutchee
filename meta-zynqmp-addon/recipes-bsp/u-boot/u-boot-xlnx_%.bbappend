FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append:mncos:zynqmp:class-target = "${@' file://0001-zynqmp-multimedia-off.patch file://mncos-multimedia-off.cfg' if d.getVar('MNCOS_MULTIMEDIA_OFF') == '1' else ''}"

do_configure[postfuncs] += "mncos_check_multimedia_uboot"
python mncos_check_multimedia_uboot() {
    if d.getVar("MNCOS_MULTIMEDIA_OFF") != "1" or not {"mncos", "zynqmp"} <= set(d.getVar("OVERRIDES").split(":")):
        return
    from pathlib import Path
    configs = list(Path(d.getVar("B")).rglob(".config"))
    if not configs:
        bb.fatal("Missing U-Boot configuration for multimedia-off validation")
    for config in configs:
        text = config.read_text().splitlines()
        if "CONFIG_ZYNQMP_MULTIMEDIA_OFF=y" not in text:
            bb.fatal("U-Boot multimedia shutdown was not enabled: " + str(config))
        if any(line in text for line in ("CONFIG_VIDEO=y", "CONFIG_VIDEO_ZYNQMP_DPSUB=y", "CONFIG_XILINX_DPDMA=y")):
            bb.fatal("U-Boot graphics were reenabled: " + str(config))
}

do_deploy:append:mncos:zynqmp() {
    if [ "${MNCOS_MULTIMEDIA_OFF}" = "1" ]; then
        install -m 0644 ${B}/.config ${DEPLOYDIR}/mncos-u-boot-${MACHINE}.config
    fi
}
