# meta-fpga-util

Common FPGA demo utilities shared by ZynqMP board layers such as `meta-zuboard`
and `meta-kr260demo`.

This layer provides:

- `fwctl`, which installs `/usr/bin/fwctl` and `/etc/default/fwctl`.
- `apu-rpu-ctl`, the Linux RPMsg char-device client used by demo images to
  communicate with the RPU control firmware.
- `fpga-firmware-package.bbclass`, a reusable class for packaging prebuilt PL
  bitstreams and Cortex-R firmware ELFs from either GitHub release assets or
  local workspace outputs.
- `zynqmp-firmware`, the shared firmware recipe that every ZynqMP board builds.
  It carries only board-neutral defaults; each board layer supplies its own
  asset URLs, checksums and local paths through a `zynqmp-firmware_%.bbappend`.
- `export-tftpboot-file.bbclass` and `load-jtag-image.tcl`, the ZynqMP
  JTAG/TFTP boot bundle exporter used by MNCOS images.

Board layers should depend on this layer and keep only board-specific data:
machine configuration, DTS/domain YAML, U-Boot environment, firmware asset
URLs/checksums, and any board flasher implementation.

## Board Firmware Pattern

The firmware recipe itself
(`recipes-bsp/zynqmp-firmware/zynqmp-firmware_1.0.bb`) lives here and is shared
by all boards. A board layer only adds a thin `.bbappend` that supplies its own
assets and opts the board in via `COMPATIBLE_MACHINE`:

```bitbake
# meta-<board>/recipes-bsp/zynqmp-firmware/zynqmp-firmware_1.0.bbappend
FPGA_FIRMWARE_RELEASE_TAG = "v0.0.1"
FPGA_FIRMWARE_PS_BASEURL = "https://github.com/example/BoardDemo_PS/releases/download"
FPGA_FIRMWARE_PL_BASEURL = "https://github.com/example/BoardDemo_PL/releases/download"
FPGA_FIRMWARE_PS_DOWNLOAD_PREFIX = "board-ps"
FPGA_FIRMWARE_PL_DOWNLOAD_PREFIX = "board-pl"
FPGA_FIRMWARE_RPU_LOCAL_DIR = "${TOPDIR}/../../BoardDemo_RPU"
FPGA_FIRMWARE_PL_LOCAL_FILE = "${TOPDIR}/../../runtime-generated/bin_file/fpga.bit"

SRC_URI[fpga.sha256sum] = "..."
SRC_URI[r5c0.sha256sum] = "..."
SRC_URI[r5c1.sha256sum] = "..."

# Override the dual-R5 default only if the board ships different ELFs.
# FPGA_FIRMWARE_PS_FILES = "R5c0.elf R5c1.elf"

COMPATIBLE_MACHINE = "^board-machine$"
```

Then add the shared firmware package and `fwctl` to the image for that machine:

```bitbake
IMAGE_INSTALL:append = " zynqmp-firmware fwctl apu-rpu-ctl"
```

Each product build is expected to include `meta-fpga-util` plus exactly one
board layer. Until a board `.bbappend` sets `COMPATIBLE_MACHINE`, the shared
recipe is compatible with no machine and never builds on its own.

For boards strapped to boot from QSPI or another non-JTAG source, set
`JTAG_LOADER_FORCE_JTAG_BOOT = "1"` in the image recipe that inherits
`export-tftpboot-file`. The exported `load-jtag-image.tcl` then writes the
ZynqMP multiboot and boot-mode registers before reset, matching the Kria JTAG
boot flow. Leave the variable at its default `0` for boards such as ZuBoard that
can use the normal cable SRST pulse.

If one `hw_server` exposes multiple boards, pass an XSDB target filter to the
loader, or set `MNCOS_HW_TARGET_FILTER`. The filter is combined with each
normal target selection, so the PSU, PMU, and A53 all come from the same board.
Choose a filter property from `targets -target-properties`; for example, when
the scan-chain index is unique:

```sh
MNCOS_HW_TARGET_FILTER='jtag_device_index == 1' ./load-jtag-image.tcl <hw-server-ip> <tftp-server-ip>
```
