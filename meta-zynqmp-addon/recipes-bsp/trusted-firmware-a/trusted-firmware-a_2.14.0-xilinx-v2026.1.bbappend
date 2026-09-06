# Shared ZynqMP policy tied to the reviewed TF-A version. Revalidate the
# topology patch and R5 clock behavior when updating the firmware provider.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append:zynqmp = " file://0001-zynqmp-keep-rpll-critical.patch"
