SUMMARY = "APU to RPU RPMsg control utility"
DESCRIPTION = "Builds apu-rpu-ctl, a Linux RPMsg char-device client for demo RPU control firmware."
HOMEPAGE = "https://github.com/Monutchee/ZuBoardDemo_APU"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=bea232fc293d2909c632f6cdc3edc644"

# Source switch: "cloud" (GitHub, default) or "local" (a git checkout).
# Flip in local.conf:  APU_RPU_CTL_SRC = "local"
# Both modes build committed Git state. For local mode, commit the APU repo
# before building, or use devtool/externalsrc for live work-tree iteration.
APU_RPU_CTL_SRC ?= "cloud"
APU_RPU_CTL_GIT_BRANCH ?= "main"
APU_RPU_CTL_LOCAL_DIR ?= "${TOPDIR}/../../ZuBoardDemo_APU"

# gitsm:// (not git://): the APU repos carry the rpmsg transport library as
# a git submodule (Monutchee/OpenAMP-helper-APU); git:// would silently skip it
# and the CMake add_subdirectory(libs/openamp-helper) step would fail.
APU_RPU_CTL_REPO_cloud = "gitsm://github.com/Monutchee/ZuBoardDemo_APU.git;protocol=https;branch=${APU_RPU_CTL_GIT_BRANCH};name=apu-rpu-ctl;destsuffix=git"
APU_RPU_CTL_REPO_local = "gitsm://${APU_RPU_CTL_LOCAL_DIR};protocol=file;branch=${APU_RPU_CTL_GIT_BRANCH};name=apu-rpu-ctl;destsuffix=git"

SRC_URI = "${@d.getVar('APU_RPU_CTL_REPO_' + (d.getVar('APU_RPU_CTL_SRC') or 'cloud'))}"
SRCREV_apu-rpu-ctl ?= "${AUTOREV}"
PV = "1.0+git${SRCPV}"

S = "${WORKDIR}/git"

inherit cmake

EXTRA_OECMAKE = "-DCMAKE_BUILD_TYPE=Release"
