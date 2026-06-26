# Fix the lopper openamp assist so it generates a correct split-mode dual-R5
# xlnx,zynqmp-r5fss for the Linux host DT. The stock assist (SRCREV 9159040)
# botches the second R5 core in split mode (truncated `ranges`, atcm1/btcm1
# reg-names, dropped mboxes/carveouts on the 2nd rpmsg channel, and vdev1*
# carveout names the kernel never matches), which makes the kernel
# zynqmp_r5_remoteproc probe fail with "failed to get tcm resource" / err -22,
# or the 2nd R5's rpmsg channel never appear (vrings fall back to CMA ->
# rpmsg_create_ept -RPMSG_ERR_NO_BUFF). See the patch header for the four
# specific fixes.
#
# NOTE: lopper-native is consumed by esw-conf-native (which gen-machineconf runs),
# so after adding this, rebuild both for the fix to reach gen-machineconf:
#   bitbake -c cleansstate lopper-native esw-conf-native
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# Backward compatibility: only apply on the meta-xilinx release that carries the
# buggy assist (xlnx-rel-v2026.1). XILINX_RELEASE_VERSION is set by
# meta-xilinx-core/conf/layer.conf and tracks the checked-out xlnx-rel-vYYYY.N
# tag. On any other release the bbappend is inert -- the patch isn't added, so it
# can't fail do_patch against a differently-versioned lopper source. Extend the
# value list here if a later release ships the same unfixed assist.
LOPPER_R5_FIX_RELEASES = "v2026.1"
SRC_URI += "${@bb.utils.contains_any('XILINX_RELEASE_VERSION', d.getVar('LOPPER_R5_FIX_RELEASES'), 'file://0001-openamp_xlnx-fix-split-mode-dual-r5.patch', '', d)}"
