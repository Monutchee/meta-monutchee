FILESEXTRAPATHS:prepend := "${THISDIR}/${PN}:"

SRC_URI += "file://10-mncos-usedomains.conf"

# Augment systemd-conf's default 80-wired.network with a drop-in that enables
# UseDomains, so the DHCP option 15 domain reaches systemd-resolved.
# Gated on the same dhcp-ethernet PACKAGECONFIG that installs 80-wired.network,
# so we never leave a drop-in pointing at a non-existent .network file.
do_install:append() {
    if ${@bb.utils.contains('PACKAGECONFIG', 'dhcp-ethernet', 'true', 'false', d)}; then
        install -D -m0644 ${WORKDIR}/10-mncos-usedomains.conf \
            ${D}${systemd_unitdir}/network/80-wired.network.d/10-mncos-usedomains.conf
    fi
}
