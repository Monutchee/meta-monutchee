# meta-xilinx-addon

Shared Xilinx utilities for MNCOS product layers.

This layer provides:

- `xilinx-dfx-firmware.bbclass`, a generic dfx-mgr packager for PL bitstreams,
  optional overlays, and Cortex-R firmware in the dfx-mgr RPU layout.
- `export-tftpboot-file.bbclass` and `load-jtag-image.tcl`, the JTAG/TFTP boot
  bundle exporter used by product images.
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
