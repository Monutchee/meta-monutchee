# meta-zynqmp-addon

Shared ZynqMP/OpenAMP integration for Monutchee products. Product firmware,
images and application policy remain in their product layers.

## R5 clock protection

ZynqMP designs that run R5 firmware from RPLL can lose that PLL during Linux's
unused-clock cleanup when Linux has no consumer holding it enabled. The shared
TF-A patch marks only the `CLK_RPLL_INT` PLL node with
`CLK_IS_CRITICAL | CLK_SET_RATE_NO_REPARENT`. The existing ZynqMP Linux clock
driver reads the firmware topology and retains an enable reference for RPLL.

The patch applies through the `zynqmp` machine override to the reviewed
`trusted-firmware-a_2.14.0-xilinx-v2026.1` recipe. All ZynqMP products using this
layer and firmware recipe inherit it, including Kria and ZUBoard products.
Non-ZynqMP targets such as Versal do not receive the patch. This policy applies
to both headless and graphics-enabled images.

Linux retains its RPLL clock reference even when both R5 cores are stopped.
PMU can still suspend the PLL when all firmware consumers release it.
Validate suspend/resume on each hardware configuration. The patch preserves the
FSBL-configured clock rate, parent and dividers; it does not set a universal
R5 frequency, lock the rate, or arbitrate direct PM firmware clock requests. Designs selecting another R5 parent need protection for that actual
clock path. Dynamic remoteproc clock ownership is not implemented.

## Removing the boot workaround

The shared boot script still adds `clk_ignore_unused`. Products receive the RPLL patch but retain the global workaround until their other firmware-owned and PL clock
consumers have been checked. RPLL protection alone does not prove those other
clocks can safely be gated.

For a validated product, remove the argument using its own machine override in
its `u-boot-xlnx-scr.bbappend`. Ship the patched BL31 and
regenerated boot script together; a root-filesystem update alone cannot replace
TF-A. Check board-specific boot configuration for other copies of the argument.

When changing TF-A versions or providers, port and validate the append and
patch together before booting without `clk_ignore_unused`. Older firmware
recipes do not receive this version-specific patch.

## Validation

For each product, check the resolved TF-A `SRC_URI` and generated boot script,
then verify RPLL remains locked and the R5 clock source/dividers match its XSA
through Linux unused-clock cleanup and individual/combined R5 restarts. Check
other firmware-owned and PL clocks that the global workaround previously kept
enabled. Retain a working boot artifact for rollback. Validate low-power modes
separately, including firmware PLL suspend/resume when both R5 cores are
stopped. Actual system power savings require measurement.

The topology patch does not change authentication or firmware permissions.
Future secure-boot releases must authenticate the rebuilt BL31 using the
normal protected signing process; this does not authenticate R5 payloads
loaded later by Linux. Keep signing keys out of this layer.

## Multimedia shutdown

`MNCOS_MULTIMEDIA_OFF = "1"` enables a coordinated FSBL, U-Boot and Linux policy.
It requires `MNCOS_HEADLESS = "1"`. Products opt in explicitly; otherwise they retain
headless capture/codecs support. Disable this setting explicitly
before enabling graphics with `MNCOS_HEADLESS = "0"`.

The FSBL-linked xilpm configuration assigns GPU and both pixel nodes only to
APU. PMU otherwise gives every permissible master initial power requirements,
even before that master requests the GPU; R5 defaults prevent early shutdown.
The version-scoped xilpm recipe narrows only these three slave permissions and
rejects conflicting preallocations. Master, reset, PLL and non-GPU ownership
are preserved; PMU firmware itself is unchanged.

U-Boot is built without video or DPDMA probing. Its board initialization uses
existing EEMI APIs to claim the GPU and both legacy pixel nodes with no power
requirement, verify their firmware state machines are off, gate the three GPU
clocks, then release ownership. Updating all three states prevents later
firmware cleanup from repeating pixel shutdown and restoring the main clock.
Display shutdown claims DP access, stops streams/audio, drains DPDMA, powers down the
DP PHY transmitters, gates the display clocks, and releases the DP requirement.
PMU reference counting decides whether VPLL can be suspended. This code does
not force shared PLLs off, bypass firmware permissions, or touch the shared
PS-GTR provider's other lanes. Firmware errors and hardware readback mismatches
produce an explicit incomplete-shutdown boot message; pending DMA retains its
access and clocks rather than being cut off.

The Linux device-tree policy disables GPU, DPSUB and DPDMA nodes in the base
tree and every final DTB after carrier overlays have been merged. The strict
kernel fragment also removes media/camera, codec, sound, DRM, framebuffer, and
video framebuffer DMA drivers while retaining metering AXI DMA/CMA/OpenAMP.
The ordinary headless fragment continues to preserve capture for other users.
Build-time checks reject incompatible settings and reenabled drivers/packages.
Release `build.json` records `multimedia_off`; deployment includes the effective
kernel and U-Boot configurations.

Ship FSBL, U-Boot and the Linux DT/kernel in one release. Future authenticated boot
images must sign the rebuilt FSBL and U-Boot through the normal release process. This
policy adds no debugfs power-write service or new security permissions.

The hardware hook targets the PS GPU and DisplayPort subsystem. It does not
remove video engines already instantiated in a PL bitstream or power down
external camera/display chips. Designs with PL video, VCU or external bridges
need their own hardware shutdown and rail policy before claiming a complete
multimedia shutdown.

Verify real GPU gates, firmware requirements, display PHY state, and VPLL on
each supported hardware configuration, with both R5 cores at their configured
frequency and metering/network/storage operating. The geometry processor and
GPU cache share the FPD supply with the A53s: clock gating cannot remove that
leakage while Linux runs. See AMD [UG1085, GPU power domains](https://docs.amd.com/v/u/en-US/ug1085-zynq-ultrascale-trm),
Chapter 5. The KR260 INA260 reports VCC_SOM power, not total
carrier-board power.
