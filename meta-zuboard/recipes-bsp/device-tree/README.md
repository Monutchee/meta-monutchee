

Use the `zub1cg.dtsi` with Xilinx SDT workflow.


Execute the following command in XSCT
```
sdtgen set_dt_param -xsa <.xsa path> -dir <output_Path> -include_dts zub1cg.dtsi
sdtgen generate_sdt
```