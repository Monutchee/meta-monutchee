SUMMARY = "Practical base utilities for MNCOS products"
LICENSE = "MIT"

inherit packagegroup

# Select command packages, not util-linux's all-utilities recommendations.
# Consumer recipes must declare additional commands in their own RDEPENDS;
# ordinary library and package dependencies remain fully enabled.
RDEPENDS:${PN} = " \
    util-linux-agetty \
    util-linux-blkid \
    util-linux-blockdev \
    util-linux-chrt \
    util-linux-dmesg \
    util-linux-fdisk \
    util-linux-findmnt \
    util-linux-flock \
    util-linux-fsck \
    util-linux-fstrim \
    util-linux-getopt \
    util-linux-hexdump \
    util-linux-hwclock \
    util-linux-ionice \
    util-linux-kill \
    util-linux-logger \
    util-linux-lsblk \
    util-linux-lscpu \
    util-linux-mount \
    util-linux-mountpoint \
    util-linux-nologin \
    util-linux-renice \
    util-linux-rfkill \
    util-linux-setsid \
    util-linux-sulogin \
    util-linux-swaponoff \
    util-linux-taskset \
    util-linux-umount \
"
