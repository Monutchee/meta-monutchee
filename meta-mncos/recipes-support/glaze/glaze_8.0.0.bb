SUMMARY = "Extremely fast JSON and reflection library for modern C++"
HOMEPAGE = "https://stephenberry.github.io/glaze/"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ea4d29875d83fbbf50485c846dbbbed8"

# Update the release tag and immutable revision together.
GLAZE_TAG ?= "v8.0.0"
SRC_URI = "git://github.com/stephenberry/glaze;protocol=https;nobranch=1"
SRCREV ?= "be4481f4b106fc82f0b7bc85f6a92202f8c0dd59"

S = "${WORKDIR}/git"

inherit cmake

EXTRA_OECMAKE = " \
    -Dglaze_BUILD_EXAMPLES=OFF \
    -Dglaze_DEVELOPER_MODE=OFF \
    -Dglaze_INSTALL=ON \
"

FILES:${PN}-dev += "${datadir}/${BPN}/*.cmake"

# Glaze is header-only, so consumers need its development package contents.
ALLOW_EMPTY:${PN} = "1"

BBCLASSEXTEND = "native"

# Header consumers retain notices without installing development headers.
PACKAGES =+ "${PN}-licenses"
FILES:${PN}-licenses = "${datadir}/licenses/${PN}"
RDEPENDS:${PN}-dev = "${PN}-licenses"

do_install:append() {
    install -d ${D}${datadir}/licenses/${PN}
    install -m 0644 ${S}/LICENSE ${D}${datadir}/licenses/${PN}/LICENSE
}

python do_unpack:append() {
    tag = d.getVar("GLAZE_TAG")
    commit, _ = bb.process.run(["git", "-C", d.getVar("S"),
                                "rev-parse", "refs/tags/" + tag + "^{commit}"])
    if commit.strip() != d.getVar("SRCREV"):
        bb.fatal("Glaze tag %s does not match SRCREV" % tag)
}
