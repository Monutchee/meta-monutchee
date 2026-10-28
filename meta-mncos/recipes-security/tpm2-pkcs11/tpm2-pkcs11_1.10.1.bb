SUMMARY = "TPM-backed PKCS11 provider library"
HOMEPAGE = "https://github.com/tpm2-software/tpm2-pkcs11"
LICENSE = "BSD-2-Clause"
LIC_FILES_CHKSUM = "file://LICENSE;md5=0fc19f620a102768d6dbd1e7166e78ab"
SRC_URI = "https://github.com/tpm2-software/tpm2-pkcs11/releases/download/${PV}/tpm2-pkcs11-${PV}.tar.gz \
    file://0001-fix-RSA-PSS-state-discovery.patch \
    file://0002-avoid-recursive-provider-public-crypto.patch"
SRC_URI[sha256sum] = "f3315d17811f918779a1046ba20e49060a40b2b586a06ab013ff8c8da53752e4"
DEPENDS = "autoconf-archive-native tpm2-tss openssl sqlite3 libyaml p11-kit"
inherit autotools pkgconfig
# Native callers provision tokens through PKCS11. Do not install the optional
# Python provisioning CLI or make an unused Python stack a runtime dependency.
EXTRA_OECONF = "--disable-ptool-checks --disable-unit --disable-integration --without-fapi \
    --with-p11kitmoduledir=${libdir}/pkcs11 --with-p11kitconfigdir=${datadir}/p11-kit/modules"
FILES:${PN} += "${libdir}/pkcs11/*.so* ${datadir}/p11-kit/modules"
FILES:${PN}-dev += "${libdir}/pkcs11/*.la ${libdir}/pkgconfig"
FILES:${PN}-staticdev += "${libdir}/pkcs11/*.a"
# An unversioned .so is the actual dlopen module, not a development linker stub.
INSANE_SKIP:${PN} += "dev-so"
BBCLASSEXTEND = "native"
