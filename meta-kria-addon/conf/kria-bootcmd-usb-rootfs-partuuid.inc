# Opt in to a u-boot-xlnx-scr patch that makes boot.cmd.kria pass a
# kernel-native root= value when the boot script is loaded from USB.
#
# This fixes QEMU flows that expose the WIC root disk as USB storage while
# keeping the behavior opt-in for machine/product layers.
KRIA_BOOTCMD_USB_ROOTFS_PARTUUID = "1"
