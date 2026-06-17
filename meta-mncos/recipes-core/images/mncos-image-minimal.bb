DESCRIPTION = "Minimal image of Monutchee OS"
LICENSE = "MIT"

require include/mncos-image-common.inc

inherit mncos-image-identity

MNCOS_IMAGE_ROLE = "main"
MNCOS_IMAGE_LABEL = "MNCOS MAIN SYSTEM IMAGE"


# Function to copy the .WIC.XZ image to export/image
do_copy_wic_image() {
    DEST_DIR="${TOPDIR}/export/image"
    mkdir -p ${DEST_DIR}
    cp ${DEPLOY_DIR_IMAGE}/*.wic.xz ${DEST_DIR}/ || true
}
#Ensures the task always runs.
do_copy_wic_image[nostamp] = "1"
addtask copy_wic_image after do_image_complete before do_build

# Function to copy the SDK
do_copy_sdk() {
    DEST_DIR="${TOPDIR}/export/sdk"
    mkdir -p ${DEST_DIR}
    cp ${SDK_DEPLOY}/*.sh ${DEST_DIR}/ || true
}
#Ensures the task always runs.
do_copy_sdk[nostamp] = "1"
addtask copy_sdk after do_populate_sdk
