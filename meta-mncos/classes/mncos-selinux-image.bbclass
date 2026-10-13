# Optional: products without meta-selinux must still parse normally.
inherit ${@bb.utils.contains('DISTRO_FEATURES', 'selinux', 'selinux-image', '', d)}

# Override upstream's silent /.autorelabel fallback: read-only roots cannot
# recover missing build labels on the target.
selinux_set_labels() {
    test -f ${IMAGE_ROOTFS}${sysconfdir}/selinux/config || bbfatal "SELinux image config missing"
    case '${DEFAULT_ENFORCING}' in permissive|enforcing) ;; *) bbfatal "Invalid SELinux image mode" ;; esac
    grep -qx 'SELINUX=${DEFAULT_ENFORCING}' ${IMAGE_ROOTFS}${sysconfdir}/selinux/config || bbfatal "SELinux config differs from selected image mode"
    POL_TYPE=$(sed -n 's/^SELINUXTYPE=//p' ${IMAGE_ROOTFS}${sysconfdir}/selinux/config)
    test -n "$POL_TYPE" || bbfatal "SELinux policy type missing"
    setfiles -m -r ${IMAGE_ROOTFS} ${IMAGE_ROOTFS}${sysconfdir}/selinux/$POL_TYPE/contexts/files/file_contexts ${IMAGE_ROOTFS} || bbfatal "SELinux rootfs labeling failed"
    test ! -e ${IMAGE_ROOTFS}/.autorelabel || bbfatal "Read-only SELinux image must not request first boot autorelabel"
}
