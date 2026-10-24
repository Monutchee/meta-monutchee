# Azure IoT C SDK

`azure-iot-sdk-c` supplies static, position-independent IoT Hub and Device
Provisioning Service clients using MQTT and OpenSSL. X.509 and symmetric-key
provisioning are enabled. Samples, tests, HTTP, AMQP, service clients, TPM and
RIoT are excluded. BitBake fetches the SDK and its seven production submodules;
CMake never fetches source code.

Consumers add `azure-iot-sdk-c` to `DEPENDS` and discover the installed headers
and archives with their normal cross-compilation sysroot. Upstream's bundled
dependency build does not export IoT Hub/DPS CMake package targets. Applications
that statically incorporate this SDK should retain a runtime dependency on
`azure-iot-sdk-c-licenses` to ship the complete upstream notices. Development
SDKs obtain the archives through the development package dependency.

To upgrade, update `AZURE_IOT_SDK_C_TAG`, `SRCREV_sdk`, `PV` (the SDK version),
and every production submodule `SRCREV` using that tag's Git tree. Review the
license checksums and the feature options. The unpack task rejects a tag or
submodule revision that disagrees with the pinned SDK tree. Builds fetch by
immutable commit, so a tag change alone cannot silently change the source.

Validate with `bitbake azure-iot-sdk-c` in the consuming product's configured
Yocto environment, then rebuild its consumers. This checks fetching, license
checksums, cross-compilation, installation, sysroot staging and package QA.
