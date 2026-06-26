DESCRIPTION = "Minimal MNCOS image for ZuBoard"
LICENSE = "MIT"

require recipes-core/images/include/mncos-image-common.inc

COMPATIBLE_MACHINE = "^kr260demo$"

MNCOS_IMAGE_ROLE = "main"
MNCOS_IMAGE_LABEL = "MNCOS ZUBOARD MAIN SYSTEM IMAGE"

# Product-specific packages.
#   :append adds board-specific packages on top of the shared MNCOS base set.
#   :remove (if needed) trims packages from the base set, e.g.:
#       IMAGE_INSTALL:remove = " htop"
#
# PL bitstream loading: on the Kria K26 SOM the PL must be loaded via dfx-mgr
# (xmutil loadapp), which applies the PL device-tree overlay and brings up the
# PL clock + PS<->PL AXI bridge. The raw fpga_manager path (fwctl) is intentionally
# NOT installed on this board: it bypasses the overlay, leaving the PL AXI
# interface unbrought-up so any AXI access from the APU/R5 hangs. R5 firmware
# load/start is handled by apu-rpu-ctl.
IMAGE_INSTALL:append = " \
    apu-rpu-ctl \
    dfx-mgr \
    kr260demo-pl-app \
    lmsensors-config-kria-fancontrol \
"

# This product image does not need the generated machine's VCU codec stack.
IMAGE_FEATURES:remove = "hwcodecs"

# --- kria-qspi workaround (meta-kria v2026.1) --------------------------------
# meta-kria v2026.1 narrowed kria-qspi's COMPATIBLE_MACHINE to *-multidomain
# machines only (commit 0e181e1 "kria-qspi: restrict compatible machines to
# multidomain"), but the shared SOM include k26-smk.inc still does an
# unconditional `EXTRA_IMAGEDEPENDS += "kria-qspi"`. On our single-domain
# kr260demo that forces a dependency on a recipe that refuses to build for the
# machine, giving: ERROR: Nothing PROVIDES 'kria-qspi' (not in COMPATIBLE_MACHINE).
# Per AMD's Kria Yocto docs the single-domain boot artifact is boot.bin
# (xilinx-bootbin); kria-qspi is the multidomain QSPI A/B/recovery image. So we
# drop the unbuildable dep here.
#
# Guards -- the removal fires only when BOTH conditions hold:
#  1. Release: the COMPATIBLE_MACHINE restriction was introduced in meta-kria
#     v2026.1. On earlier releases kria-qspi IS buildable for single-domain
#     machines, so dropping it there would be wrong. XILINX_RELEASE_VERSION (set
#     by meta-xilinx-core/conf/layer.conf) tracks the checked-out xlnx-rel-vYYYY.N
#     tag. Add releases to KRIA_QSPI_WORKAROUND_RELEASES if a later one still
#     ships the unconditional dep -- or drop the value once k26-smk.inc is fixed
#     upstream so we stop masking a then-real dependency.
#  2. Machine: only non-multidomain machines hit the mismatch. On a
#     *-multidomain machine kria-qspi IS compatible, so 'multidomain' in
#     MACHINEOVERRIDES makes the removal expand to empty and the QSPI image builds.
KRIA_QSPI_WORKAROUND_RELEASES = "v2026.1"
EXTRA_IMAGEDEPENDS:remove = "${@'kria-qspi' if (d.getVar('XILINX_RELEASE_VERSION') in (d.getVar('KRIA_QSPI_WORKAROUND_RELEASES') or '').split() and 'multidomain' not in (d.getVar('MACHINEOVERRIDES') or '')) else ''}"

# Board-specific dev flow: TFTP/JTAG boot export (provided by meta-fpga-util).
IMAGE_CLASSES:append = " export-tftpboot-file"
JTAG_LOADER_TCL = "${FPGA_UTIL_LAYERDIR}/recipes-core/images/files/load-jtag-image.tcl"
JTAG_LOADER_FORCE_JTAG_BOOT = "1"
do_copy_tftpboot[file-checksums] += "${JTAG_LOADER_TCL}:True"

# (Optional) Change destination directory on machine specific directory
# TFTPBOOT_DEST_DIR = "${TOPDIR}/export/tftpboot/${MACHINE}"
