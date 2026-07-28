SUMMARY = "Temporary restricted MSAP1 AI diagnostics account"
DESCRIPTION = "Creates the opt-in debugai account and confines SSH sessions to diagnostic-only machine-readable mnc commands."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://60-msap1-debugai.conf"

S = "${WORKDIR}"

inherit useradd

RDEPENDS:${PN} = "msap1-apu-app openssh-sshd"

USERADD_PACKAGES = "${PN}"
# Repeat the shared groups defensively so the account can be constructed in
# this recipe's isolated sysroot. At image install time useradd.bbclass keeps
# an existing group instead of recreating it.
GROUPADD_PARAM:${PN} = "--system msap1-data; --system systemd-journal"
USERADD_PARAM:${PN} = " \
    --create-home \
    --home-dir /var/lib/debugai \
    --shell /usr/bin/mnc-ssh-gateway \
    --groups msap1-data,systemd-journal \
    --password '\$6\$mncdebugai\$uAytevGe0YCxUwm.TphDELwLvq7Ks8bCgEw5E1x0U6Bn0FcFGkLYqRzyjgWrFAGiU2ZpSHIJnzGth7uXEUoq2/' \
    debugai \
"

do_install() {
    install -d "${D}${sysconfdir}/ssh/sshd_config.d"
    install -m 0600 "${WORKDIR}/60-msap1-debugai.conf" \
        "${D}${sysconfdir}/ssh/sshd_config.d/60-msap1-debugai.conf"
}

FILES:${PN} = "${sysconfdir}/ssh/sshd_config.d/60-msap1-debugai.conf"
