MNCOS_IMAGE_ROLE ??= "unspecified"
MNCOS_IMAGE_LABEL ??= "MNCOS image"

ROOTFS_POSTPROCESS_COMMAND:append = " mncos_write_image_identity;"

mncos_write_image_identity() {
    install -d "${IMAGE_ROOTFS}${sysconfdir}"

    printf 'IMAGE_ROLE="%s"\nIMAGE_LABEL="%s"\nIMAGE_RECIPE="%s"\nMACHINE="%s"\nDISTRO_VERSION="%s"\n' \
        "${MNCOS_IMAGE_ROLE}" \
        "${MNCOS_IMAGE_LABEL}" \
        "${PN}" \
        "${MACHINE}" \
        "${DISTRO_VERSION}" \
        > "${IMAGE_ROOTFS}${sysconfdir}/mncos-image-info"

    printf '\n*** %s ***\nImage role: %s\nImage recipe: %s\nMachine: %s\n\n' \
        "${MNCOS_IMAGE_LABEL}" \
        "${MNCOS_IMAGE_ROLE}" \
        "${PN}" \
        "${MACHINE}" \
        >> "${IMAGE_ROOTFS}${sysconfdir}/issue"

    printf '*** %s ***\nImage role: %s\nImage recipe: %s\nMachine: %s\n' \
        "${MNCOS_IMAGE_LABEL}" \
        "${MNCOS_IMAGE_ROLE}" \
        "${PN}" \
        "${MACHINE}" \
        > "${IMAGE_ROOTFS}${sysconfdir}/motd"
}
