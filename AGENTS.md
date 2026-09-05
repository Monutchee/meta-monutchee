# meta-monutchee repository guidance

## Purpose and routing

- This repository contains shared Monutchee distribution/Xilinx layers and
  product layers. Read `README.md` before changing layer structure.
- Keep the Provisioning Station archive format, deterministic packaging, and
  manifest schema in `meta-mnc-artifact`; it must remain vendor-neutral.
- Keep XSDB/JTAG payload selection and the Station loader in
  `meta-xilinx-addon`. Product identity and source-image selection belong in
  the product layer.
- MSAP1-specific image, firmware, recipe, and template policy belongs under
  `meta-msap1/`; read `meta-msap1/README.md` before changing that product.
- Keep reusable ZynqMP, Kria, and distribution behavior in the appropriate
  shared layer. Do not move an MSAP1-only workaround into a shared layer.
- Workspace creation and repository synchronization belong to
  `monutchee-manifest`; generated MSAP1 machine configuration belongs in the
  build directory, not this layer.

## MSAP1 integration contract

- `msap1-image` installs the meter and waveform DMA kernel modules,
  `msap1-apu-app`, its settings, acquisition, web, and product service-manager
  units, the MSAP1 DFX firmware, and supporting packages.
- `msap1-meter-stream` durably commits every validated PL meter record below
  `/data/mnc/database/meter-stream` before acquisition publishes typed latest
  values. `msap1-meter-historian` consumes an independently acknowledged
  cursor and stores typed projections below `/data/mnc/database/meter-historian`.
  Keep SQLite, both units, and their separately owned persistent directories
  together; spool failure must stop acquisition rather than silently lose an
  accepted record.
- Product daemons use the framed Boost.Asio Unix-stream transport and
  `mnc::Service` readiness/watchdog lifecycle. `msap1-service-manager` orders
  and audits units through systemd sd-bus; it must not replace systemd PID
  ownership or restart limits.
- `msap1-modbus-server` is an unprivileged protocol gateway that reads typed
  latest snapshots through acquisition IPC. Keep transport-neutral Modbus
  framing in `mnc::modbus`, the static product register contract in
  `msap1::modbus`, and grant only `CAP_NET_BIND_SERVICE` for TCP port 502.
  Serial access is provided through the standard `dialout` group; the gateway
  must never open DMA, RPMsg, or meter database files directly.
- `msap1-mqtt-publisher` is an optional unprivileged latest-snapshot publisher.
  Keep it disabled unless active product settings enable MQTT; broker failure
  degrades only MQTT and must never stop acquisition. Passwords and private-key
  passphrases stay in the settings secret store, while TLS assets are exposed
  to the publisher only as protected runtime files.
- `msap1-data-sender` is the always-installed M19 historian export service.
  Keep its append-only numeric identity stable, its primary data-group and
  supplementary settings-group access minimal, and its persistent/runtime
  directories owned through restrictive tmpfiles rules. The unit must require
  settings, historian, `/data`, and network-online ordering without making
  acquisition depend on outbound delivery health.
- Build the Data Sender transport adapter with libcurl and keep target curl's
  HTTP, HTTPS, FTP, and SFTP protocols enabled through libssh2. Retain CA
  certificates. Generated files remain behind authenticated, manifest-
  authorized backend streaming; never package an unauthenticated nginx alias
  or raw listing for `/data/mnc/data-sender`.
- `msap1-modbus-register-doc` runs the APU's authoritative target-built
  `modbus-map-dump` under QEMU and exports a single-sheet Excel register map to
  `export/docs`. Keep the workbook generator in the product layer and do not
  duplicate register definitions in Yocto metadata.
- MSAP1 uses persistent journald as its only log store, with product retention
  policy under `meta-msap1`. Preserve structured component metadata on the APU
  and firmware-loader services; do not create a second log database.
- Keep `msap1-web-backend.service` unprivileged and grant its authenticated
  Developer log API read-only system-journal access through the
  `systemd-journal` supplementary group.
- The temporary `debugai` SSH account is an explicit development-image opt-in.
  Keep it disabled by default, reject it from production-flash builds, and
  confine it to the metadata-enforced `mnc` diagnostic JSON gateway without
  PTY, forwarding, shell, SCP, or SFTP access.
