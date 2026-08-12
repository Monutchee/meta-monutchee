SUMMARY = "Temporary restricted MSAP1 AI diagnostics account"
DESCRIPTION = "Creates the opt-in debugai account and confines SSH sessions to diagnostic-only machine-readable mnc commands."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://60-msap1-debugai.conf"

S = "${WORKDIR}"

inherit useradd

# mnc-users creates the shared mnc-* groups this account joins, and must be
# installed first so they exist when useradd runs for debugai below.
RDEPENDS:${PN} = "msap1-apu-app mnc-users openssh-sshd"

USERADD_PACKAGES = "${PN}"
# Only the debugai account itself is declared here. The shared mnc-* groups are
# owned by mnc-users; re-declaring them locally is what previously let two
# recipes disagree about their gids. systemd-journal is a distro group.
#
# If a future build cannot resolve the mnc-* groups at this recipe's useradd
# time, restore a defensive GROUPADD_PARAM that expands ${MNC_GROUPADD_SHARED}
# from meta-mncos/conf/include/mnc-identities.inc - never one with the numbers
# written out again, which is how they drifted apart in the first place.
GROUPADD_PARAM:${PN} = "--system systemd-journal"
USERADD_PARAM:${PN} = " \
    --create-home \
    --home-dir /var/lib/debugai \
    --shell /usr/bin/mnc-ssh-gateway \
    --groups mnc-data,mnc-settings,systemd-journal \
    --password '\$6\$mncdebugai\$uAytevGe0YCxUwm.TphDELwLvq7Ks8bCgEw5E1x0U6Bn0FcFGkLYqRzyjgWrFAGiU2ZpSHIJnzGth7uXEUoq2/' \
    debugai \
"

do_install() {
    install -d "${D}${sysconfdir}/ssh/sshd_config.d"
    install -m 0600 "${WORKDIR}/60-msap1-debugai.conf" \
        "${D}${sysconfdir}/ssh/sshd_config.d/60-msap1-debugai.conf"
}

FILES:${PN} = "${sysconfdir}/ssh/sshd_config.d/60-msap1-debugai.conf"
