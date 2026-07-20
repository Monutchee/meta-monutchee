SUMMARY = "MSAP1 React/Vite web interface"
DESCRIPTION = "Builds the static MSAP1 ADC diagnostics frontend for nginx."
HOMEPAGE = "https://github.com/Monutchee/MSAP1_WEB"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"

def msap1_web_local_source_hash(d):
    """Hash files npm packs during do_configure for local_inst builds."""
    if d.getVar('MSAP1_WEB_SRC') != 'local_inst':
        return ''

    import hashlib
    import os

    source_root = os.path.realpath(d.getVar('MSAP1_WEB_LOCAL_DIR'))
    if not os.path.isdir(source_root):
        return 'missing-local-source'

    tracked_files = []
    for name in (
        'package.json',
        'npm-shrinkwrap.json',
        'index.html',
        'tsconfig.json',
        'tsconfig.app.json',
        'tsconfig.node.json',
        'vite.config.ts',
        'vite.config.js',
    ):
        path = os.path.join(source_root, name)
        if os.path.isfile(path):
            tracked_files.append(path)

    for directory in ('src', 'public'):
        tree_root = os.path.join(source_root, directory)
        if not os.path.isdir(tree_root):
            continue
        for root, directories, files in os.walk(tree_root):
            directories[:] = sorted(
                entry for entry in directories if not entry.startswith('.'))
            for name in sorted(files):
                tracked_files.append(os.path.join(root, name))

    digest = hashlib.sha256()
    for path in sorted(tracked_files):
        relative_path = os.path.relpath(path, source_root)
        digest.update(relative_path.encode('utf-8'))
        digest.update(b'\0')
        with open(path, 'rb') as source_file:
            for block in iter(lambda: source_file.read(65536), b''):
                digest.update(block)
    return digest.hexdigest()

# Source switch:
#   cloud      - fetch the selected branch from GitHub (default)
#   local      - fetch the committed state of a local Git checkout
#   local_inst - build the local working tree directly, including uncommitted edits
MSAP1_WEB_SRC ?= "cloud"
MSAP1_WEB_GIT_BRANCH ?= "main"
MSAP1_WEB_LOCAL_DIR ?= "${TOPDIR}/../../MSAP1_WEB"

MSAP1_WEB_REPO_cloud = "git://github.com/Monutchee/MSAP1_WEB.git;protocol=https;branch=${MSAP1_WEB_GIT_BRANCH};name=msap1-web;destsuffix=git"
MSAP1_WEB_REPO_local = "git://${MSAP1_WEB_LOCAL_DIR};protocol=file;branch=${MSAP1_WEB_GIT_BRANCH};name=msap1-web;destsuffix=git"
MSAP1_WEB_REPO_local_inst = ""

# The layer copy is required at fetch time so BitBake can download every npm
# dependency before tasks are placed in the network-disabled build sandbox.
MSAP1_WEB_LOCKFILE = "${THISDIR}/files/npm-shrinkwrap.json"
SRC_URI = "${@d.getVar('MSAP1_WEB_REPO_' + (d.getVar('MSAP1_WEB_SRC') or 'cloud'))} \
           npmsw://${MSAP1_WEB_LOCKFILE};dev=1"
SRCREV_msap1-web ?= "${AUTOREV}"

PV = "${@'0.1.0+local' if d.getVar('MSAP1_WEB_SRC') == 'local_inst' else '0.1.0+git' + (d.getVar('SRCPV') or '')}"
S = "${WORKDIR}/git"

inherit npm externalsrc

# This recipe runs Vite on the build host and installs only static output.
NPM_INSTALL_DEV = "1"
NPM_ARCH = "${@map_nodejs_arch(d.getVar('BUILD_ARCH'), d)}"
RDEPENDS:${PN}:remove = "nodejs"

EXTERNALSRC = "${@d.getVar('MSAP1_WEB_LOCAL_DIR') if d.getVar('MSAP1_WEB_SRC') == 'local_inst' else ''}"
EXTERNALSRC_BUILD = "${WORKDIR}/msap1-web-build"

# npm_do_configure packs ${S} into ${NPM_PACKAGE}. Externalsrc normally hashes
# the live tree for do_compile only, which can leave that package stale. Make
# relevant local frontend changes invalidate do_configure as well.
MSAP1_WEB_LOCAL_SOURCE_HASH = "${@msap1_web_local_source_hash(d)}"
do_configure[vardeps] += "MSAP1_WEB_LOCAL_SOURCE_HASH"

python do_configure:prepend() {
    import filecmp
    import os

    source_lock = os.path.join(d.getVar('S'), 'npm-shrinkwrap.json')
    layer_lock = d.getVar('MSAP1_WEB_LOCKFILE')
    if not os.path.exists(source_lock):
        bb.fatal('MSAP1_WEB does not contain npm-shrinkwrap.json')
    if not filecmp.cmp(source_lock, layer_lock, shallow=False):
        bb.fatal('MSAP1_WEB npm-shrinkwrap.json differs from the meta-msap1 copy; update both together')
}

python do_compile:append() {
    import os
    import subprocess

    env = os.environ.copy()
    env['HOME'] = d.getVar('WORKDIR')
    env['PATH'] = os.path.join(d.getVar('NPM_BUILD'), 'bin') + ':' + d.getVar('PATH')
    package_dir = os.path.join(
        d.getVar('NPM_BUILD'), 'lib', 'node_modules', 'msap1-web')
    subprocess.check_call(
        ['npm', '--offline', '--no-audit', '--no-fund', 'run', 'build'],
        cwd=package_dir, env=env)
}

do_install() {
    install -d ${D}${datadir}/msap1-web
    cp --no-preserve=ownership -R \
        ${NPM_BUILD}/lib/node_modules/msap1-web/dist/. \
        ${D}${datadir}/msap1-web/
}

FILES:${PN} = "${datadir}/msap1-web"
