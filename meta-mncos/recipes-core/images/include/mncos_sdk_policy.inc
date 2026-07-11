# mncos_sdk_policy.inc
#
# MNCOS SDK generation and export policy shared by product image recipes.

# Use a stable, product-facing installer name instead of Yocto's verbose
# distro/image/host/tune/machine-derived default.
TOOLCHAIN_OUTPUTNAME = "${PROJECT_NAME}-toolchain-${SDK_VERSION}"

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
