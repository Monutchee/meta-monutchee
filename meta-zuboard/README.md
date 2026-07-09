# meta-zuboard yocto layer

# Introduction

ZUBoard product layer.

Shared Xilinx mechanics live in:

- `meta-xilinx-addon` for dfx-mgr firmware packaging, U-Boot JTAG/TFTP
  environment support, JTAG export, and APU/RPU utilities.
- `meta-zynqmp-addon` for ZynqMP OpenAMP, kernel, lopper, and domain YAML
  support.

This layer keeps the ZUBoard product pieces: image recipes, product flasher,
device-tree notes, and the thin `zudemo-dfx-firmware` recipe that supplies
ZUBoard PL/RPU inputs to `xilinx-dfx-firmware.bbclass`.

## Build Guide


1. Generate the system device tree using SDT flow

    ```bash
    rm -rf ../../runtime-generated/vivado_SDT_out/ && \
    echo 'set_dt_param \
      -xsa ../../runtime-generated/bin_file/ZuBoardDemo_PL.xsa\
      -include_dts ../sources/meta-monutchee/meta-zuboard/recipes-bsp/device-tree/files/zub1cg.dtsi \
      -dir ../../runtime-generated/vivado_SDT_out/ ; \
      generate_sdt ; exit' | sdtgen
    ```

1. Generate yocto machine config using gen-machineconf

    When there are multiple OpenAMP domain files under `yocto-build/sources/meta-monutchee/meta-zynqmp-addon/recipes-bsp/domainyaml/openamp-overlay-zynqmp-{VERSION}.yaml`

    Please use the openamp-verlay-zynqmp-**.yaml version that make the meta-xilinx release version.

    ```bash
    #Assume your are in yocto-build/build

    gen-machineconf parse-sdt \
      --hw-description ../../runtime-generated/vivado_SDT_out/ \
      -c conf -D \
      -g full --machine-name "zudemo" \
      --add-config CONFIG_YOCTO_BBMC_CORTEXR5_0_FREERTOS=y \
      --add-config CONFIG_YOCTO_BBMC_CORTEXR5_1_FREERTOS=y \
      --domain-file ../sources/meta-monutchee/meta-zynqmp-addon/recipes-bsp/domainyaml/openamp-overlay-zynqmp-v2026_1.yaml
    ```
