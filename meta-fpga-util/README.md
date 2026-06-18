# meta-fpga-util

Common FPGA demo utilities shared by board layers such as `meta-zuboard` and a
future `meta-kr260demo`.

This layer provides:

- `fwctl`, which installs `/usr/bin/fwctl` and `/etc/default/fwctl`.
- `apu-rpu-ctl`, the Linux RPMsg char-device client used by demo images to
  communicate with the RPU control firmware.
- `fpga-firmware-package.bbclass`, a reusable class for packaging prebuilt PL
  bitstreams and Cortex-R firmware ELFs from either GitHub release assets or
  local workspace outputs.
- `export-tftpboot-file.bbclass` and `load-jtag-image.tcl`, the ZynqMP
  JTAG/TFTP boot bundle exporter used by MNCOS images.

Board layers should depend on this layer and keep only board-specific data:
machine configuration, DTS/domain YAML, U-Boot environment, firmware asset
URLs/checksums, and any board flasher implementation.

## Board Firmware Recipe Pattern

A board layer only needs a thin recipe:

```bitbake
SUMMARY = "Board demo FPGA bitstream and Cortex-R firmware"
LICENSE = "CLOSED"

FPGA_FIRMWARE_RELEASE_TAG = "v0.0.1"
FPGA_FIRMWARE_PS_FILES = "R5c0.elf R5c1.elf"
FPGA_FIRMWARE_PL_FILE = "fpga.bit"
FPGA_FIRMWARE_PS_BASEURL = "https://github.com/example/BoardDemo_PS/releases/download"
FPGA_FIRMWARE_PL_BASEURL = "https://github.com/example/BoardDemo_PL/releases/download"
FPGA_FIRMWARE_RPU_LOCAL_DIR = "${TOPDIR}/../../BoardDemo_RPU"
FPGA_FIRMWARE_PL_LOCAL_FILE = "${TOPDIR}/../../runtime-generated/bin_file/fpga.bit"
FPGA_FIRMWARE_PS_DOWNLOAD_PREFIX = "board-ps"
FPGA_FIRMWARE_PL_DOWNLOAD_PREFIX = "board-pl"

SRC_URI[fpga.sha256sum] = "..."
SRC_URI[r5c0.sha256sum] = "..."
SRC_URI[r5c1.sha256sum] = "..."

COMPATIBLE_MACHINE = "^board-machine$"

inherit fpga-firmware-package
```

Then add the board firmware package and `fwctl` to the image for that machine:

```bitbake
IMAGE_INSTALL:append:board-machine = " board-firmware fwctl apu-rpu-ctl"
```
