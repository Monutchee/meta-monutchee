FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append = " ${@bb.utils.contains('DISTRO_FEATURES', 'selinux', 'file://mncos-file_contexts.subs_dist file://mncos-file_contexts.local', '', d)}"
do_install:append() {
    if ${@bb.utils.contains('DISTRO_FEATURES', 'selinux', 'true', 'false', d)}; then
        cat ${WORKDIR}/mncos-file_contexts.subs_dist >> ${D}${sysconfdir}/selinux/${POLICY_NAME}/contexts/files/file_contexts.subs_dist
        install -d ${D}${sysconfdir}/selinux/${POLICY_NAME}/contexts/files \
                   ${D}${POLICY_STORE_ROOT}/${POLICY_NAME}/active
        cat ${WORKDIR}/mncos-file_contexts.local >> ${D}${sysconfdir}/selinux/${POLICY_NAME}/contexts/files/file_contexts.local
        cat ${WORKDIR}/mncos-file_contexts.local >> ${D}${POLICY_STORE_ROOT}/${POLICY_NAME}/active/file_contexts.local
    fi
}
