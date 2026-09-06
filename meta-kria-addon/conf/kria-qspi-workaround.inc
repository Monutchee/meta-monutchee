# meta-kria v2026.1 keeps k26-smk.inc's unconditional kria-qspi dependency,
# but kria-qspi itself is compatible only with multidomain machines. Drop the
# unbuildable dependency for single-domain Kria images until upstream aligns the
# include and recipe compatibility.
KRIA_QSPI_WORKAROUND_RELEASES ?= "v2026.1"

# True when this build is a single-domain Kria on an affected release, i.e. the
# case where kria-qspi is dropped below.
KRIA_QSPI_WORKAROUND_ACTIVE = "${@'1' if (d.getVar('XILINX_RELEASE_VERSION') in (d.getVar('KRIA_QSPI_WORKAROUND_RELEASES') or '').split() and 'multidomain' not in (d.getVar('MACHINEOVERRIDES') or '')) else ''}"

EXTRA_IMAGEDEPENDS:remove:kria = "${@'kria-qspi' if d.getVar('KRIA_QSPI_WORKAROUND_ACTIVE') else ''}"

# The kria machine confs set QEMU_FLASH_FILE = "kria-qspi-${MACHINE}" so qemu's
# QSPI MTD drive boots from the kria-qspi image. But with kria-qspi dropped above
# that file is never deployed, and QSPI-mode qemu fails with:
#   "MTD file '.../kria-qspi-<machine>.bin' not found"
# Point qemu back at the generic qemu-qspi image (meta-xilinx's default flash
# file, built automatically via machine-xilinx-qemu.inc when QEMU_FLASH_TYPE is
# qspi) so runqemu can boot without the hardware A/B qspi layout.
QEMU_FLASH_FILE:kria = "${@('qemu-qspi-%s' % d.getVar('MACHINE')) if d.getVar('KRIA_QSPI_WORKAROUND_ACTIVE') else ('kria-qspi-%s' % d.getVar('MACHINE'))}"

# The kria machine confs also append "-boot multiboot=64" (0x200000 / 32KiB),
# matching the kria-qspi flash layout where boot.bin lives in the A-image slot
# at 0x200000. The generic qemu-qspi image instead writes boot.bin at offset 0,
# so the boot ROM must search from there; otherwise qemu scans from 2MB and
# fails with "No valid boot image found in MTD file".
#
# We rebuild the MTD -drive here (we can't reuse qemu_mtd_params() -- it lives in
# qemuboot-xilinx.bbclass and is out of scope when the image, or runqemu's own
# `bitbake -e`, expands this variable, raising NameError). The base value carries
# NO multiboot flag, so when active the boot ROM defaults to offset 0 to match
# qemu-qspi. The machine conf still unconditionally appends " -boot multiboot=64";
# since bitbake applies every :remove after every :append (regardless of parse
# order), the :remove below strips that append for the workaround case only,
# leaving multidomain machines with their stock multiboot=64.
QEMU_HW_FLASH:kria = "${@('-drive file=@DEPLOY_DIR_IMAGE@/%s.bin,if=mtd,format=raw,index=0' % d.getVar('QEMU_FLASH_FILE')) if d.getVar('QEMU_FLASH_TYPE') == 'qspi' else ''}"
QEMU_HW_FLASH:remove:kria = "${@(' -boot multiboot=%d' % int(int('0x20_0000', 0) / (32 * 1024))) if d.getVar('KRIA_QSPI_WORKAROUND_ACTIVE') else ''}"
