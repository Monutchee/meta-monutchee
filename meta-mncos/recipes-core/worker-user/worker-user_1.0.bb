SUMMARY = "System 'worker' user and group for the web/backend stack"
DESCRIPTION = "Creates the unprivileged, no-login system user 'worker' and its \
matching group. The reverse proxy (nginx/lighttpd) and the C++ backend both run \
as this single shared user, so the backend can start, signal and reload the \
proxy without root. Provided at the OS level so the account exists independently \
of any one application package."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

inherit useradd

# This recipe ships no files; it exists solely to create the account.
USERADD_PACKAGES = "${PN}"
ALLOW_EMPTY:${PN} = "1"

# System account: no login shell, no home directory. --user-group also creates
# the matching 'worker' group. A consuming service unit (e.g. the backend in
# meta-zuboard) adds whatever else it needs at runtime — supplementary groups
# such as 'dialout' for the UART, and CAP_NET_BIND_SERVICE for ports < 1024.
USERADD_PARAM:${PN} = "--system --no-create-home --home-dir /nonexistent \
                       --shell /usr/sbin/nologin --user-group worker"

do_install() {
    :
}
