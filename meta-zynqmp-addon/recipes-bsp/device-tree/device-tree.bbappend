FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
# Only the Linux MNCOS DT, never FSBL/PMU standalone multiconfig device trees.
EXTRA_DT_INCLUDE_FILES:append:mncos:zynqmp:class-target = "${@' mncos-multimedia-off.dtsi' if d.getVar('MNCOS_MULTIMEDIA_OFF') == '1' else ''}"

# Carrier overlays can reenable DP after BASE_DTS includes have been applied.
# Enforce the policy on every final Linux DTB, after the recipe bundles overlays.
do_compile[postfuncs] += "mncos_finalize_multimedia_dt"
python mncos_finalize_multimedia_dt() {
    if d.getVar("MNCOS_MULTIMEDIA_OFF") != "1" or not {"mncos", "zynqmp"} <= set(d.getVar("OVERRIDES").split(":")):
        return
    import subprocess
    from pathlib import Path
    dtbs = list(Path(d.getVar("B")).glob("*.dtb"))
    if not dtbs:
        bb.fatal("No final DTBs found for multimedia-off policy")
    for dtb in dtbs:
        for symbol in ("gpu", "zynqmp_dpsub", "zynqmp_dpdma"):
            node = subprocess.check_output(["fdtget", str(dtb), "/__symbols__", symbol], text=True).strip()
            subprocess.run(["fdtput", "-t", "s", str(dtb), node, "status", "disabled"], check=True)
            if subprocess.check_output(["fdtget", str(dtb), node, "status"], text=True).strip() != "disabled":
                bb.fatal("Multimedia node reenabled in final DTB: " + str(dtb) + ":" + node)
}
