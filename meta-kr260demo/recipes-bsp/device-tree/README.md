Please use the Xilinx SDT workflow.

## SDT gen


```bash
rm -rf ../../runtime-generated/vivado_SDT_out/ && \
echo 'set_dt_param \
   -xsa ../../runtime-generated/bin_file/KR260Demo_PL.xsa\
   -board_dts zynqmp-smk-k26-reva \
   -dir ../../runtime-generated/vivado_SDT_out/ ; \
   generate_sdt ; exit' | sdtgen
```

## gen-machine-conf

Note that the "k26-smk-kr-sdt.yaml" already have `CONFIG_YOCTO_BBMC_CORTEXR5_0_FREERTOS=y` enabled

```bash
#Assume your are in yocto-build/build

gen-machineconf \
  --template ../sources/meta-kria/conf/machineyaml/k26-smk-kr-sdt.yaml \
  parse-sdt \
  --hw-description ../../runtime-generated/vivado_SDT_out/ \
  -c conf -D \
  --machine-name kr260demo \
  --add-config CONFIG_YOCTO_BBMC_CORTEXR5_1_FREERTOS=y \
  --domain-file ../sources/meta-monutchee/meta-kr260demo/recipes-bsp/domainyaml/openamp-overlay-zynqmp.yaml
```


Use this [link](https://github.com/Xilinx/gen-machine-conf/blob/xilinx_v2025.2/gen-machine-scripts/configs/Kconfig.yoctomachinesettings)
to find your k26/k24 som or starter kit board name