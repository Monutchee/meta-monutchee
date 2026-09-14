# meta-monutchee

Monutchee distribution, Xilinx integration, board, and product layers for
MNCOS.

MNCOS uses OE-Core directly and defaults to headless Linux. See
[Standalone MNCOS](docs/standalone-mncos.md) for the `MNCOS_HEADLESS` switch,
capture compatibility, multimedia policy, migration, release
reports and validation requirements.

Shared layer ownership is split by responsibility:

- `meta-mnc-artifact` defines the vendor-neutral Provisioning Station archive
  and manifest contract.
- `meta-xilinx-addon` maps Xilinx/XSDB boot inputs into that contract and keeps
  legacy JTAG export support during migration.
- `meta-zynqmp-addon` owns shared ZynqMP/OpenAMP integration, including
  [RPLL protection for R5 firmware](meta-zynqmp-addon/README.md#r5-clock-protection).
- External product layers select the image and product policy.

## Creating a Xilinx product layer

Use the maintained KR260 product-layer scaffold instead of copying an existing
product layer by hand:

```bash
python3 scripts/create-xilinx-product-layer.py \
    --product example \
    --project-prefix EXAMPLE \
    --board kr260
```

See [Creating a Xilinx product layer](docs/create-xilinx-product-layer.md) for
the complete command reference and follow-up workflow.

Workspace creation is owned by
[`monutchee-manifest`](https://github.com/Monutchee/monutchee-manifest).
This repository provides `yocto-script/setupSDK`, which the manifest links into
each synchronized Yocto workspace.

## Licensing

Shared Monutchee code is offered under **GPL-3.0-only** or a separately agreed
commercial license. See [LICENSE](LICENSE), [LICENSING.md](LICENSING.md) and
[COMMERCIAL.md](COMMERCIAL.md). Business internal use is allowed under GPLv3.
Upstream components and explicitly licensed files retain their own terms.

Private product layers are separate repositories synchronized by their project
manifests into `sources/meta-<product>`. Shared SDK setup discovers their templates.

The `meta-xilinx-addon/recipes-support/mnc-xilinx-sysmon` recipe provides the
independent `mnc::xilinx::sysmon` IIO hardware monitor. Its
[SDK documentation](meta-xilinx-addon/recipes-support/mnc-xilinx-sysmon/files/source/README.md)
describes channel discovery, conversion, identity and native testing. Product
layers select this provider through their own hardware profiles and packages.

The `meta-mncos/recipes-support/mnc-system` recipe provides the reusable MNC
system-management SDK and daemon. See its `files/source/README.md` for namespace,
build, IPC and product-policy integration contracts.
