FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "file://modify_feature.cfg"
SRC_URI += "file://msys.env"

# U-Boot reads its default-environment text from
#   board/${CONFIG_SYS_VENDOR}/${CONFIG_SYS_BOARD}/<CONFIG_ENV_SOURCE_FILE>.env
# For this ZynqMP target that resolves to board/xilinx/zynqmp/msys.env, and
# modify_feature.cfg sets CONFIG_ENV_SOURCE_FILE="msys". Drop the file into the
# source tree before the environment is generated (do_compile). SRC_URI files
# are unpacked into ${WORKDIR} on this release (Yocto scarthgap).
do_configure:prepend() {
    install -D -m 0644 "${WORKDIR}/msys.env" "${S}/board/xilinx/zynqmp/msys.env"
}
