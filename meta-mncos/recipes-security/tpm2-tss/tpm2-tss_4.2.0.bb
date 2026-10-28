SUMMARY = "TPM 2.0 software stack with ESAPI and resource-manager transport"
HOMEPAGE = "https://github.com/tpm2-software/tpm2-tss"
LICENSE = "BSD-2-Clause"
LIC_FILES_CHKSUM = "file://LICENSE;md5=500b2e742befc3da00684d8a1d5fd9da"
SRC_URI = "https://github.com/tpm2-software/tpm2-tss/releases/download/${PV}/tpm2-tss-${PV}.tar.gz"
SRC_URI[sha256sum] = "b53f0c5c8c4ce17f05701a410ca9688f725ca380c9bc4640eacd0eadb1fea124"
DEPENDS = "autoconf-archive-native openssl"
inherit autotools pkgconfig useradd
EXTRA_OECONF = "--disable-doxygen-doc --disable-unit --disable-integration \
    --disable-fapi --disable-policy --disable-tcti-libtpms \
    --disable-tcti-spi-ltt2go --disable-tcti-spi-ftdi --disable-tcti-i2c-ftdi \
    --with-crypto=ossl --with-tctidefaultmodule=libtss2-tcti-device.so.0 \
    --with-tctidefaultconfig=/dev/tpmrm0 --with-udevrulesdir=${nonarch_base_libdir}/udev/rules.d \
    --with-sysusersdir=no --with-tmpfilesdir=no"
USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "--system tss"
USERADD_PARAM:${PN} = "--system --no-create-home --home / --gid tss --shell ${base_sbindir}/nologin tss"
FILES:${PN} += "${nonarch_base_libdir}/udev/rules.d"
BBCLASSEXTEND = "native"
# Runtime event-log directories belong to the enabled FAPI service. FAPI is
# disabled here and /run is created at boot, never shipped as persistent state.
do_install:append() {
    rm -rf ${D}/run ${D}${localstatedir}/run
}
