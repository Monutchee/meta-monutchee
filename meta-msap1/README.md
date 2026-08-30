# meta-msap1 Yocto layer

## Introduction

MSAP1 product layer for the AMD Kria KR260 Robotics Starter Kit.

Shared Xilinx mechanics live in `meta-xilinx-addon`, `meta-zynqmp-addon`, and
`meta-kria-addon`. This layer contains only the product build templates, image
recipes, DFX firmware inputs, and production-flashing policy.

The `msap1` machine configuration is generated from the product XSA and
SDT output. It is installed in the build directory by the Monutchee
`make_mconf.sh`/`make_yocto.sh` workflow; it is not stored in this layer.

## Workspace setup

The workspace is created by `monutchee-manifest`:

```bash
curl -fsSL \
  "https://raw.githubusercontent.com/Monutchee/monutchee-manifest/main/msap1/setupWorkspace" \
  | bash -s -- yocto scripts
```

After the component repositories exist, use `all` instead of `yocto scripts`.

## Hardware-to-image workflow

The KR260 profile expects a bitstream-inclusive `MSAP1_PL.xsa`, two
R5 OpenAMP applications, and the standard KR260 board description
`zynqmp-smk-k26-reva`.

```bash
./make_PL.sh --xsa /path/to/MSAP1_PL.xsa
./make_mconf.sh
./make_RPU.sh
./make_yocto.sh
```

The default image target is `msap1-image`. The optional production
flashing target is `msap1-production-flash-image`.

Build the self-contained Provisioning Station RAM-boot artifact with:

```bash
MACHINE=msap1 bitbake msap1-jtag-image
```

The deploy directory receives a versioned archive plus the stable
`msap1-jtag-image.tar.gz` link. The archive contains a v2 Station manifest,
the Xilinx JTAG firmware and loader under `jtag/`, and the normal MSAP1 kernel,
device tree, boot script, and initramfs under `tftp/`. The older flat
`build/export/tftpboot` output remains available until the Station replaces
the existing `mnc deploy` workflow.

The serial login banner and post-login MOTD identify the built image:

```text
*** MNCOS MSAP1 MAIN SYSTEM IMAGE ***
Image role: main
Image recipe: msap1-image
Machine: msap1
Build time: 2026-07-29 14:25:03 UTC
Build hash: 4eb8ae
```

The six-character value is the display form of the BitBake `do_rootfs` task
hash. `/etc/mncos-image-info` stores the full hash and the same human-readable
UTC build time.

`msap1-image` includes the `msap1_dma` kernel module (one shared DMA
transport core serving both acquisition devices) plus `msap1-apu-app`. The package installs the `mnc` diagnostic CLI
with Bash completion and enables the settings, acquisition, web-backend, and
product service-manager units. `msap1-fpga-acquisition.service` owns both DMAengine
channels, publishes typed latest meter snapshots for IPC consumers, and
retains 128 MiB of raw waveform history. Latest snapshots are intentionally
lossy and do not provide historian durability. Completed triggered waveform files are written below persistent
storage at `/data/mnc/waveform`. R5 core 0 owns
AD7771 SPI, reset/synchronization, capture and meter configuration; RPMsg
carries configuration/control/health only and never carries waveform data.

Internal APU services communicate through explicitly framed Boost.Asio
Unix-domain stream sockets. `msap1-service-manager` starts or adopts
acquisition before the web backend, audits systemd unit health through sd-bus,
and exposes the `mnc service` control surface. systemd remains responsible for
process ownership, watchdog enforcement, restart limits, and final unit state.

At boot, the default DFX firmware load remains active after its successful
one-shot execution. R5 firmware loading completes next, and acquisition starts
only after both R5 cores and their RPMsg endpoints have been brought up. This
ordering prevents service retries from reloading an already active PL design.

The canonical factory settings are installed directly from the selected APU
source as `/usr/share/monutchee/msap1/settings/factory-defaults.json`.
`msap1-settings` validates and initializes `/data/mnc/settings`, then owns the
active settings document and separate secret persistence. The layer defines only users,
permissions, service ordering, and directory creation; it does not duplicate
product default values or migrate legacy `/etc` ADC profiles.

The image keeps systemd-journald as the single product log store. MSAP1
configures persistent storage with a 32 MiB maximum and seven-day retention.
The SD-card ext4 partition `/dev/sda1` is mounted at `/data` during
`local-fs.target` startup for meter-system persistent data. The
`msap1-data-mount` package installs and enables the path-matched `data.mount`
unit. Before mounting, systemd runs `fsck.ext4` so an unclean shutdown or JTAG
reset cannot silently expose a damaged partition to the settings, meter-record,
or waveform services. An unrepairable filesystem prevents `data.mount` and its
dependent services from starting instead of falling back to non-persistent root
storage. Files on the ext4 filesystem are non-executable and writable by the
existing `mnc-data` service group.

The APU services and PL/RPU firmware loaders add structured component metadata,
and `mnc log` presents their combined timestamp-ordered lifecycle. The
unprivileged web backend receives read-only journal access through its
`systemd-journal` supplementary group so the authenticated administrator
Developer page can query the same entries without running the backend as root.

