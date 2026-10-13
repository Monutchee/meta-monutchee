FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI:append = " ${@bb.utils.contains('DISTRO_FEATURES', 'selinux', 'file://mncos-selinux.cfg', '', d)}"
python () {
    if "selinux" in (d.getVar("DISTRO_FEATURES") or "").split():
        d.appendVarFlag("do_configure", "postfuncs", " mncos_check_selinux_kernel")
}

python mncos_check_selinux_kernel() {
    if 'selinux' not in (d.getVar('DISTRO_FEATURES') or '').split():
        return
    import pathlib
    config = pathlib.Path(d.getVar('B'), '.config').read_text().splitlines()
    required = ('SECURITY', 'SECURITYFS', 'SECURITY_SELINUX', 'SECURITY_SELINUX_DEVELOP', 'AUDIT', 'AUDITSYSCALL', 'EXT4_FS_SECURITY', 'SQUASHFS_XATTR')
    missing = [symbol for symbol in required if 'CONFIG_' + symbol + '=y' not in config]
    lsm = next((line for line in config if line.startswith('CONFIG_LSM=')), '')
    if 'selinux' not in lsm:
        missing.append('LSM containing selinux')
    if missing:
        bb.fatal('SELinux kernel configuration is incomplete: ' + ', '.join(missing))
}
