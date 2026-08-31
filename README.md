# meta-monutchee

Monutchee distribution, Xilinx integration, board, and product layers for
MNCOS.

Shared layer ownership is split by responsibility:

- `meta-mnc-artifact` defines the vendor-neutral Provisioning Station archive
  and manifest contract.
- `meta-xilinx-addon` maps Xilinx/XSDB boot inputs into that contract and keeps
  legacy JTAG export support during migration.
- Product layers such as `meta-msap1` select the image and product policy.

## Creating a Xilinx product layer

Use the maintained KR260 product-layer scaffold instead of copying an existing
product layer by hand:

```bash
python3 scripts/create-xilinx-product-layer.py \
    --product msap1 \
    --project-prefix MSAP1 \
    --board kr260
```

See [Creating a Xilinx product layer](docs/create-xilinx-product-layer.md) for
the complete command reference and follow-up workflow.

Workspace creation is owned by
[`monutchee-manifest`](https://github.com/Monutchee/monutchee-manifest).
This repository provides `yocto-script/setupSDK`, which the manifest links into
each synchronized Yocto workspace.
