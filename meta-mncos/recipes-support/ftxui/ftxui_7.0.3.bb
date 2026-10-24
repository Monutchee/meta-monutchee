SUMMARY = "Functional terminal user interface library for C++"
HOMEPAGE = "https://github.com/ArthurSonzogni/FTXUI"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=602507f167b627b30ce2cd7a24d50ea3"

# Update the release tag and immutable revision together.
FTXUI_TAG ?= "v7.0.3"
SRC_URI = "git://github.com/ArthurSonzogni/FTXUI.git;protocol=https;nobranch=1"
SRCREV ?= "f921fad208912747c17d129a8ef75ec7624b6eec"
S = "${WORKDIR}/git"

inherit cmake

EXTRA_OECMAKE = " \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DFTXUI_ENABLE_INSTALL=ON \
    -DFTXUI_BUILD_DOCS=OFF \
    -DFTXUI_BUILD_EXAMPLES=OFF \
    -DFTXUI_BUILD_MODULES=OFF \
    -DFTXUI_BUILD_TESTS=OFF \
    -DFTXUI_BUILD_TESTS_FUZZER=OFF \
    -DFTXUI_ENABLE_CCACHE=OFF \
"

# Static consumers ship notices without installing development archives/headers.
PACKAGES =+ "${PN}-licenses"
FILES:${PN}-licenses = "${datadir}/licenses/${PN}"
RDEPENDS:${PN}-dev = "${PN}-staticdev ${PN}-licenses"
RDEPENDS:${PN}-staticdev = ""

do_install:append() {
    install -d ${D}${datadir}/licenses/${PN}
    install -m 0644 ${S}/LICENSE ${D}${datadir}/licenses/${PN}/LICENSE
}

python do_unpack:append() {
    tag = d.getVar("FTXUI_TAG")
    commit, _ = bb.process.run(["git", "-C", d.getVar("S"),
                                "rev-parse", "refs/tags/" + tag + "^{commit}"])
    if commit.strip() != d.getVar("SRCREV"):
        bb.fatal("FTXUI tag %s does not match SRCREV" % tag)
}
