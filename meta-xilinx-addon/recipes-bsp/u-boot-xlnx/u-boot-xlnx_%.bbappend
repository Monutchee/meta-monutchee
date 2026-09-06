FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "file://modify_feature.cfg"
SRC_URI += "file://mnc_u-boot-xlnx-addon.env"

# U-Boot reads its default-environment text from
#   board/${CONFIG_SYS_VENDOR}/${CONFIG_SYS_BOARD}/<CONFIG_ENV_SOURCE_FILE>.env
# For these ZynqMP targets that resolves to
# board/xilinx/zynqmp/mnc_u-boot-xlnx-addon.env, and modify_feature.cfg sets
# CONFIG_ENV_SOURCE_FILE accordingly. Drop the file into the source tree before
# the environment is generated (do_compile). SRC_URI files are unpacked into
# ${WORKDIR} on this release (Yocto scarthgap).
do_configure:prepend() {
    install -D -m 0644 "${WORKDIR}/mnc_u-boot-xlnx-addon.env" \
        "${S}/board/xilinx/zynqmp/mnc_u-boot-xlnx-addon.env"
}
