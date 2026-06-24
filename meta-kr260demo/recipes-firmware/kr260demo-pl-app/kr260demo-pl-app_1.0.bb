SUMMARY = "KR260Demo dfx-mgr PL application (bitstream + overlay + shell.json) and R5 firmware"
DESCRIPTION = "Packages the KR260Demo programmable-logic full bitstream, its \
lopper-generated device-tree overlay (a /plugin/ overlay targeting &fpga_full) and an \
auto-generated shell.json into /lib/firmware/xilinx/kr260demo/, so the design loads with \
'xmutil loadapp kr260demo'. The Cortex-R5 firmware R5c0.elf/R5c1.elf are installed into \
/lib/firmware for manual remoteproc loading; dfx-mgr/xmutil on the 2025.2 release does \
NOT load RPU firmware."

LICENSE = "CLOSED"

# dfx_user_dts (AMD/Xilinx Kria-app packaging class): bootgen-converts the .bit to
# ${PN}.bin, dtc-compiles the PL overlay to ${PN}.dtbo, rewrites the overlay's
# firmware-name to "${PN}.bin", and installs the app (bin + dtbo + shell.json) under
# /lib/firmware/xilinx/${FW_INSTALL_DIR}/. DEPENDS (dtc-native bootgen-native) and the
# virtual/kernel:do_configure compile dependency come from the class.
inherit dfx_user_dts

COMPATIBLE_MACHINE = "^kr260demo$"
PACKAGE_ARCH = "${MACHINE_ARCH}"

# dfx-mgr app/folder name -> "xmutil loadapp kr260demo". (This folder name is what
# xmutil uses, NOT the .bin/.dtbo basenames, which are ${PN}-derived and internal.)
FW_INSTALL_DIR = "kr260demo"

# Kill switch for boot-time PL loading by dfx-mgr-fw-load.service. The PL app is
# still packaged either way; this controls only /etc/dfx-mgrd/default_firmware.
KR260DEMO_DFX_AUTOLOAD ?= "0"

# Inputs taken straight from the workspace / gen-machineconf output.
# TOPDIR is <project>/yocto-build/build.
#  - KR260Demo_PL.bit : PL bitstream, named on the Vivado side, in runtime-generated/bin_file
#  - pl.dtsi          : the lopper-generated /plugin/ overlay (targets &fpga_full) that
#                       gen-machineconf emits under build/conf/dts/kr260demo/pl-overlay-full/
#                       -- already overlay-form, so NO /plugin/ wrap is needed here.
#  - R5c{0,1}.elf     : R5 firmware ELFs
FILESEXTRAPATHS:prepend := "${TOPDIR}/../../runtime-generated/bin_file:${TOPDIR}/conf/dts/kr260demo/pl-overlay-full:${TOPDIR}/../../KR260Demo_RPU/R5c0/build:${TOPDIR}/../../KR260Demo_RPU/R5c1/build:"

SRC_URI = " \
    file://KR260Demo_PL.bit \
    file://pl.dtsi \
    file://R5c0.elf \
    file://R5c1.elf \
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

    # Cortex-R5 firmware into /lib/firmware for manual remoteproc loading:
    #   echo R5c0.elf > /sys/class/remoteproc/remoteproc0/firmware ; echo start > .../state
    install -Dm 0644 ${WORKDIR}/R5c0.elf ${D}${nonarch_base_libdir}/firmware/R5c0.elf
    install -Dm 0644 ${WORKDIR}/R5c1.elf ${D}${nonarch_base_libdir}/firmware/R5c1.elf
}

do_install[vardeps] += "FW_INSTALL_DIR KR260DEMO_DFX_AUTOLOAD"

# The class's FILES:${PN} already claims /lib/firmware/xilinx/${FW_INSTALL_DIR};
# add the optional dfx-mgr boot marker and the two R5 ELFs we drop directly in
# /lib/firmware.
FILES:${PN} += " \
    ${sysconfdir}/dfx-mgrd/default_firmware \
    ${nonarch_base_libdir}/firmware/R5c0.elf \
    ${nonarch_base_libdir}/firmware/R5c1.elf \
"

# Prebuilt R5 (armv7r) ELFs + a bitstream blob; skip host/target arch QA.
INSANE_SKIP:${PN} += "arch ldflags textrel file-rdeps"
