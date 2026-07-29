MNCOS_IMAGE_ROLE ??= "unspecified"
MNCOS_IMAGE_LABEL ??= "MNCOS image"
MNCOS_BUILD_TIME ??= "${@time.strftime('%Y-%m-%d %H:%M:%S UTC', time.strptime(d.getVar('DATETIME'), '%Y%m%d%H%M%S'))}"
MNCOS_BUILD_HASH ??= "${BB_TASKHASH}"

ROOTFS_POSTPROCESS_COMMAND:append = " mncos_write_image_identity;"

mncos_write_image_identity() {
    build_hash_short="$(printf '%s' "${MNCOS_BUILD_HASH}" | cut -c 1-6)"
    install -d "${IMAGE_ROOTFS}${sysconfdir}"

    printf 'IMAGE_ROLE="%s"\nIMAGE_LABEL="%s"\nIMAGE_RECIPE="%s"\nMACHINE="%s"\nDISTRO_VERSION="%s"\nBUILD_TIME="%s"\nBUILD_HASH="%s"\nBUILD_HASH_SHORT="%s"\n' \
        "${MNCOS_IMAGE_ROLE}" \
        "${MNCOS_IMAGE_LABEL}" \
        "${PN}" \
        "${MACHINE}" \
        "${DISTRO_VERSION}" \
        "${MNCOS_BUILD_TIME}" \
        "${MNCOS_BUILD_HASH}" \
        "$build_hash_short" \
        > "${IMAGE_ROOTFS}${sysconfdir}/mncos-image-info"

    printf '\n*** %s ***\nImage role: %s\nImage recipe: %s\nMachine: %s\nBuild time: %s\nBuild hash: %s\n\n' \
        "${MNCOS_IMAGE_LABEL}" \
        "${MNCOS_IMAGE_ROLE}" \
        "${PN}" \
        "${MACHINE}" \
        "${MNCOS_BUILD_TIME}" \
        "$build_hash_short" \
        >> "${IMAGE_ROOTFS}${sysconfdir}/issue"

    printf '*** %s ***\nImage role: %s\nImage recipe: %s\nMachine: %s\nBuild time: %s\nBuild hash: %s\n' \
        "${MNCOS_IMAGE_LABEL}" \
        "${MNCOS_IMAGE_ROLE}" \
        "${PN}" \
        "${MACHINE}" \
        "${MNCOS_BUILD_TIME}" \
        "$build_hash_short" \
        > "${IMAGE_ROOTFS}${sysconfdir}/motd"
}
