FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append = " file://modify-kernel-features.cfg"
SRC_URI:append:mncos = "${@' file://0001-media-xilinx-dprxss-select-drm-kms-helper.patch file://mncos-headless.cfg' if d.getVar('MNCOS_HEADLESS') == '1' else ''}"

# Check the resolved configuration: a later BSP fragment or Kconfig select
# must not silently restore a display driver. Missing symbols are disabled.
python () {
    if d.getVar("DISTRO") == "mncos":
        d.appendVarFlag("do_configure", "postfuncs", " mncos_check_headless_kernel")
}
python mncos_check_headless_kernel() {
    if d.getVar("MNCOS_HEADLESS") != "1":
        return
    import re
    from pathlib import Path
    fragment = Path(d.getVar("WORKDIR")) / "mncos-headless.cfg"
    disabled = set(re.findall(r"^# (CONFIG_\w+) is not set$", fragment.read_text(), re.M))
    config = (Path(d.getVar("B")) / ".config").read_text()
    enabled = set(re.findall(r"^(CONFIG_\w+)=[ym]$", config, re.M))
    unexpected = sorted(disabled & enabled)
    if unexpected:
        bb.fatal("MNCOS_HEADLESS kernel enables display/GPU symbols: " + " ".join(unexpected))
    if "CONFIG_VIDEO_XILINX_DPRXSS" in enabled and "CONFIG_DRM_KMS_HELPER" not in enabled:
        bb.fatal("Xilinx DisplayPort capture requires DRM_KMS_HELPER to link without display drivers")
}

# Include the effective kernel configuration in deployment and sstate output.
do_deploy:append:mncos() {
    install -m 0644 ${B}/.config ${DEPLOYDIR}/mncos-kernel-${MACHINE}.config
}
