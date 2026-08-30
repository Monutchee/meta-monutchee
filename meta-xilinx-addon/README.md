# meta-xilinx-addon

Shared Xilinx utilities for MNCOS product layers.

This layer provides:

- `xilinx-jtag-artifact.bbclass` and `load-jtag-image-station.tcl`, which
  package a self-contained v2 Provisioning Station JTAG/TFTP artifact.
- `xilinx-dfx-firmware.bbclass`, a generic dfx-mgr packager for PL bitstreams,
  optional overlays, and Cortex-R firmware in the dfx-mgr RPU layout.
- `export-tftpboot-file.bbclass` and `load-jtag-image.tcl`, the JTAG/TFTP boot
  bundle exporter used by existing product images during migration.
- `apu-rpu-ctl`, the Linux RPMsg char-device client used by demo images.
- `fwctl`, kept as an optional raw fpga_manager/remoteproc utility.
- A shared `u-boot-xlnx` add-on that installs the MNCOS JTAG/TFTP default
  environment.
- `conf/xilinx-build.inc`, common Xilinx build defaults.

Product layers should keep their board-specific firmware recipe thin: inherit
`xilinx-dfx-firmware`, set the `XILINX_DFX_*` variables, and provide the `.bit`,
`.dtso`/`.dtbo` if needed, and RPU `.elf` files through `SRC_URI`.

For boards strapped to boot from QSPI or another non-JTAG source, set
`JTAG_LOADER_FORCE_JTAG_BOOT = "1"` in the image recipe that inherits
`export-tftpboot-file`.

New Station artifacts use a dedicated product recipe:

```bitbake
MNC_XILINX_JTAG_IMAGE_RECIPE = "my-product-image"

inherit xilinx-jtag-artifact

MNC_ARTIFACT_NAME = "my-product-jtag-image"
MNC_ARTIFACT_PRODUCT = "my-product"
MNC_XILINX_JTAG_FORCE_JTAG_BOOT = "1"
```

The Station loader accepts `<hw-server-url> <tftp-server-ipv4>
[board-ipv4]`, resolves every artifact path relative to its own location, and
expects the Station to serve the archive's `tftp/` directory directly.
