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

`msap1-image` includes the `msap1_ad7771_iio` kernel module and
`msap1-apu-app`. The package enables `msap1-fpga-acquisition.service`, which
owns IIO/DMAengine and publishes the full-rate stream through shared memory.
R5 core 0 still owns AD7771 SPI, reset/synchronization, capture control, and
health; RPMsg carries control and health only.

`conf/machineyaml/msap1-sdt.yaml` inherits the KR260 machine template and sets
`CONFIG_SUBSYSTEM_PL_INPUT_DTSI` to
`conf/dtsi/msap1-ad7771-iio.dtsi`. `gen-machineconf` therefore merges the IIO
consumer, SG clock metadata, and Linux ownership overrides into the generated
`pl.dtso`; the DFX firmware recipe consumes that generated overlay unchanged.
DMA descriptors and sample buffers use Linux DMA/CMA allocation, with no fixed
ADC reserved-memory carveout.

The default template builds the APU application from the adjacent `MSAP1_APU`
checkout. Initialize that repository's submodules before using
`MSAP1_APU_APP_SRC = "local_inst"`; committed `local` and `cloud` modes fetch
them automatically.

The generator does not create component repositories, XSA files, RPU firmware,
or generated machine configuration.
