# meta-kr260demo yocto layer

## Introduction
KR260Demo product layer.

Shared Xilinx mechanics live in:

- `meta-xilinx-addon` for dfx-mgr firmware packaging, U-Boot JTAG/TFTP
  environment support, JTAG export, and APU/RPU utilities.
- `meta-zynqmp-addon` for ZynqMP OpenAMP, kernel, lopper, and domain YAML
  support.
- `meta-kria-addon` for Kria-only image dependencies and fancontrol overrides.

This layer keeps the KR260 product pieces: image recipes, product flasher,
device-tree notes, and the thin `kr260demo-dfx-firmware` recipe that supplies
KR260 PL/RPU inputs to `xilinx-dfx-firmware.bbclass`.

## Build Guide

1. Generate the system device tree using SDT flow

    ```bash
    rm -rf ../../runtime-generated/vivado_SDT_out/ && \
    echo 'set_dt_param \
      -xsa ../../runtime-generated/bin_file/KR260Demo_PL.xsa\
      -board_dts zynqmp-smk-k26-reva \
      -dir ../../runtime-generated/vivado_SDT_out/ ; \
      generate_sdt ; exit' | sdtgen
    ```

1. Generate yocto machine config using gen-machineconf

    When there are multiple OpenAMP domain files under `yocto-build/sources/meta-monutchee/meta-zynqmp-addon/recipes-bsp/domainyaml/openamp-overlay-zynqmp-{VERSION}.yaml`

    Please use the openamp-verlay-zynqmp-**.yaml version that make the meta-xilinx release version.

    ```bash
    #Assume your are in yocto-build/build

    gen-machineconf parse-sdt \
      -c conf -D \
      --template ../sources/meta-kria/conf/machineyaml/k26-smk-kr-sdt.yaml \
      --hw-description ../../runtime-generated/vivado_SDT_out/    \
      --machine-name kr260demo \
      --add-config CONFIG_YOCTO_BBMC_CORTEXR5_0_FREERTOS=y \
      --add-config CONFIG_YOCTO_BBMC_CORTEXR5_1_FREERTOS=y \
      --domain-file ../sources/meta-monutchee/meta-zynqmp-addon/recipes-bsp/domainyaml/openamp-overlay-zynqmp-v2026_1.yaml
    ```