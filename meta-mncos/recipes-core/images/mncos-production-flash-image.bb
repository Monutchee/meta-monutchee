SUMMARY = "Generic MNCOS production flashing image"
DESCRIPTION = "Vendor-neutral RAM-resident base image extended by each machine layer with its production flashing implementation."
LICENSE = "MIT"

inherit core-image mncos-image-identity

MNCOS_IMAGE_ROLE = "production-flash"
MNCOS_IMAGE_LABEL = "MNCOS PRODUCTION FLASH IMAGE"

# Machine and product layers extend this variable with the packages that know
# how to fetch, validate and install that product's production image.
MNCOS_PRODUCTION_FLASH_EXTRA_INSTALL ?= ""

IMAGE_INSTALL = " \
    packagegroup-core-boot \
    systemd-conf \
    busybox \
    ${MNCOS_PRODUCTION_FLASH_EXTRA_INSTALL} \
"

IMAGE_FEATURES = ""
IMAGE_LINGUAS = ""
IMAGE_ROOTFS_SIZE = "32768"
IMAGE_ROOTFS_EXTRA_SPACE = "0"
NO_RECOMMENDATIONS = "1"

# A compressed initramfs is the portable contract. Machine layers can add the
# wrapper or additional format required by their boot ROM and host-side tool.
MNCOS_PRODUCTION_FLASH_IMAGE_FSTYPES ?= "cpio.gz"

python () {
    d.setVar("IMAGE_FSTYPES", d.getVar("MNCOS_PRODUCTION_FLASH_IMAGE_FSTYPES"))
}
