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
[board-ipv4] [xsdb-target-id]`, resolves every artifact path relative to its
own location, and expects the Station to serve the archive's `tftp/` directory
directly. When a target ID is supplied, every PSU, PMU, and A53 operation is
scoped to that target's JTAG cable and device index so multiple attached boards
can be queued safely by the Station.

### JTAG cable reset selection

Both loaders default to the cable SRST pulse. For hardware whose cable advertises
and physically wires POR, the Station artifact recipe can explicitly set:

```bitbake
MNC_XILINX_JTAG_BOARD_RESET = "por"
```

The legacy TFTP export uses `JTAG_LOADER_BOARD_RESET = "por"`. Both build settings
accept only `srst` or `por` and are validated before shell rendering. A loader
process may override the packaged choice with `MNCOS_JTAG_BOARD_RESET=por` (or
`srst`); invalid values, including an empty override, fail before connection.

POR invokes XSDB `rst -por` on the selected PSU target. Unsupported POR aborts
without falling back to SRST or downloading payloads. Station cable/device
binding and post-reset reconnect remain in effect. The independent
`MNC_XILINX_JTAG_FORCE_JTAG_BOOT` / `JTAG_LOADER_FORCE_JTAG_BOOT` settings still
control the subsequent boot-mode register writes and `rst -system`; platforms
requiring this override must retain it after POR.

Cable capability alone does not prove board reset wiring or an external
peripheral's reset behavior. Qualify the relevant image and hardware before
enabling POR in board policy. POR is not a power removal command.

### Post-POR settle before JTAG takeover

Both loaders wait five seconds after a successful cable POR pulse, before
reconnecting, writing the optional JTAG boot-mode override or issuing the
subsequent `rst -system`. The SRST path retains its two-second settle. The wait
also applies when the boot-mode override is disabled and precedes all firmware
downloads. Preserve this ordering when adapting the download sequence.

Five seconds is the retained policy from successful ZynqMP board qualification;
it is not a proven minimum or a universal readiness guarantee. Autonomous boot
from the board's configured boot medium can run during this interval. The wait
does not establish a failure's root cause or prove physical power removal or
reset of an external TPM. Qualify each hardware/image combination before
release. Normal target binding, A53 core reset, PMUFW/FSBL downloads, FSBL
completion gate and payload flow remain in effect. Unsupported POR aborts;
there is no fallback to SRST.

### FSBL completion

Both loaders set an owned `XFsbl_Exit` breakpoint and use XSDB
`con -block -timeout 60`, following AMD's ZynqMP download sequence. They resolve
the exit symbol from the downloaded FSBL and verify the halted PC matches it
and require `FSBL_STATUS` (`0xffd80060`) to report `XFSBL_COMPLETED` (zero)
on the same physically bound PSU before downloading TF-A. Error lockdown also
reaches the exit, so a nonzero or unreadable status aborts. Missing symbols,
timeout, an unrelated halt or failed
breakpoint cleanup abort the loader. Only the breakpoint created by this wait
is removed; a cleanup failure does not replace an earlier execution failure.
There is no fixed-delay completion assumption or reset/retry fallback.

`FSBL_STATUS == 0` alone cannot prove completion: zero is also its initial
value. The exit breakpoint and successful status jointly establish completed
JTAG initialization before TF-A is downloaded.
