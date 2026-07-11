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

# Copy a freshly populated SDK installer into export/sdk.  Use SDKDEPLOYDIR
# here because SDK_DEPLOY is populated by the sstate machinery only after the
# do_populate_sdk task function and its post-functions have completed.
copy_sdk_to_export() {
    DEST_DIR="${TOPDIR}/export/sdk"
    mkdir -p ${DEST_DIR}
    for SDK_INSTALLER in ${SDKDEPLOYDIR}/*.sh; do
        [ -f "${SDK_INSTALLER}" ] || continue
        cp "${SDK_INSTALLER}" "${DEST_DIR}/"
    done
}

# Run the export as part of do_populate_sdk so it also happens when callers
# explicitly select `bitbake <image> -c populate_sdk`.
do_populate_sdk[postfuncs] += "copy_sdk_to_export"
PSEUDO_IGNORE_PATHS .= ",${TOPDIR}/export/sdk"

# If populate_sdk is restored from sstate, the installer is restored directly
# into SDK_DEPLOY rather than being created in SDKDEPLOYDIR.
copy_restored_sdk_to_export() {
    DEST_DIR="${TOPDIR}/export/sdk"
    mkdir -p ${DEST_DIR}
    cp -f ${SDK_DEPLOY}/*.sh "${DEST_DIR}/"
}

do_populate_sdk_setscene[postfuncs] += "copy_restored_sdk_to_export"

# Provide a cheap, always-run task for recreating export/sdk from an SDK that
# has already been deployed.  It deliberately does not depend on
# do_populate_sdk, so an ordinary image build never generates an SDK merely to
# satisfy this convenience export.
do_copy_sdk() {
    DEST_DIR="${TOPDIR}/export/sdk"
    SDK_INSTALLER="${TOOLCHAIN_OUTPUTNAME}.sh"

    if [ ! -f "${DEST_DIR}/${SDK_INSTALLER}" ] && \
       [ -f "${SDK_DEPLOY}/${SDK_INSTALLER}" ]; then
        mkdir -p "${DEST_DIR}"
        cp -f "${SDK_DEPLOY}/${SDK_INSTALLER}" "${DEST_DIR}/"
    fi
}

do_copy_sdk[nostamp] = "1"
addtask copy_sdk before do_build
