# meta-monutchee repository guidance

## Purpose and routing

- This repository contains shared Monutchee distribution/Xilinx layers and
  product layers. Read `README.md` before changing layer structure.
- MSAP1-specific image, firmware, recipe, and template policy belongs under
  `meta-msap1/`; read `meta-msap1/README.md` before changing that product.
- Keep reusable ZynqMP, Kria, and distribution behavior in the appropriate
  shared layer. Do not move an MSAP1-only workaround into a shared layer.
- Workspace creation and repository synchronization belong to
  `monutchee-manifest`; generated MSAP1 machine configuration belongs in the
  build directory, not this layer.

## MSAP1 integration contract

- `msap1-image` installs the meter DMA kernel module, `msap1-apu-app`, its
  acquisition daemon service, the MSAP1 DFX firmware, and supporting packages.
- MSAP1 uses persistent journald as its only log store, with product retention
  policy under `meta-msap1`. Preserve structured component metadata on the APU
  and firmware-loader services; do not create a second log database.
- The hardware workflow consumes a bitstream-inclusive `MSAP1_PL.xsa` and both
  R5 firmware applications. Keep PL, RPU, APU, and layer revisions traceable in
  target-test records.
- Linux owns the meter AXI DMA through the product-specific DMAengine misc
  driver. The `gen-machineconf` YAML merges `msap1-meter-dma.dtsi` and
  `msap1-fabric-clock.dtsi` into `pl.dtso`; keep the consumer and nominal
  100 MHz PL0 assignment atomic with the matching FPGA overlay and do not
  append DTS text in the firmware recipe.
- AD7771 SPI, capture, conversion, and processing register nodes remain
  unavailable to Linux because R5 core 0 owns them. DMA buffers come from
  Linux DMA/CMA; do not add fixed meter reserved memory.
- Install frequency settings inside each complete schema-v2 ADC profile. The
  packaged 5 A file is the fallback; preserve the Web-generated complete
  `/etc/monutchee/msap1/adc_config/active.json` across image updates.
- Use neutral MSAP1 sensor-board identifiers in packaged profile IDs,
  filenames, services, recipes, tests, and documentation. Do not introduce
  third-party vendor or product branding into this repository.
- `MSAP1_APU_APP_SRC` supports `cloud`, `local`, and `local_inst`:
  `local_inst` builds the adjacent APU working tree directly, including
  uncommitted edits; `local` fetches its committed Git state; `cloud` fetches
  the selected remote branch.
- The APU application carries `libs/openamp-helper` as a Git submodule. Keep
  the `cloud` and committed-`local` source modes on BitBake's `gitsm://`
  fetcher; initialize the submodule in the working tree used by `local_inst`.
- Keep `EXTERNALSRC_BUILD` outside the APU source tree so Yocto testing does not
  contaminate that repository with generated CMake files.
- Do not hard-code temporary feature branches into recipes. Branch and local
  source selection belong in build configuration/templates.

## Build and verification

Initialize from the `yocto-build` workspace root:

```sh
source ./setupSDK --product msap1
bitbake-layers show-layers
bitbake msap1-apu-app
bitbake msap1-image
```

- For fast APU iteration, select `MSAP1_APU_APP_SRC = "local_inst"`, confirm
  `MSAP1_APU_APP_LOCAL_DIR`, then rebuild `msap1-apu-app` without requiring an
  APU commit.
- For source-mode changes, inspect `bitbake -e msap1-apu-app` and verify
  `SRC_URI`, `S`, `EXTERNALSRC`, and `EXTERNALSRC_BUILD` resolve as intended.
- Recipe changes must pass at least the affected recipe build. Image, firmware,
  package-list, machine, or boot-policy changes require `msap1-image`.
- Changes to the machine YAML or PL input DTSI must also run `make_mconf.sh` and
  inspect the generated `build/conf/dts/msap1/pl-overlay-full/pl.dtso`.
- Do not edit files under `build/tmp`, `build/sstate-cache`, downloads, or other
  generated build output to fix a recipe.

## Maintaining this file

- Update this `AGENTS.md` in the same change when durable layer ownership,
  source modes, build, or verification conventions change.
- Keep branch-specific and transient build failures in issue/test documentation
  rather than here.
