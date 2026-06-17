SUMMARY = "Extremely fast JSON and reflection library for modern C++"
HOMEPAGE = "https://stephenberry.github.io/glaze/"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ea4d29875d83fbbf50485c846dbbbed8"

SRC_URI = "git://github.com/stephenberry/glaze;protocol=https;branch=main"
SRCREV = "ae87b187e2264ad452777bca68e35595406e9dca"

S = "${WORKDIR}/git"

inherit cmake

EXTRA_OECMAKE = " \
    -Dglaze_BUILD_EXAMPLES=OFF \
    -DBUILD_TESTING=OFF \
    -Dglaze_ENABLE_FUZZING=OFF \
    -Dglaze_DEVELOPER_MODE=OFF \
"

FILES:${PN}-dev += "${datadir}/${BPN}/*.cmake"

# Glaze is header-only, so consumers need its development package contents.
ALLOW_EMPTY:${PN} = "1"

BBCLASSEXTEND = "native"
