SUMMARY = "MNC service users and groups"
DESCRIPTION = "Creates every system account that MNC software runs as, or that \
owns MNC state: mnc-data, mnc-settings, mnc-stream, mnc-historian and mnc-web. \
Provided at the OS level so the accounts exist independently of any one \
application package, and so their pinned uid/gid values are declared in exactly \
one place. Consumers add this to RDEPENDS rather than declaring accounts \
themselves."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# The numbers and account shapes live here; see the header of that file before
# changing anything. Ids are permanent once shipped.
require conf/include/mnc-identities.inc

inherit useradd

# This recipe ships no files; it exists solely to create the accounts.
USERADD_PACKAGES = "${PN}"
ALLOW_EMPTY:${PN} = "1"

GROUPADD_PARAM:${PN} = "${MNC_GROUPADD_SHARED}"
USERADD_PARAM:${PN} = "${MNC_USERADD_SHARED}"

do_install() {
    :
}
