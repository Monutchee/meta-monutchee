#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Wrap a product root in SquashFS and a minimal BusyBox initramfs tree."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess


def build(root, output, readelf, epoch):
    output.mkdir(parents=True)
    for directory in ('bin', 'usr/lib', 'dev', 'proc', 'sys', 'run', 'newroot'):
        (output / directory).mkdir(parents=True, exist_ok=True)
    (output / 'lib').symlink_to('usr/lib')
    (output / 'lib64').symlink_to('usr/lib')
    (output / 'usr/lib64').symlink_to('lib')

    def source(path):
        for _ in range(16):
            candidate = root / path.lstrip('/')
            if not candidate.is_symlink():
                if not candidate.is_file():
                    raise RuntimeError('missing rootfs ELF dependency: ' + path)
                return candidate
            link = os.readlink(candidate)
            path = os.path.normpath(link if link.startswith('/') else str(Path(path).parent / link))
        raise RuntimeError('symlink loop: ' + path)

    copied = set()

    def elf(path, destination):
        if destination in copied:
            return
        copied.add(destination)
        src = source(path)
        dest = output / destination.lstrip('/')
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest)
        data = subprocess.check_output([readelf, '-l', '-d', str(src)], text=True)
        interpreter = re.search(r'Requesting program interpreter: ([^\]]+)', data)
        if interpreter:
            elf(interpreter[1], interpreter[1])
        for needed in re.findall(r'Shared library: \[([^\]]+)\]', data):
            for prefix in ('/usr/lib/', '/lib/', '/lib64/', '/usr/lib64/'):
                try:
                    source(prefix + needed)
                except RuntimeError:
                    continue
                elf(prefix + needed, '/usr/lib/' + needed)
                break
            else:
                raise RuntimeError('unresolved BusyBox dependency: ' + needed)

    elf('/bin/busybox', '/bin/busybox')
    for applet in ('sh', 'mount', 'mv', 'switch_root'):
        (output / 'bin' / applet).symlink_to('busybox')
    shutil.copy2(root / 'usr/share/mnc/storage/ram-init', output / 'init')
    env = os.environ.copy()
    env.pop('SOURCE_DATE_EPOCH', None)  # Explicit timestamp flags below own reproducibility.
    subprocess.run(['mksquashfs', str(root), str(output / 'rootfs.squashfs'),
                    '-noappend', '-comp', 'gzip', '-processors', '2', '-mkfs-time', str(epoch),
                    '-all-time', str(epoch), '-wildcards', '-e', 'boot/*'], check=True, env=env)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--readelf', default='readelf')
    parser.add_argument('--epoch', type=int, required=True)
    args = parser.parse_args()
    build(args.root, args.output, args.readelf, args.epoch)
