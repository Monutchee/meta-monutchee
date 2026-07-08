Please use the Xilinx SDT workflow.



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
