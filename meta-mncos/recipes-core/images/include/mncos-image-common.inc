# mncos-image-common.inc
#
# Common MNCOS image scaffolding and base package set, shared by all product
# images (zudemo-image, future kr260demo-image, ...).
#
# A product image `require`s this file, then customises the package set:
#
#   IMAGE_INSTALL:append = " my-extra-pkg"      # add product-specific packages
#   IMAGE_INSTALL:remove = " htop webengine"    # trim packages from the base set
#
# NOTE: ':remove' is applied last and overrides ':append', so it deterministically
# drops a base package regardless of where it was added. It is silent when the
# package is not present (e.g. a typo), so treat the lists below as the source of
# truth for what can be removed.

inherit core-image mncos-image-identity

# --- Base package set -------------------------------------------------------
# Grouped for readability. Products should not edit these directly; use
# IMAGE_INSTALL:append / IMAGE_INSTALL:remove in the image recipe instead.
MNCOS_COMMON_INSTALL = " \
    u-boot-tools \
    u-boot-fw-utils \
    libubootenv \
    openssl \
    jq \
    util-linux \
    net-tools \
    traceroute \
"

MNCOS_LIB_INSTALL = " \
    net-snmp \
    curl \
    sqlite3 \
    libsodium \
    paho-mqtt-cpp \
    boost \
"

MNCOS_APP_INSTALL = " \
    wireguard-tools \
    nginx \
    worker-user \
    webengine \
    openssh \
    htop \
    tar \
    xz \
"

IMAGE_INSTALL:append = " ${MNCOS_COMMON_INSTALL} ${MNCOS_LIB_INSTALL} ${MNCOS_APP_INSTALL}"

# --- Common image export tasks ---------------------------------------------
# Copy the final .wic.xz image into export/image for convenience.
do_copy_wic_image() {
    DEST_DIR="${TOPDIR}/export/image"
    mkdir -p ${DEST_DIR}
    cp ${DEPLOY_DIR_IMAGE}/*.wic.xz ${DEST_DIR}/ || true
}

# Ensures the task always runs.
do_copy_wic_image[nostamp] = "1"
addtask copy_wic_image after do_image_complete before do_build

# Copy the populated SDK installer into export/sdk.
do_copy_sdk() {
    DEST_DIR="${TOPDIR}/export/sdk"
    mkdir -p ${DEST_DIR}
    cp ${SDK_DEPLOY}/*.sh ${DEST_DIR}/ || true
}

# Ensures the task always runs.
do_copy_sdk[nostamp] = "1"
addtask copy_sdk after do_populate_sdk
