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

When have multiple openamp domain file under `yocto-build/sources/meta-monutchee/meta-kr260demo/recipes-bsp/domainyaml/openamp-overlay-zynqmp-{VERSION}.yaml`

Please use the openamp-verlay-zynqmp-**.yaml version that make the meta-xilinx release version.

```bash
#Assume your are in yocto-build/build

gen-machineconf \
  --template ../sources/meta-kria/conf/machineyaml/k26-smk-kr-sdt.yaml \
  parse-sdt \
  --hw-description ../../runtime-generated/vivado_SDT_out/ \
  -c conf -D \
  --machine-name kr260demo \
  --add-config CONFIG_YOCTO_BBMC_CORTEXR5_0_FREERTOS=y \
  --add-config CONFIG_YOCTO_BBMC_CORTEXR5_1_FREERTOS=y \
  --domain-file ../sources/meta-monutchee/meta-kr260demo/recipes-bsp/domainyaml/openamp-overlay-zynqmp-v2026_1.yaml
```

> Use the v2026.1 layered flow (`--domain-file` is repeatable; gen-machineconf
> deep-merges domains of the same name), NOT the legacy `openamp-overlay-zynqmp.yaml`:
>   - `kr260demo-multidomain.yaml` (base): cores + `cluster_cpu`, which makes lopper
>     auto-generate the Linux `xlnx,zynqmp-r5fss` remoteproc nodes (so
>     `/sys/class/remoteproc/{0,1}` appear and dfx-mgr can load the R5s).
>   - `kr260demo-openamp-overlay.yaml` (channel overlay): reserved-memory carveouts
>     + rpmsg/remoteproc relations + mailboxes for both cores. Drop this one if you
>     only need bare R5 loading and no Linux<->R5 rpmsg.
>
> Neither file has a host `access:` list. That legacy field is what leaked an
> `&openamp_a53_0_cluster` node into `pl.dtso` referencing non-live labels, which
> broke `dfx-mgr-client -loadByName kr260demo`.
>
> After regenerating, sanity-check the result before a full image build:
> `grep -c zynqmp-r5fss build/conf/dts/kr260demo/cortexa53-linux.dts` (expect 1)
> and confirm `build/conf/dts/kr260demo/pl-overlay-full/pl.dtso` has no
> `openamp`/`cpus_r5` references.


Use this [link](https://github.com/Xilinx/gen-machine-conf/blob/xilinx_v2025.2/gen-machine-scripts/configs/Kconfig.yoctomachinesettings)
to find your k26/k24 som or starter kit board name