inherit deploy

MNC_ARTIFACT_TOOL ?= "${MNC_ARTIFACT_LAYERDIR}/scripts/mnc-artifact.py"
MNC_ARTIFACT_SCHEMA_FILE ?= "${MNC_ARTIFACT_LAYERDIR}/schema/mnc-station-artifact-v2.schema.json"
MNC_ARTIFACT_STAGING_DIR ?= "${WORKDIR}/mnc-station-artifact"
MNC_ARTIFACT_EXPORT_DIR ?= "${TOPDIR}/export/provision-image"

MNC_ARTIFACT_NAME ?= "${PN}"
MNC_ARTIFACT_VENDOR ?= ""
MNC_ARTIFACT_OPERATION ?= ""
MNC_ARTIFACT_PRODUCT ?= ""
MNC_ARTIFACT_MACHINE ?= "${MACHINE}"
MNC_ARTIFACT_VERSION ?= "${DISTRO_VERSION}"
MNC_ARTIFACT_SOURCE_EPOCH ?= "${@d.getVar('SOURCE_DATE_EPOCH') or '0'}"
MNC_ARTIFACT_BUILD_ID ?= "${@time.strftime('%Y%m%d%H%M%S', time.gmtime(int(d.getVar('MNC_ARTIFACT_SOURCE_EPOCH') or '0')))}"
MNC_ARTIFACT_CREATED_UTC ?= "${@time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(int(d.getVar('MNC_ARTIFACT_SOURCE_EPOCH') or '0')))}"

MNC_ARTIFACT_EXECUTOR_TYPE ?= ""
MNC_ARTIFACT_ENTRYPOINT ?= ""
MNC_ARTIFACT_TFTP_ROOT ?= ""

MNC_ARTIFACT_LATEST_NAME ?= "${MNC_ARTIFACT_NAME}.tar.gz"

mnc_artifact_populate() {
    bbfatal "The recipe inheriting mnc-artifact must implement mnc_artifact_populate"
}

# Vendor classes may replace these resolved shell values after their source
# artifact exists. The default remains deterministic for ordinary recipes.
mnc_artifact_resolve_metadata() {
    :
}

do_deploy() {
    set -eu

    rm -rf "${MNC_ARTIFACT_STAGING_DIR}"
    install -d "${MNC_ARTIFACT_STAGING_DIR}" "${DEPLOYDIR}"
    mnc_artifact_populate

    artifact_epoch="${MNC_ARTIFACT_SOURCE_EPOCH}"
    artifact_build_id="${MNC_ARTIFACT_BUILD_ID}"
    artifact_created_utc="${MNC_ARTIFACT_CREATED_UTC}"
    mnc_artifact_resolve_metadata
    artifact_archive_name="${MNC_ARTIFACT_NAME}-${artifact_build_id}.tar.gz"

    set -- create \
        --payload-dir "${MNC_ARTIFACT_STAGING_DIR}" \
        --output "${DEPLOYDIR}/${artifact_archive_name}" \
        --epoch "${artifact_epoch}" \
        --name "${MNC_ARTIFACT_NAME}" \
        --vendor "${MNC_ARTIFACT_VENDOR}" \
        --operation "${MNC_ARTIFACT_OPERATION}" \
        --product "${MNC_ARTIFACT_PRODUCT}" \
        --machine "${MNC_ARTIFACT_MACHINE}" \
        --version "${MNC_ARTIFACT_VERSION}" \
        --build-id "${artifact_build_id}" \
        --created-utc "${artifact_created_utc}" \
        --executor-type "${MNC_ARTIFACT_EXECUTOR_TYPE}" \
        --entrypoint "${MNC_ARTIFACT_ENTRYPOINT}"

    if [ -n "${MNC_ARTIFACT_TFTP_ROOT}" ]; then
        set -- "$@" --tftp-root "${MNC_ARTIFACT_TFTP_ROOT}"
    fi

    "${PYTHON}" "${MNC_ARTIFACT_TOOL}" "$@"
    "${PYTHON}" "${MNC_ARTIFACT_TOOL}" verify \
        --archive "${DEPLOYDIR}/${artifact_archive_name}" >/dev/null

    ln -sfn "${artifact_archive_name}" \
        "${DEPLOYDIR}/${MNC_ARTIFACT_LATEST_NAME}"
}

do_deploy[file-checksums] += " \
    ${MNC_ARTIFACT_TOOL}:True \
    ${MNC_ARTIFACT_SCHEMA_FILE}:True \
"

addtask deploy after do_compile before do_build

# Keep a developer-facing copy outside tmp/deploy, matching the existing
# export/tftpboot workflow.  Resolve the stable deploy link first so both the
# versioned archive and a stable link can be reproduced when do_deploy comes
# from sstate.
do_export_provision_image() {
    set -eu

    source_latest="${DEPLOY_DIR_IMAGE}/${MNC_ARTIFACT_LATEST_NAME}"
    if [ ! -e "${source_latest}" ]; then
        bbfatal "Provisioning image deploy output is missing: ${source_latest}"
    fi

    source_archive="$(readlink -f "${source_latest}")"
    deploy_dir="$(readlink -f "${DEPLOY_DIR_IMAGE}")"
    case "${source_archive}" in
        "${deploy_dir}"/*) ;;
        *) bbfatal "Provisioning image deploy link escapes ${DEPLOY_DIR_IMAGE}: ${source_latest}" ;;
    esac

    archive_name="$(basename "${source_archive}")"
    if [ "${archive_name}" = "${MNC_ARTIFACT_LATEST_NAME}" ]; then
        bbfatal "Versioned provisioning image name collides with stable name: ${archive_name}"
    fi

    install -d "${MNC_ARTIFACT_EXPORT_DIR}"
    install -m 0644 "${source_archive}" \
        "${MNC_ARTIFACT_EXPORT_DIR}/${archive_name}"
    ln -sfn "${archive_name}" \
        "${MNC_ARTIFACT_EXPORT_DIR}/${MNC_ARTIFACT_LATEST_NAME}"
}

do_export_provision_image[nostamp] = "1"
PSEUDO_IGNORE_PATHS .= ",${MNC_ARTIFACT_EXPORT_DIR}"
addtask export_provision_image after do_deploy before do_build
