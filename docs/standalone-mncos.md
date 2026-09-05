# Standalone MNCOS on Scarthgap

`meta-mncos` owns the distribution policy and uses OE-Core directly. Builds
use the matching Yocto 5.0.18 releases of OE-Core and BitBake, AMD/Xilinx
2026.1, and the community-layer revisions in `monutchee-manifest`. Neither
the Poky distribution layers nor the combined Poky checkout is required.

## Policy and ownership

The distro retains systemd, RPM packaging, MNCOS target/SDK identity and
version 0.0.1, SDK installation paths, ptest, multiarch, uninative, hash
equivalence, compiler security flags, no-static-library policy, strict source
checksums, and SPDX generation. Hardware integration remains in the shared
Xilinx/ZynqMP/Kria add-on layers; applications and image selection remain in
product layers. Generated machine configuration is not edited by hand.

The sole graphics policy interface is:

```bitbake
MNCOS_HEADLESS ?= "1"
```

The default removes Linux target X11, Wayland, OpenGL, Vulkan, DirectFB,
fbdev and libmali features, graphical image features, and the effective
`mali400` machine feature. Kernel display controllers, GPUs, display DMA and
framebuffer consoles are disabled. A post-configuration check rejects a BSP
fragment or Kconfig dependency that restores a disabled symbol. Rootfs
validation rejects the graphics provider packages even if explicitly added.

V4L2 capture/processing, VCU policy, Xilinx framebuffer **DMA**, DMA/CMA,
OpenAMP, acquisition, and remote web access remain available. DRM core and
protocol helpers remain where capture needs them, including the Xilinx
DisplayPort receiver. `CONFIG_DRM=y` alone is not a headless failure.
A kernel Kconfig patch makes that receiver select the shared DRM KMS helpers
it needs for linking when display controllers are disabled. Those helpers
do not enable a display controller or a framebuffer console.
MSAP1 and KR260 continue excluding `hwcodecs` from their main images.
Vendor standalone/FreeRTOS multiconfigs and native/SDK host tools are outside
this Linux target filtering.

Set `MNCOS_HEADLESS = "0"` in product configuration or `local.conf` to stop
the filtering and omit the headless kernel fragment. This restores the
previous graphics policy; it does not install a desktop or add support for
new display hardware. Any other value is a configuration error. Regenerate
the kernel configuration/build when switching modes.

## Reports

`INHERIT += "cve-check"` enables Scarthgap's recipe and image reporting.
CVE findings are report-only. A missing database is fatal because silently
skipping checks would produce incomplete reports. No severity gate or new
blanket CVE exclusions are introduced.

A normal image build collects reports into
`tmp/deploy/images/<machine>/<image>-<machine>.rootfs.mncos-reports/`:

- `image.cve.json`, `image.spdx.tar.zst`, and `image.manifest`;
- `kernel.config` containing the actual deployed kernel configuration;
- `build.json` with product/distro/headless identity and recipe CVE coverage;
- available CVE reports for the kernel, U-Boot, and TF-A.

Missing/empty required image reports fail collection. Missing supplemental
recipe reports are identified as coverage gaps. Firmware built by standalone
or FreeRTOS multiconfigs requires separate review. CVE product/version
mappings and vendor patch status need triage; a report is not proof that all
vulnerabilities are known or fixed.

`mnc yocto build` includes these reports plus resolved manifest/source
revisions and dirty-tree indicators in the outer delivery's `metadata/`.
Station archive schemas and signing policy are unchanged. Standard SDK export
also copies available package manifests and SPDX archives beside the
installer; eSDK outputs remain in the OE-Core SDK deployment directory.
Do not publish full `bitbake -e` output: local configuration can contain secrets.

## Migration and validation

Update both `monutchee-manifest` and `meta-monutchee` together. Follow the
manifest repository's standalone MNCOS migration instructions to synchronize
sources and create a fresh build directory. `setupSDK` rejects old active
Poky layer paths without rewriting user configuration. Downloads and sstate
caches may be reused. Save the previous build directory and resolved source
revisions for rollback.

Local checks:

```sh
python3 -m unittest discover -s scripts/tests -v
```

For MSAP1, KR260 demo and ZU demo, check fresh setup, layer resolution, both
headless values and invalid input, main/production-flash builds, SDK/eSDK
generation, and image/report contents. Check the Station artifact for MSAP1.
Compare effective settings, package manifests and final kernel configuration
against the previous build, allowing only graphics, reporting and source-path
changes. Retest serial boot, storage/network, PL overlays, RPU/OpenAMP,
acquisition/capture and web services on each applicable board. Full image and
hardware validation remain release requirements even when metadata tests pass.

## Production maintenance

Keep credential provisioning, secure boot, signed updates, service exposure
and update/recovery policy as explicit production work. Triage the CVE baseline
before introducing High/Critical gates; any waiver must identify the CVE,
recipe/version, reason, owner and expiry. Maintain the selected vendor kernel
and firmware as well as OE-Core. Evaluate later Scarthgap releases through
coordinated builds, hardware checks and CVE comparisons.
