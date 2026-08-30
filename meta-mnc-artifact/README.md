# meta-mnc-artifact

Generic, vendor-neutral packaging support for artifacts consumed by a
Monutchee Provisioning Station.

Recipes inherit `mnc-artifact`, provide the public manifest metadata, and
implement `mnc_artifact_populate` to place regular payload files below
`${MNC_ARTIFACT_STAGING_DIR}`. The class writes a deterministic `.tar.gz`
archive through the standard Yocto deploy task and verifies it before it enters
`${DEPLOY_DIR_IMAGE}`. A separate, unstamped export task copies the versioned
archive and recreates its stable link under
`${TOPDIR}/export/provision-image`; recipes may override that location with
`MNC_ARTIFACT_EXPORT_DIR`.

The public format is defined by
`schema/mnc-station-artifact-v2.schema.json`. Paths inside an archive are POSIX
relative paths. Links, devices, duplicate members, traversal paths, and files
not covered by `manifest.json` are rejected.

The same schema is available to target-side tools through the optional
`mnc-station-artifact-schema` package.

Required class variables are:

- `MNC_ARTIFACT_NAME`
- `MNC_ARTIFACT_VENDOR`
- `MNC_ARTIFACT_OPERATION`
- `MNC_ARTIFACT_PRODUCT`
- `MNC_ARTIFACT_EXECUTOR_TYPE`
- `MNC_ARTIFACT_ENTRYPOINT`

`MNC_ARTIFACT_TFTP_ROOT` is optional for non-TFTP executors. Vendor integration
layers should set executor policy and populate the payload; product layers
should set product identity and source-image dependencies.

Artifacts are unsigned at this stage. `manifest.sig` is reserved for a future
protected release-signing pipeline and must not be populated by recipes yet.