## Temporary machine-diagnostics account

The restricted account used to exercise the future MCP/remote-support
interface is disabled by default. Enable it only in a development build:

```bitbake
MSAP1_ENABLE_DEBUGAI = "1"
```

This installs the temporary `debugai` / `debugai` credentials and an SSH
`Match User` policy with no PTY, forwarding, tunnel, user RC, interactive
shell, SCP, or SFTP access. Its forced APU gateway accepts only
machine-readable, metadata-classified diagnostic commands:

```sh
ssh debugai@METER 'mnc --output json machine describe'
ssh debugai@METER 'mnc --output json meter health'
```

Runtime-control, maintenance, continuous, socket-override, and timeout-override
commands are rejected before their handlers run. The production-flash image
recipe fails if `MSAP1_ENABLE_DEBUGAI` is enabled. The password account is only
for prototype validation; production support access must use provisioned
per-device certificates or keys and an audited enablement workflow.

`conf/machineyaml/msap1-sdt.yaml` inherits the KR260 machine template and sets
`CONFIG_SUBSYSTEM_PL_INPUT_DTSI` to the product's DMA and fabric-clock DTSI
files. `gen-machineconf` therefore merges both the meter and waveform DMA
consumers, SG clock metadata, Linux ownership overrides, and the nominal
100 MHz PL0 request into the generated `pl.dtso`; the DFX firmware recipe
consumes that generated overlay unchanged. The explicit 100 MHz request
prevents the ZynqMP Linux clock framework from rounding Vivado's exported
99,999,001 Hz value down to the 90.909 MHz divider. DMA descriptors and
transport buffers use Linux DMA/CMA allocation, with no fixed waveform or
meter reserved-memory carveout.

The default template builds the APU application from the adjacent `MSAP1_APU`
checkout. Initialize that repository's submodules before using
`MSAP1_APU_APP_SRC = "local_inst"`; committed `local` and `cloud` modes fetch
them automatically.

The generator does not create component repositories, XSA files, RPU firmware,
or generated machine configuration.

## Generated Modbus register documentation

The `msap1-modbus-register-doc` recipe runs the target-built
`modbus-map-dump --format json` executable under Yocto's QEMU wrapper and
renders the authoritative register map as a single-sheet Excel workbook:

```text
build/export/docs/msap1_modbus_registers.xlsx
```

The document is generated automatically by `msap1-image` and `populate_sdk`,
or independently with:

```sh
bitbake msap1-modbus-register-doc
```

The workbook contains the hexadecimal and decimal register range, data type,
function code, access type, and fully qualified meter attribute for every
exported register definition. The Python generator uses only the standard
library; it does not maintain a second Modbus table in the Yocto layer.

## Web interface source selection

`msap1-web` builds the React/Vite frontend into `/usr/share/msap1-web`; Node.js
and `node_modules` are build-time-only and are not installed on the target.
Select its source in `local.conf` using the same modes as the APU application:

```bitbake
MSAP1_WEB_SRC = "cloud"      # selected GitHub branch
MSAP1_WEB_SRC = "local"      # committed local checkout
MSAP1_WEB_SRC = "local_inst" # live checkout, including uncommitted edits
MSAP1_WEB_GIT_BRANCH = "main"
MSAP1_WEB_LOCAL_DIR = "${MSAP1_LAYERDIR}/../../../../applications/MSAP1_WEB"
```

The recipe fetches dependencies before the network-disabled compile task, with
`npm-shrinkwrap.json` serving as the complete library list. In `local_inst`
mode it reads the live `MSAP1_WEB` lockfile directly and includes that file in
both the fetch and configure signatures. Adding, updating, or removing a
dependency therefore refetches the required tarballs and regenerates the
offline npm package cache automatically on the next build.

The template derives the adjacent Web checkout from `MSAP1_LAYERDIR`, so this
automatic mode also works when the Yocto build directory is outside the
workspace. Override `MSAP1_WEB_LOCAL_DIR` only for a different checkout layout.

Committed `local` and `cloud` builds remain reproducible and use the pinned
lockfile under `recipes-httpd/msap1-web/files/`, because their Git source is not
available when BitBake expands fetch URLs. Update that pinned copy with the Web
repository lockfile when publishing a dependency change; configuration fails
if the fetched source and pinned lockfile differ.

In `local_inst` mode, `externalsrc` invalidates `do_compile` when the live
checkout changes. Immediately before Vite runs, the recipe overlays the current
frontend build configuration and complete `src/` and `public/` trees onto the
offline npm package prepared by `do_configure`. Editing, adding, or removing a
frontend source file therefore does not require a manual recipe clean, while a
lockfile edit automatically reruns the dependency-fetch/configure path.

The product web backend enables nginx on HTTP port 80 and HTTPS port 443. On
first boot, `msap1-web-tls-setup` creates a per-device self-signed development
certificate in `/var/lib/monutchee/tls`. Replace `msap1-web.crt` and
`msap1-web.key` there with a CA-issued certificate and restart
`msap1-web-backend` for production. The setup helper preserves an existing
certificate/key pair and fails rather than overwriting incomplete provisioned
TLS material.
