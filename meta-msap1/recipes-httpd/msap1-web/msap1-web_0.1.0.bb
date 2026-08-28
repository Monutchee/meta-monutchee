SUMMARY = "MSAP1 React/Vite web interface"
DESCRIPTION = "Builds the static MSAP1 ADC diagnostics frontend for nginx."
HOMEPAGE = "https://github.com/Monutchee/MSAP1_WEB"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=86d3f3a95c324c9479bd8986968f4327"

# Source switch:
#   cloud      - fetch the selected branch from GitHub (default)
#   local      - fetch the committed state of a local Git checkout
#   local_inst - build the local working tree directly, including uncommitted edits
MSAP1_WEB_SRC ?= "cloud"
MSAP1_WEB_GIT_BRANCH ?= "main"
MSAP1_WEB_LOCAL_DIR ?= "${MSAP1_LAYERDIR}/../../../../applications/MSAP1_WEB"

MSAP1_WEB_REPO_cloud = "git://github.com/Monutchee/MSAP1_WEB.git;protocol=https;branch=${MSAP1_WEB_GIT_BRANCH};name=msap1-web;destsuffix=git"
MSAP1_WEB_REPO_local = "git://${MSAP1_WEB_LOCAL_DIR};protocol=file;branch=${MSAP1_WEB_GIT_BRANCH};name=msap1-web;destsuffix=git"
MSAP1_WEB_REPO_local_inst = ""

# npmsw needs a lockfile at fetch time, before a cloud/local Git checkout has
# been unpacked. Those reproducible modes therefore use the layer-pinned copy.
# local_inst already has its live checkout, so consume that lockfile directly:
# a dependency edit then changes the fetch and configure task signatures
# without requiring a second manual copy merely to build the working tree.
MSAP1_WEB_LAYER_LOCKFILE = "${THISDIR}/files/npm-shrinkwrap.json"
MSAP1_WEB_LOCAL_INST_LOCKFILE = "${MSAP1_WEB_LOCAL_DIR}/npm-shrinkwrap.json"
MSAP1_WEB_LOCKFILE = "${@d.getVar('MSAP1_WEB_LOCAL_INST_LOCKFILE') if d.getVar('MSAP1_WEB_SRC') == 'local_inst' else d.getVar('MSAP1_WEB_LAYER_LOCKFILE')}"
SRC_URI = "${@d.getVar('MSAP1_WEB_REPO_' + (d.getVar('MSAP1_WEB_SRC') or 'cloud'))} \
           npmsw://${MSAP1_WEB_LOCKFILE};dev=1"
SRCREV_msap1-web ?= "${AUTOREV}"

# BitBake's generic fetch checksum list tracks only file:// entries. npmsw
# reads this selected lockfile to expand the npm dependency URLs, so include it
# explicitly in both signatures. This guarantees that a local_inst dependency
# edit refetches its tarballs and regenerates npm's offline package cache.
do_fetch[file-checksums] += " ${MSAP1_WEB_LOCKFILE}:True"
do_configure[file-checksums] += " ${MSAP1_WEB_LOCKFILE}:True"

PV = "${@'0.1.0+local' if d.getVar('MSAP1_WEB_SRC') == 'local_inst' else '0.1.0+git' + (d.getVar('SRCPV') or '')}"
S = "${WORKDIR}/git"

inherit npm externalsrc

# This recipe runs Vite on the build host and installs only static output.
NPM_INSTALL_DEV = "1"
NPM_ARCH = "${@map_nodejs_arch(d.getVar('BUILD_ARCH'), d)}"
RDEPENDS:${PN}:remove = "nodejs"

EXTERNALSRC = "${@d.getVar('MSAP1_WEB_LOCAL_DIR') if d.getVar('MSAP1_WEB_SRC') == 'local_inst' else ''}"
EXTERNALSRC_BUILD = "${WORKDIR}/msap1-web-build"

python do_configure:prepend() {
    import filecmp
    import os

    source_lock = os.path.join(d.getVar('S'), 'npm-shrinkwrap.json')
    fetch_lock = d.getVar('MSAP1_WEB_LOCKFILE')
    if not os.path.exists(source_lock):
        bb.fatal('MSAP1_WEB does not contain npm-shrinkwrap.json')
    if not filecmp.cmp(source_lock, fetch_lock, shallow=False):
        bb.fatal('MSAP1_WEB npm-shrinkwrap.json differs from the selected npmsw lockfile; update the meta-msap1 copy for cloud/local builds')
}

python do_compile:append() {
    import os
    import shutil
    import subprocess

    env = os.environ.copy()
    env['HOME'] = d.getVar('WORKDIR')
    env['PATH'] = os.path.join(d.getVar('NPM_BUILD'), 'bin') + ':' + d.getVar('PATH')
    package_dir = os.path.join(
        d.getVar('NPM_BUILD'), 'lib', 'node_modules', 'msap1-web')

    # npm_do_configure prepares an offline package and dependency cache. In
    # local_inst mode that package can be older than the live external tree
    # because externalsrc tracks source changes for do_compile, not for npm's
    # packing step. Overlay only the frontend inputs immediately before Vite
    # runs so uncommitted additions, edits, and deletions are always compiled.
    if d.getVar('MSAP1_WEB_SRC') == 'local_inst':
        source_dir = d.getVar('S')
        for directory in ('src', 'public'):
            source = os.path.join(source_dir, directory)
            destination = os.path.join(package_dir, directory)
            if os.path.lexists(destination):
                shutil.rmtree(destination)
            if os.path.isdir(source):
                shutil.copytree(source, destination)

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
            source = os.path.join(source_dir, name)
            destination = os.path.join(package_dir, name)
            if os.path.isfile(source):
                shutil.copy2(source, destination)
            elif os.path.lexists(destination):
                os.unlink(destination)

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