- The hardware workflow consumes a bitstream-inclusive `MSAP1_PL.xsa` and both
  R5 firmware applications. Keep PL, RPU, APU, and layer revisions traceable in
  target-test records.
- Linux owns the meter and waveform AXI DMA channels through the single
  `msap1-dma` DMAengine misc module: one shared transport core plus one
  personality per compatible. The `gen-machineconf` YAML merges
  `msap1-meter-dma.dtsi` and `msap1-fabric-clock.dtsi` into `pl.dtso`; keep
  both consumers and the nominal 100 MHz PL0 assignment atomic with the
  matching FPGA overlay and do not append DTS text in the firmware recipe.
- Both consumers (`/dev/msap1-meter`, 512 x 256-byte records;
  `/dev/msap1-waveform`, 256 x 32,832-byte blocks) share the core's transport
  rules: the active DMA period is reserved, kernel-ring overruns are reported
  separately through the shared transport-status ioctl, and a period is never
  returned while DMA may be overwriting it. Long pre-trigger history and
  `.mncwf` storage under `/data/mnc/waveform` belongs to the APU daemon, not
  the kernel driver or a reserved DDR carveout. nginx may serve completed
  captures only through WebEngine-authenticated protected routes.
- AD7771 SPI, capture, conversion, and processing register nodes remain
  unavailable to Linux because R5 core 0 owns them. DMA buffers come from
  Linux DMA/CMA; do not add fixed meter reserved memory.
- Install the canonical factory settings directly from the selected APU source
  at `${S}/config/settings/factory-defaults.json`; never maintain a recipe-local
  duplicate. `msap1-settings` owns `/data/mnc/settings` and must start before
  acquisition and Web. Packaging must preserve active settings and secrets
  across image updates. Do not create draft or revision storage.
- Use neutral MSAP1 sensor-board identifiers in packaged profile IDs,
  filenames, services, recipes, tests, and documentation. Do not introduce
  third-party vendor or product branding into this repository.
- `MSAP1_APU_APP_SRC` supports `cloud`, `local`, and `local_inst`:
  `local_inst` builds the adjacent APU working tree directly, including
  uncommitted edits; `local` fetches its committed Git state; `cloud` fetches
  the selected remote branch.
- `msap1-web` treats `npm-shrinkwrap.json` as its complete dependency list.
  `local_inst` must select the adjacent Web checkout's lockfile directly and
  hash it into fetch/configure; committed `local` and `cloud` modes must use
  the matching layer-pinned copy so their offline dependency inputs remain
  reproducible.
- The APU application carries `libs/openamp-helper` as a Git submodule. Keep
  the `cloud` and committed-`local` source modes on BitBake's `gitsm://`
  fetcher; initialize the submodule in the working tree used by `local_inst`.
- Keep `EXTERNALSRC_BUILD` outside the APU source tree so Yocto testing does not
  contaminate that repository with generated CMake files.
- Do not hard-code temporary feature branches into recipes. Branch and local
  source selection belong in build configuration/templates.

## Build and verification

- `meta-mncos` owns standalone OE-Core distro policy. Do not reintroduce a
  dependency on Poky layers or the combined Poky checkout.
- `MNCOS_HEADLESS` defaults to `1`; `0` retains the previous graphics policy.
  Keep capture, DMA/CMA and required DRM helpers intact when disabling display
  and GPU drivers. Never apply Linux graphics policy to firmware multiconfigs.
- Preserve CVE/SPDX release reports and source provenance. Report generation
  failures are errors; CVE findings remain report-only until baseline triage.

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
- `msap1-jtag-image` is the Station-facing RAM-boot artifact target. Its
  `manifest.json` must cover every regular payload file, and the legacy flat
  TFTP export must remain functional until its consumers are migrated.
- Station artifacts remain unsigned in BitBake. Signing keys and
  `manifest.sig` belong to a protected release pipeline, never a layer recipe.

## Maintaining this file

- Update this `AGENTS.md` in the same change when durable layer ownership,
  source modes, build, or verification conventions change.
- Keep branch-specific and transient build failures in issue/test documentation
  rather than here.
