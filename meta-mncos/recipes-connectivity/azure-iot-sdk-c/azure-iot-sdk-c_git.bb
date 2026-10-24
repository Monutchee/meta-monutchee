SUMMARY = "Azure IoT C SDK for MQTT device and provisioning clients"
HOMEPAGE = "https://github.com/Azure/azure-iot-sdk-c"
LICENSE = "MIT"
LIC_FILES_CHKSUM = " \
    file://LICENSE;md5=4283671594edec4c13aeb073c219237a \
    file://c-utility/LICENSE;md5=4283671594edec4c13aeb073c219237a \
    file://umqtt/LICENSE;md5=6e1bb384cedd6442b3a2b9a5b531e005 \
    file://deps/azure-macro-utils-c/LICENSE;md5=4283671594edec4c13aeb073c219237a \
    file://deps/c-build-tools/LICENSE;md5=b98fddd052bb2f5ddbcdbd417ffb26a8 \
    file://deps/c-logging/LICENSE;md5=d4a904ca135bb7bc912156fee12726f0 \
    file://deps/umock-c/LICENSE;md5=4f9c2c296f77b3096b6c11a16fa7c66e \
    file://deps/parson/LICENSE;md5=5bbf3d18d8ed39559cb984dfe7c3a1ad \
"

# Update this tag, its immutable SRCREV, and the submodule revisions together.
# Fetch by immutable commit; validate the human-readable tag after unpacking.
AZURE_IOT_SDK_C_TAG ?= "LTS_10_2026"
PV = "1.15.0+git"
SRCREV_sdk = "220d5b7bb908c90dad387f2b6366c5ec269046ee"
SRCREV_cutility = "aed4fb49f2e70be535a2f8fef8c51844387f37e2"
SRCREV_umqtt = "545f470a78c44f2e8405e0f14dff7a9f1774c252"
SRCREV_macroutils = "045636acd64c0edf67c1b07fcfc0901d0ecfbd64"
SRCREV_buildtools = "b26bcf4a6a8eda025b1dce0a6bbfe02e2f7bb4e5"
SRCREV_logging = "504e7d4765adc718957cb32cf7699d9e9272c727"
SRCREV_umock = "e54b586e00f26f36f8f96e389738b8c798ad6fb7"
SRCREV_parson = "60c37844d7a1c97547812cac3423d458c73e60f9"
SRCREV_FORMAT = "sdk_cutility_umqtt_macroutils_buildtools_logging_umock_parson"

# Fetch only production dependencies, at the revisions recorded by the SDK tag.
# Explicit git fetches avoid gitsm's recursive test/tool dependency trees while
# retaining BitBake mirroring, source archiving, checksums and offline builds.
SRC_URI = " \
    git://github.com/Azure/azure-iot-sdk-c.git;protocol=https;nobranch=1;name=sdk;destsuffix=git \
    git://github.com/Azure/azure-c-shared-utility.git;protocol=https;nobranch=1;name=cutility;destsuffix=git/c-utility \
    git://github.com/Azure/azure-umqtt-c.git;protocol=https;nobranch=1;name=umqtt;destsuffix=git/umqtt \
    git://github.com/Azure/macro-utils-c.git;protocol=https;nobranch=1;name=macroutils;destsuffix=git/deps/azure-macro-utils-c \
    git://github.com/Azure/c-build-tools;protocol=https;nobranch=1;name=buildtools;destsuffix=git/deps/c-build-tools \
    git://github.com/Azure/c-logging;protocol=https;nobranch=1;name=logging;destsuffix=git/deps/c-logging \
    git://github.com/Azure/umock-c;protocol=https;nobranch=1;name=umock;destsuffix=git/deps/umock-c \
    git://github.com/kgabis/parson.git;protocol=https;nobranch=1;name=parson;destsuffix=git/deps/parson \
"
S = "${WORKDIR}/git"

DEPENDS = "openssl"
inherit cmake

EXTRA_OECMAKE = " \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_TESTING=OFF \
    -Dbuild_as_dynamic=OFF \
    -Duse_installed_dependencies=OFF \
    -Duse_prov_client=ON \
    -Dhsm_type_x509=ON \
    -Dhsm_type_symm_key=ON \
    -Dhsm_type_sastoken=OFF \
    -Dhsm_type_riot=OFF \
    -Duse_tpm_simulator=OFF \
    -Duse_amqp=OFF \
    -Duse_http=OFF \
    -Duse_mqtt=ON \
    -Duse_openssl=ON \
    -Duse_default_uuid=ON \
    -Ddont_use_uploadtoblob=ON \
    -Dbuild_service_client=OFF \
    -Dbuild_provisioning_service_client=OFF \
    -Dskip_samples=ON \
    -Drun_e2e_tests=OFF \
    -Drun_unittests=OFF \
    -Dno_logging=ON \
"

# The SDK is linked statically into consumers. Ship its complete upstream
# notices separately so consumers can retain them without SDK source trees.
PACKAGES =+ "${PN}-licenses"
FILES:${PN}-licenses = "${datadir}/licenses/${PN}"
FILES:${PN}-dev += "${prefix}/cmake"
RDEPENDS:${PN}-dev = "${PN}-staticdev"
RDEPENDS:${PN}-staticdev = ""

# Upstream installs some dependency CMake metadata directly under prefix/cmake.
# Consumers discover the installed production headers and archives directly.
do_install:append() {
    # Upstream exports absolute OpenSSL paths from the build sysroot. Preserve
    # ordinary library lookup so the development package remains relocatable.
    sed -i -e 's#${RECIPE_SYSROOT}${libdir}/libssl.so#ssl#g' \
           -e 's#${RECIPE_SYSROOT}${libdir}/libcrypto.so#crypto#g' \
        ${D}${libdir}/cmake/azure_c_shared_utility/azure_c_shared_utilityTargets.cmake

    install -d ${D}${datadir}/licenses/${PN}
    for component in . c-utility umqtt deps/azure-macro-utils-c deps/c-build-tools deps/c-logging deps/umock-c deps/parson; do
        case "$component" in
            .) notice=azure-iot-sdk-c ;;
            *) notice=$(printf '%s' "$component" | tr / -) ;;
        esac
        install -m 0644 ${S}/$component/LICENSE ${D}${datadir}/licenses/${PN}/$notice.LICENSE
    done
}

python do_unpack:append() {
    tag = d.getVar("AZURE_IOT_SDK_C_TAG")
    commit, _ = bb.process.run(["git", "-C", d.getVar("S"),
                                "rev-parse", "refs/tags/" + tag + "^{commit}"])
    if commit.strip() != d.getVar("SRCREV_sdk"):
        bb.fatal("Azure SDK tag %s does not match SRCREV_sdk" % tag)
    for path, name in (
        ("c-utility", "cutility"), ("umqtt", "umqtt"),
        ("deps/azure-macro-utils-c", "macroutils"),
        ("deps/c-build-tools", "buildtools"), ("deps/c-logging", "logging"),
        ("deps/umock-c", "umock"), ("deps/parson", "parson"),
    ):
        entry, _ = bb.process.run(["git", "-C", d.getVar("S"),
                                   "ls-tree", "HEAD", path])
        if entry.split()[2] != d.getVar("SRCREV_" + name):
            bb.fatal("Azure SDK submodule %s does not match SRCREV_%s" % (path, name))
}
