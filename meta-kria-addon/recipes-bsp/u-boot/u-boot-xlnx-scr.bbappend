KRIA_BOOTCMD_USB_ROOTFS_PARTUUID ?= ""

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append = "${@' file://0001-boot-cmd-kria-use-partuuid-root-for-usb-boot.patch;patchdir=..' if d.getVar('KRIA_BOOTCMD_USB_ROOTFS_PARTUUID') == '1' else ''}"
