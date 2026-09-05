# MNCOS owns distribution policy; OE-Core loads defaultsetup.conf afterwards.
DISTRO_CODENAME = "scarthgap"
LOCALCONF_VERSION = "2"

# Preserve the previous distribution defaults when headless mode is disabled.
DISTRO_FEATURES ?= "${DISTRO_FEATURES_DEFAULT} opengl ptest multiarch wayland vulkan"
DISTRO_EXTRA_RDEPENDS += "packagegroup-core-boot"
DISTRO_EXTRA_RRECOMMENDS += "kernel-module-af-packet"
PREFERRED_VERSION_linux-yocto ?= "6.6%"
PREFERRED_VERSION_linux-yocto-rt ?= "6.6%"
PACKAGE_CLASSES ?= "package_rpm"
TCLIBCAPPEND = ""
SDK_NAME = "${DISTRO}-${TCLIBC}-${SDKMACHINE}-${IMAGE_BASENAME}-${TUNE_PKGARCH}-${MACHINE}"
BB_SIGNATURE_HANDLER ?= "OEEquivHash"
BB_HASHSERVE ??= "auto"

require conf/distro/include/no-static-libs.inc
require conf/distro/include/yocto-uninative.inc
require conf/distro/include/security_flags.inc
INHERIT += "uninative create-spdx cve-check mncos-policy"
# Kernel SPDX generation needs debug symbols, as in the previous distro.
KERNEL_EXTRA_FEATURES:append = " features/debug/debug-kernel.scc"
# OE-Core's default-distrovars.inc supplies BB_STRICT_CHECKSUM = "1".
CVE_CHECK_FORMAT_JSON = "1"
CVE_CHECK_CREATE_MANIFEST = "1"
CVE_CHECK_COPY_FILES = "1"

# This is Linux target policy. Native and SDK host tools retain their features.
MNCOS_HEADLESS ?= "1"
MNCOS_HEADLESS_REMOVE_DISTRO_FEATURES = "x11 wayland opengl vulkan directfb fbdev libmali"
MNCOS_HEADLESS_REMOVE_IMAGE_FEATURES = "x11 x11-base x11-sato weston splash"
DISTRO_FEATURES:remove:class-target = "${@d.getVar('MNCOS_HEADLESS_REMOVE_DISTRO_FEATURES') if d.getVar('MNCOS_HEADLESS') == '1' else ''}"
MACHINE_FEATURES:remove:class-target = "${@'mali400' if d.getVar('MNCOS_HEADLESS') == '1' else ''}"
IMAGE_FEATURES:remove:class-target = "${@d.getVar('MNCOS_HEADLESS_REMOVE_IMAGE_FEATURES') if d.getVar('MNCOS_HEADLESS') == '1' else ''}"
EXTRA_IMAGE_FEATURES:remove:class-target = "${@d.getVar('MNCOS_HEADLESS_REMOVE_IMAGE_FEATURES') if d.getVar('MNCOS_HEADLESS') == '1' else ''}"
