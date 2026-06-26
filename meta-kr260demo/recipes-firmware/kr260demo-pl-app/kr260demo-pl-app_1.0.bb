SUMMARY = "KR260Demo dfx-mgr PL application (bitstream + overlay + shell.json) and R5 firmware"
DESCRIPTION = "Packages the KR260Demo programmable-logic full bitstream, its \
lopper-generated device-tree overlay (a /plugin/ overlay targeting &fpga_full) and an \
auto-generated shell.json into /lib/firmware/xilinx/kr260demo/, so the design loads with \
'xmutil loadapp kr260demo'. The Cortex-R5 firmware R5c0.elf/R5c1.elf are installed into \
the dfx-mgr RPU base layout /lib/firmware/xilinx/${RPU_FW_BASE}/rpu/<core>/ so dfx-mgr \
(>= 2026.1) can load them on demand, and also as flat /lib/firmware copies for manual \
remoteproc loading."

LICENSE = "CLOSED"

# dfx_user_dts (AMD/Xilinx Kria-app packaging class): bootgen-converts the .bit to
# ${PN}.bin, dtc-compiles the PL overlay to ${PN}.dtbo, rewrites the overlay's
# firmware-name to "${PN}.bin", and installs the app (bin + dtbo + shell.json) under
# /lib/firmware/xilinx/${FW_INSTALL_DIR}/. DEPENDS (dtc-native bootgen-native) and the
# virtual/kernel:do_configure compile dependency come from the class.
inherit dfx_user_dts systemd

COMPATIBLE_MACHINE = "^kr260demo$"
PACKAGE_ARCH = "${MACHINE_ARCH}"

# dfx-mgr app/folder name -> "xmutil loadapp kr260demo". (This folder name is what
# xmutil uses, NOT the .bin/.dtbo basenames, which are ${PN}-derived and internal.)
FW_INSTALL_DIR = "kr260demo"

# Boot-time autoload switch. When "1":
#   - PL: /etc/dfx-mgrd/default_firmware = kr260demo, loaded by dfx-mgr-fw-load.service.
#   - R5: kr260demo-rpu-load.service is enabled and loads R5c0/R5c1 after the PL.
# dfx-mgr's default_firmware only takes ONE name, so the R5 cores need their own
# oneshot service -- it cannot be done with default_firmware alone.
# The PL app and the service+script are always packaged; this only controls whether
# they auto-run at boot. When "0" both stay present but inert (load them by hand:
# xmutil loadapp kr260demo; dfx-mgr-client -loadByName R5c0; ... R5c1).
KR260DEMO_DFX_AUTOLOAD ?= "0"

# dfx-mgr (>= 2026.1) RPU base directory name.
#
# dfx-mgr scans the dirs in daemon.conf "firmware_location" (/lib/firmware/xilinx)
# and treats any directory literally named "rpu"/"RPU" as an RPU base design -- no
# shell.json required (see dfx-mgr src/daemon_helper.c accel_dir_add / parse_packages
# and src/rpu.c). Inside that "rpu" dir, each numeric subdir <n> is an RPU slot whose
# number is used verbatim as the remoteproc index: dfx-mgr does
#   echo <fw>.elf > /sys/class/remoteproc/remoteproc<n>/firmware ; echo start > .../state
# Each .elf in slot <n> becomes a loadable accelerator named after the file (sans .elf).
# So:  rpu/0/R5c0.elf -> remoteproc0 (R5 core 0),  rpu/1/R5c1.elf -> remoteproc1 (core 1).
#
# IMPORTANT: this base must NOT live under the PL app dir (kr260demo/) -- that dir has a
# shell.json, and dfx-mgr stops recursing at the first base it finds, so a nested rpu/
# would never be discovered. Hence a sibling base dir, distinct from the PL app dir name.
RPU_FW_BASE ?= "kr260demo-rpu"

# Inputs taken straight from the workspace / gen-machineconf output.
# TOPDIR is <project>/yocto-build/build.
#  - KR260Demo_PL.bit : PL bitstream exported with the SDT handoff, so the packaged
#                       firmware stays in sync with the generated PL overlay.
#  - pl.dtsi          : the lopper-generated /plugin/ overlay (targets &fpga_full) that
#                       gen-machineconf emits under build/conf/dts/kr260demo/pl-overlay-full/
#                       -- already overlay-form, so NO /plugin/ wrap is needed here.
#  - R5c{0,1}.elf     : R5 firmware ELFs
# Keep the overlay path before the raw SDT output path: both contain a pl.dtsi,
# but the dfx_user_dts class needs the /plugin/ overlay form from build/conf.
FILESEXTRAPATHS:prepend := "${TOPDIR}/conf/dts/kr260demo/pl-overlay-full:${TOPDIR}/../../runtime-generated/vivado_SDT_out:${TOPDIR}/../../KR260Demo_RPU/R5c0/build:${TOPDIR}/../../KR260Demo_RPU/R5c1/build:"

