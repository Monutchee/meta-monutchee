# The FSBL links this generated PM configuration through libxilpm.a.
# Run after upstream lopper generation/CMake and before compilation.
MNCOS_MULTIMEDIA_OFF ??= "0"
do_configure[postfuncs] += "mncos_gpu_apu_ownership"

python mncos_gpu_apu_ownership() {
    import pathlib
    import re

    if d.getVar("MNCOS_MULTIMEDIA_OFF") != "1" or "zynqmp" not in d.getVar("OVERRIDES").split(":"):
        return
    if d.getVar("ESW_MACHINE") != "psu_cortexa53_0":
        return

    source = pathlib.Path(d.getVar("S"))
    generated = source / "pm_cfg_obj.c"
    text = generated.read_text()
    # Narrow only GPU slave permissions. Preserve master, reset, PLL and every
    # non-GPU entry verbatim. A custom GPU preallocation needs explicit review.
    start = text.index("/* SLAVE SECTION */")
    end = text.index("/* PREALLOC SECTION */", start)
    slaves = text[start:end]
    if re.search(r"\bNODE_GPU(?:_PP_[01])?\s*,", text[end:]):
        bb.fatal("Multimedia-off conflicts with a GPU preallocation in the PM configuration")
    apu = "PM_CONFIG_IPI_PSU_CORTEXA53_0_MASK"
    allowed = {apu, "PM_CONFIG_IPI_PSU_CORTEXR5_0_MASK", "PM_CONFIG_IPI_PSU_CORTEXR5_1_MASK"}
    for node in ("NODE_GPU", "NODE_GPU_PP_0", "NODE_GPU_PP_1"):
        pattern = r"(\b" + node + r",\s*PM_SLAVE_FLAG_IS_SHAREABLE,\s*)([^,]+)(,)"
        matches = list(re.finditer(pattern, slaves))
        if len(matches) != 1:
            bb.fatal("Unexpected PM configuration for " + node)
        masks = {part.strip() for part in matches[0][2].split("|")}
        if apu not in masks or not masks <= allowed:
            bb.fatal("Refusing to expand or reinterpret GPU ownership for " + node)
        slaves = re.sub(pattern, lambda m: m[1] + apu + m[3], slaves)
    text = text[:start] + slaves + text[end:]
    generated.write_text(text)
    (source / d.getVar("ESW_COMPONENT_SRC").lstrip("/") / "zynqmp/client/common/pm_cfg_obj.c").write_text(text)
}
