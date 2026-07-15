SUMMARY = "MSAP1 APU application"
DESCRIPTION = "Builds the Linux RPMsg application used to visualize AD7771 samples supplied by R5 core 0."
HOMEPAGE = "https://github.com/Monutchee/MSAP1_APU"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"

# Source switch: "cloud" (GitHub, default) or "local" (a Git checkout).
# Local Git mode builds committed state. Use externalsrc/devtool when testing
# uncommitted APU changes.
MSAP1_APU_APP_SRC ?= "cloud"
MSAP1_APU_APP_GIT_BRANCH ?= "main"
MSAP1_APU_APP_LOCAL_DIR ?= "${TOPDIR}/../../MSAP1_APU"

MSAP1_APU_APP_REPO_cloud = "git://github.com/Monutchee/MSAP1_APU.git;protocol=https;branch=${MSAP1_APU_APP_GIT_BRANCH};name=msap1-apu-app;destsuffix=git"
MSAP1_APU_APP_REPO_local = "git://${MSAP1_APU_APP_LOCAL_DIR};protocol=file;branch=${MSAP1_APU_APP_GIT_BRANCH};name=msap1-apu-app;destsuffix=git"

SRC_URI = "${@d.getVar('MSAP1_APU_APP_REPO_' + (d.getVar('MSAP1_APU_APP_SRC') or 'cloud'))}"
SRCREV_msap1-apu-app ?= "${AUTOREV}"
PV = "1.0+git${SRCPV}"

S = "${WORKDIR}/git"

inherit cmake

EXTRA_OECMAKE = "-DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF"