SRC_URI = " \
    file://KR260Demo_PL.bit \
    file://pl.dtso \
    file://R5c0.elf \
    file://R5c1.elf \
    file://kr260demo-rpu-load \
    file://kr260demo-rpu-load.service \
"

FW_PATH = "${D}${nonarch_base_libdir}/firmware/xilinx/${FW_INSTALL_DIR}"

# The class produces ${PN}.bin + ${PN}.dtbo with firmware-name == ${PN}.bin (consistent),
# so no rename is done here -- only the manifest and the R5 ELFs are added.
do_install:append() {
    # Auto-generate the dfx-mgr manifest: plain full bitstream, no XRT, no DFX slots.
    printf '{\n    "shell_type": "PL_FLAT",\n    "num_pl_slots": 0,\n    "num_aie_slots": 0\n}\n' > ${FW_PATH}/shell.json

    if [ "${KR260DEMO_DFX_AUTOLOAD}" = "1" ]; then
        # Let dfx-mgr-fw-load.service load this PL design automatically at boot.
        install -d ${D}${sysconfdir}/dfx-mgrd
        printf '%s\n' "${FW_INSTALL_DIR}" > ${D}${sysconfdir}/dfx-mgrd/default_firmware
    fi

    # Cortex-R5 firmware in the dfx-mgr RPU base layout (dfx-mgr >= 2026.1):
    #   /lib/firmware/xilinx/${RPU_FW_BASE}/rpu/<core>/R5c<core>.elf
    # Load on target (PL must be loaded first):
    #   dfx-mgr-client -loadByName R5c0   # -> remoteproc0 (R5 core 0)
    #   dfx-mgr-client -loadByName R5c1   # -> remoteproc1 (R5 core 1)
    install -Dm 0644 ${WORKDIR}/R5c0.elf \
        ${D}${nonarch_base_libdir}/firmware/xilinx/${RPU_FW_BASE}/rpu/0/R5c0.elf
    install -Dm 0644 ${WORKDIR}/R5c1.elf \
        ${D}${nonarch_base_libdir}/firmware/xilinx/${RPU_FW_BASE}/rpu/1/R5c1.elf

    # R5 boot-autoload oneshot (loads R5c0/R5c1 after the PL). Always installed;
    # enabled only when KR260DEMO_DFX_AUTOLOAD=1 (see SYSTEMD_AUTO_ENABLE below).
    install -Dm 0755 ${WORKDIR}/kr260demo-rpu-load ${D}${bindir}/kr260demo-rpu-load
    install -Dm 0644 ${WORKDIR}/kr260demo-rpu-load.service \
        ${D}${systemd_system_unitdir}/kr260demo-rpu-load.service
}

do_install[vardeps] += "FW_INSTALL_DIR KR260DEMO_DFX_AUTOLOAD RPU_FW_BASE"

# Package + (conditionally) enable the R5 autoload service.
SYSTEMD_SERVICE:${PN} = "kr260demo-rpu-load.service"
SYSTEMD_AUTO_ENABLE:${PN} = "${@bb.utils.contains_any('KR260DEMO_DFX_AUTOLOAD', '1', 'enable', 'disable', d)}"

# The class's FILES:${PN} already claims /lib/firmware/xilinx/${FW_INSTALL_DIR};
# add the optional dfx-mgr boot marker, the RPU base tree, and the two flat R5 ELFs.
FILES:${PN} += " \
    ${sysconfdir}/dfx-mgrd/default_firmware \
    ${nonarch_base_libdir}/firmware/xilinx/${RPU_FW_BASE} \
    ${bindir}/kr260demo-rpu-load \
    ${systemd_system_unitdir}/kr260demo-rpu-load.service \
"

# Prebuilt R5 (armv7r) ELFs + a bitstream blob; skip host/target arch QA.
INSANE_SKIP:${PN} += "arch ldflags textrel file-rdeps"
