# Generic dfx-mgr firmware packaging for Xilinx PL + RPU demo designs.

inherit dfx_user_dts systemd

XILINX_DFX_APP_NAME ?= "${PN}"
XILINX_DFX_AUTOLOAD ?= "0"
XILINX_DFX_RPU_BASE ?= "${XILINX_DFX_APP_NAME}-rpu"
XILINX_DFX_RPU_LOAD_NAMES ?= "R5c0 R5c1"
XILINX_DFX_RPU_ELFS ?= ""
XILINX_DFX_SERVICE_AFTER ?= "dfx-mgr.service dfx-mgr-fw-load.service"
XILINX_DFX_SHELL_TYPE ?= "PL_FLAT"
XILINX_DFX_ARTIFACT_DIR ?= "${TOPDIR}/../../runtime-generated/bin_file"

# Shared CI/runtime artifact location for locally generated RPU firmware.
FILESEXTRAPATHS:prepend := "${XILINX_DFX_ARTIFACT_DIR}:"

FW_INSTALL_DIR = "${XILINX_DFX_APP_NAME}"
COMPATIBLE_MACHINE = ".*"
PACKAGE_ARCH = "${MACHINE_ARCH}"

do_install:append() {
    fw_path="${D}${nonarch_base_libdir}/firmware/xilinx/${XILINX_DFX_APP_NAME}"
    install -d "${fw_path}"

    printf '{\n    "shell_type": "%s",\n    "num_pl_slots": 0,\n    "num_aie_slots": 0\n}\n' \
        "${XILINX_DFX_SHELL_TYPE}" > "${fw_path}/shell.json"

    if [ "${XILINX_DFX_AUTOLOAD}" = "1" ]; then
        install -d "${D}${sysconfdir}/dfx-mgrd"
        printf '%s\n' "${XILINX_DFX_APP_NAME}" > "${D}${sysconfdir}/dfx-mgrd/default_firmware"
    fi

    for entry in ${XILINX_DFX_RPU_ELFS}; do
        slot="${entry%%:*}"
        elf="${entry#*:}"
        src="${WORKDIR}/${elf}"
        if [ ! -e "${src}" ]; then
            src="${S}/${elf}"
        fi
        if [ ! -e "${src}" ]; then
            bbfatal "Missing RPU firmware ELF: ${elf}"
        fi
        install -Dm 0644 "${src}" \
            "${D}${nonarch_base_libdir}/firmware/xilinx/${XILINX_DFX_RPU_BASE}/rpu/${slot}/${elf}"
    done

    install -d "${D}${bindir}" "${D}${systemd_system_unitdir}"
    cat > "${D}${bindir}/${PN}-rpu-load" <<EOF
#!/bin/sh
set -u

RPU_FW="${XILINX_DFX_RPU_LOAD_NAMES}"
rc=0

for fw in \${RPU_FW}; do
	echo "${PN}-rpu-load: loading \${fw}"
	if ! dfx-mgr-client -loadByName "\${fw}"; then
		echo "${PN}-rpu-load: FAILED to load \${fw}" >&2
		rc=1
	fi
done

exit \${rc}
EOF
    chmod 0755 "${D}${bindir}/${PN}-rpu-load"

    cat > "${D}${systemd_system_unitdir}/${PN}-rpu-load.service" <<EOF
[Unit]
Description=Load ${XILINX_DFX_APP_NAME} RPU firmware via dfx-mgr
After=${XILINX_DFX_SERVICE_AFTER}
Wants=dfx-mgr.service
ConditionPathExists=/usr/bin/dfx-mgr-client

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=${bindir}/${PN}-rpu-load

[Install]
WantedBy=multi-user.target
EOF
}

do_install[vardeps] += "XILINX_DFX_APP_NAME XILINX_DFX_AUTOLOAD XILINX_DFX_RPU_BASE XILINX_DFX_RPU_LOAD_NAMES XILINX_DFX_RPU_ELFS XILINX_DFX_SERVICE_AFTER XILINX_DFX_SHELL_TYPE"

SYSTEMD_SERVICE:${PN} = "${PN}-rpu-load.service"
SYSTEMD_AUTO_ENABLE:${PN} = "${@bb.utils.contains_any('XILINX_DFX_AUTOLOAD', '1', 'enable', 'disable', d)}"

FILES:${PN} += " \
    ${sysconfdir}/dfx-mgrd/default_firmware \
    ${nonarch_base_libdir}/firmware/xilinx/${XILINX_DFX_RPU_BASE} \
    ${bindir}/${PN}-rpu-load \
    ${systemd_system_unitdir}/${PN}-rpu-load.service \
"

RDEPENDS:${PN} += "dfx-mgr"
INSANE_SKIP:${PN} += "arch ldflags textrel file-rdeps"
