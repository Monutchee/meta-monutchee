FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append = " ${@bb.utils.contains('DISTRO_FEATURES', 'selinux', 'file://mncos-file_contexts.subs_dist', '', d)}"
do_install:append() {
    if ${@bb.utils.contains('DISTRO_FEATURES', 'selinux', 'true', 'false', d)}; then
        cat ${WORKDIR}/mncos-file_contexts.subs_dist >> ${D}${sysconfdir}/selinux/${POLICY_NAME}/contexts/files/file_contexts.subs_dist
    fi
}
