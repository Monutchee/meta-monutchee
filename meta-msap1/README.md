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

The generator does not create component repositories, XSA files, RPU firmware,
or generated machine configuration.
