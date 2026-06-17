SUMMARY = "webengine — Boost.Beast API backend that owns its nginx reverse proxy"
DESCRIPTION = "Fetches the Nginx front/back-end demo, builds the C++ backend, and \
installs it with the on-target nginx config + web root under /opt/monutchee/mncos. \
The backend owns nginx's lifecycle (Option A); the distro nginx.service stays \
disabled (see the nginx bbappend). A per-device self-signed TLS cert is generated \
on first boot."
HOMEPAGE = "https://github.com/lesterlo/Nginx-front-back-end-demo"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://../LICENSE;md5=bea232fc293d2909c632f6cdc3edc644"

# Source switch: "cloud" (GitHub, default) or "local" (a git checkout).
# Flip in local.conf:  WEBENGINE_SRC = "local"
# Both build COMMITTED state: for cloud the branch must be pushed; for local,
# commit your changes (use devtool/externalsrc if you need the live work tree).
WEBENGINE_SRC ?= "cloud"
WEBENGINE_GIT_BRANCH ?= "main"
WEBENGINE_LOCAL_DIR  ?= "/opt/monutchee/test/Nginx-front-back-end-interaction-demo"

WEBENGINE_REPO_cloud = "git://github.com/lesterlo/Nginx-front-back-end-demo.git;protocol=https;branch=${WEBENGINE_GIT_BRANCH};name=webengine;destsuffix=git"
WEBENGINE_REPO_local = "git://${WEBENGINE_LOCAL_DIR};protocol=file;branch=${WEBENGINE_GIT_BRANCH};name=webengine;destsuffix=git"

SRC_URI = "${@d.getVar('WEBENGINE_REPO_' + (d.getVar('WEBENGINE_SRC') or 'cloud'))}"

# Pinned to the feature/new_glaze_install_method tip for a reproducible build. Bump
# this (or point it at a release tag's commit) when you cut a stable version.
SRCREV_webengine = "ec36c6cc0fe4bd13ed513855bd9f989b51a27b27"
PV = "1.0+git${SRCPV}"

# The CMake project lives in the repo's backend/ subdirectory.
S = "${WORKDIR}/git/backend"

DEPENDS = "boost glaze openssl"
RDEPENDS:${PN} = "worker-user nginx openssl-bin"

inherit cmake systemd

SYSTEMD_SERVICE:${PN} = "beast-backend.service"
# Start the backend at boot; it brings nginx up.
SYSTEMD_AUTO_ENABLE = "enable"

EXTRA_OECMAKE = "-DCMAKE_BUILD_TYPE=Release"

WEBENGINE_INSTALL_ROOT ?= "/opt/monutchee/mncos"

do_install() {
    # 1. The backend binary (CMake emits it at the build root). The repo's unit
    #    expects /usr/local/bin/backend; install to ${bindir} and repoint it.
    install -d ${D}${bindir}
    install -m 0755 ${B}/backend ${D}${bindir}/backend

    # 2. systemd unit (from the repo), repointed at ${bindir}.
    install -d ${D}${systemd_system_unitdir}
    install -m 0644 ${WORKDIR}/git/systemd/beast-backend.service \
                    ${D}${systemd_system_unitdir}/beast-backend.service
    sed -i 's,/usr/local/bin/backend,${bindir}/backend,' \
        ${D}${systemd_system_unitdir}/beast-backend.service

    # 3. nginx config + web root under WEBENGINE_INSTALL_ROOT, plus the TLS runtime dir.
    install -d ${D}${WEBENGINE_INSTALL_ROOT}/conf/webserver/www
    install -d ${D}${WEBENGINE_INSTALL_ROOT}/runtime/webserver/ssl
    install -m 0644 ${WORKDIR}/git/nginx/nginx.target.conf \
                    ${D}${WEBENGINE_INSTALL_ROOT}/conf/webserver/nginx.conf
    cp -R ${WORKDIR}/git/www/. ${D}${WEBENGINE_INSTALL_ROOT}/conf/webserver/www/

    sed -i 's,/opt/monutchee/msys,${WEBENGINE_INSTALL_ROOT},g' \
        ${D}${systemd_system_unitdir}/beast-backend.service \
        ${D}${WEBENGINE_INSTALL_ROOT}/conf/webserver/nginx.conf
}

FILES:${PN} += " \
    ${bindir}/backend \
    ${WEBENGINE_INSTALL_ROOT}/conf/webserver \
    ${WEBENGINE_INSTALL_ROOT}/runtime/webserver \
"

# Ownership + per-device TLS cert must happen on the device (where `worker` exists
# and we want a unique key), so run on first boot, not at rootfs-assembly time.
pkg_postinst_ontarget:${PN}() {
    set -e
    SSLDIR=${WEBENGINE_INSTALL_ROOT}/runtime/webserver/ssl
    if [ ! -f "$SSLDIR/server.crt" ]; then
        openssl req -x509 -nodes -days 3650 -newkey rsa:2048 \
            -keyout "$SSLDIR/server.key" -out "$SSLDIR/server.crt" \
            -subj "/CN=mncos" -addext "subjectAltName=DNS:mncos"
    fi
    # worker owns the config dir (so NginxController can write listen.conf there)
    # and the ssl dir + key (so the worker-run nginx can read the cert).
    chown worker:worker ${WEBENGINE_INSTALL_ROOT}/conf/webserver
    chown -R worker:worker "$SSLDIR"
    chmod 600 "$SSLDIR/server.key"
}
