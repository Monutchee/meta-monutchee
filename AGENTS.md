# Repository guidance

Read README.md before structural changes. This repository contains shared distro,
artifact, vendor integration and public demo layers. Keep private product names,
configuration, validation logs and documentation in their separate product repos.

Generic Station artifacts belong in meta-mnc-artifact; Xilinx boot/XSDB integration
belongs in meta-xilinx-addon; ZynqMP and Kria behavior belongs in their shared
add-on layers. Product layers own application selection and image policy.
Workspace creation belongs in monutchee-manifest and each project manifest.
setupSDK must support external product layers without naming private products.

Preserve local changes and generated outputs. Never edit generated machine
configuration as a source fix. Keep numeric service identities stable.
Run affected Python unit tests for tooling changes. Recipe changes require an
affected recipe build; image, firmware and boot policy changes also require the
relevant image build and hardware validation before release.

Follow LICENSING.md. Preserve upstream notices and recipe package licenses.
Update this file when durable ownership or workflow rules change.

The reusable system SDK and daemon belong in
`meta-mncos/recipes-support/mnc-system/files/source`: portable `mnc::system`,
native `mnc::os::system`, `mnc::logging` and `mnc::Service`. Product layers supply
immutable profiles, service identities, hardware allowlists and reset paths.
Keep arbitrary command/path/unit operations out of the privileged IPC API.

Vendor SYSMON code belongs in `meta-xilinx-addon/recipes-support/mnc-xilinx-sysmon`
under `mnc::xilinx::sysmon`, independent of the portable OS manager. Product
layers own provider selection, immutable hardware profiles and final package
composition. Discover AMS channels by driver/OF identity, never IIO indices;
preserve duplicate labels, units and unavailable values.

The generic base utility allowlist belongs in packagegroup-mncos-base-utils.
Additional command requirements belong in consumer RDEPENDS, with vendor
consumers maintained in vendor layers. Keep package recommendations and
automatic shared-library dependency resolution enabled.

- The system manager's optional `--boot-preferences` supplies typed factory and
  initial preferences plus a managed hostname. Keep vendor selection in product
  code; boot preferences cannot replace immutable hardware allowlists, unit
  policy or reset paths. Early hostname application must not depend on D-Bus.

- `meta-mncos/recipes-core/mnc-preboot` owns `/sbin/mnc-preboot`, the shared
  WIC/JTAG setup entry point before the main init system, and the minimal
  RAM-root init template. Required setup stages run here; storage preparation
  uses `mnc-preboot storage prepare`. Interactive invocations dispatch feature
  subcommands without running boot setup; only PID 1 hands off to systemd.
- `meta-mncos/recipes-core/mnc-storage` supplies policy-driven early storage
  preparation and explicit backup/repartition/restore tooling behind the
  `mnc-preboot storage` interface. Keep `mnc-storage` as a compatibility command.
  Product layers own disk identities, role labels, sizes and reset journals. Discovery never
  formats media; pending durable reset intent may format only the data role.
- `meta-mncos/scripts/make-ram-root.py` wraps an immutable SquashFS root in a
  minimal BusyBox initramfs. Product image recipes select it and provide the
  kernel support; hardware/Station payload policy remains in its owning layer.
