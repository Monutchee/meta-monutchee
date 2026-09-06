# Distribution validation shared by all MNCOS images, including flash images.
addhandler mncos_validate_configuration
mncos_validate_configuration[eventmask] = "bb.event.ConfigParsed"
python mncos_validate_configuration() {
    if d.getVar("MNCOS_HEADLESS") not in ("0", "1"):
        bb.fatal('MNCOS_HEADLESS must be "0" or "1"')
    if d.getVar("MNCOS_MULTIMEDIA_OFF") not in ("0", "1"):
        bb.fatal('MNCOS_MULTIMEDIA_OFF must be "0" or "1"')
    if d.getVar("MNCOS_MULTIMEDIA_OFF") == "1" and d.getVar("MNCOS_HEADLESS") != "1":
        bb.fatal('MNCOS_MULTIMEDIA_OFF requires MNCOS_HEADLESS = "1"')
}

# cve-check otherwise silently skips a recipe when the database is absent.
python mncos_require_cve_database() {
    if not os.path.isfile(d.getVar("CVE_CHECK_DB_FILE")):
        bb.fatal("MNCOS requires a CVE database; the vulnerability report would be incomplete")
}
do_cve_check[prefuncs] += "mncos_require_cve_database"

ROOTFS_POSTPROCESS_COMMAND:append = " mncos_validate_headless_rootfs;"
python mncos_validate_headless_rootfs() {
    if d.getVar("MNCOS_HEADLESS") != "1":
        return
    from oe.rootfs import image_list_installed_packages
    import oe.packagedata

    forbidden = {
        "mesa", "mesa-gl", "libmali-xlnx", "wayland", "wayland-protocols",
        "weston", "xserver-xorg", "xwayland", "libx11", "libxcb", "psplash",
        "directfb", "kernel-module-mali",
    }
    if d.getVar("MNCOS_MULTIMEDIA_OFF") == "1":
        forbidden.update({"alsa-lib", "alsa-utils", "alsa-plugins", "pulseaudio",
                          "pipewire", "v4l-utils", "ffmpeg", "libvcu-omxil",
                          "vcu-firmware", "vcu-ctrlsw", "vcu-modules"})
    unexpected = []
    for package in image_list_installed_packages(d):
        info = oe.packagedata.read_pkgdatafile(
            os.path.join(d.getVar("PKGDATA_DIR"), "runtime-reverse", package))
        recipe = info.get("PN", "")
        if recipe in forbidden or (d.getVar("MNCOS_MULTIMEDIA_OFF") == "1"
                                   and recipe.startswith("gstreamer")):
            unexpected.append("%s (%s)" % (package, info["PN"]))
    if unexpected:
        bb.fatal("MNCOS_HEADLESS image contains graphics packages: " + ", ".join(sorted(unexpected)))
}
