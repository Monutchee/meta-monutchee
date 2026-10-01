# SPDX-License-Identifier: GPL-3.0-only
import copy
import importlib.machinery
import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'recipes-core/mnc-storage/files/mnc-storage'
loader = importlib.machinery.SourceFileLoader('storage', str(SOURCE))
spec = importlib.util.spec_from_loader(loader.name, loader)
storage = importlib.util.module_from_spec(spec)
loader.exec_module(storage)


def policy():
    return dict(schema_version=1, target='test', disk='/dev/sda', state_tmpfs_mib=32,
                reset_journal='mnc-system/state.json', settings='msa/settings', product_info='productInfo.json',
                legacy_settings='/data/mnc/settings', legacy_product_info='/data/productInfo.json',
                quiesce_units=['settings.service'], roles={r: dict(label='msa-' + r, mount='/' + r,
                size_mib={'factory': 16, 'state': 256, 'data': 0}[r], readonly=r == 'factory')
                for r in storage.ROLES})


def disk():
    return dict(name='/dev/sda', type='disk', size=32 * 1024**3, mountpoints=[], children=[
        dict(name='/dev/sda1', type='part', pkname='/dev/sda', fstype='ext4', label='msa-data', mountpoints=['/data'])])


class PolicyTests(unittest.TestCase):
    def test_standard_layout(self):
        p = storage.validate(policy())
        self.assertEqual(storage.resolve(p, [disk()]), {'data': '/dev/sda1'})
        self.assertIn('size=256MiB', storage.partition_script(p))
        self.assertEqual(storage.partition_path('/dev/mmcblk0', 3), '/dev/mmcblk0p3')

    def test_emmc_only_changes_policy(self):
        p = policy()
        p['disk'] = '/dev/mmcblk0'
        d = disk()
        d['name'] = '/dev/mmcblk0'
        d['children'][0].update(name='/dev/mmcblk0p3', pkname='/dev/mmcblk0')
        self.assertEqual(storage.resolve(storage.validate(p), [d]), {'data': '/dev/mmcblk0p3'})

    def test_no_media(self):
        self.assertEqual(storage.resolve(policy(), []), {})

    def test_wrong_disk_duplicate_label_and_wrong_filesystem(self):
        for change in ('duplicate', 'disk', 'filesystem'):
            d = disk()
            nodes = [d]
            if change == 'duplicate':
                nodes.append(copy.deepcopy(d))
            elif change == 'disk':
                d['children'][0]['pkname'] = '/dev/sdb'
            else:
                d['children'][0]['fstype'] = 'vfat'
            with self.subTest(change=change), self.assertRaises(RuntimeError):
                storage.resolve(policy(), nodes)

    def test_reformat_guards(self):
        storage.disk_guard(policy(), '/dev/sda', [disk()], 'rootfs')
        for root in ('ext4', 'overlay', 'nfs'):
            with self.assertRaises(RuntimeError):
                storage.disk_guard(policy(), '/dev/sda', [disk()], root)
        for mount in ('/', '/boot', '[SWAP]', '/run/other', '/data/child'):
            d = disk()
            d['children'][0]['mountpoints'] = [mount]
            with self.subTest(mount=mount), self.assertRaises(RuntimeError):
                storage.disk_guard(policy(), '/dev/sda', [d], 'rootfs')
        with self.assertRaises(RuntimeError):
            storage.disk_guard(policy(), '/dev/sdb', [disk()], 'rootfs')

    def test_bad_policy_paths(self):
        for path in ('../etc', '/etc', 'x/../../etc', 'x//y'):
            p = policy()
            p['settings'] = path
            with self.assertRaises(RuntimeError):
                storage.validate(p)


class ArchiveTests(unittest.TestCase):
    def archive(self, path, extra=None, corrupt=False):
        files = {'settings/active.json': b'{}', 'productInfo.json': b'{"serial_number":"test"}'}
        manifest = dict(schema_version=1, target='test', sha256={
            name: storage.hashlib.sha256(value).hexdigest() for name, value in files.items()})
        if corrupt:
            files['settings/active.json'] = b'{"changed":true}'
        files['manifest.json'] = json.dumps(manifest).encode()
        with tarfile.open(path, 'w') as t:
            for name, value in files.items():
                m = tarfile.TarInfo(name)
                m.size = len(value)
                t.addfile(m, io.BytesIO(value))
            if extra:
                t.addfile(extra)

    def test_archive_integrity_and_traversal(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'backup.tar'
            self.archive(path)
            self.assertEqual(storage.verify_archive(path)['target'], 'test')
            self.archive(path, corrupt=True)
            with self.assertRaises(RuntimeError):
                storage.verify_archive(path)
            for name in ('../etc/shadow', '/etc/shadow', 'settings/../../escape', 'settings/active.json', 'settings'):
                self.archive(path, tarfile.TarInfo(name))
                with self.subTest(name=name), self.assertRaises(RuntimeError):
                    storage.verify_archive(path)

    def test_links_and_devices_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'backup.tar'
            for kind in (tarfile.SYMTYPE, tarfile.LNKTYPE, tarfile.CHRTYPE, tarfile.FIFOTYPE):
                member = tarfile.TarInfo('settings/link')
                member.type = kind
                member.linkname = '/etc'
                self.archive(path, member)
                with self.subTest(kind=kind), self.assertRaises(RuntimeError):
                    storage.verify_archive(path)


class BootTests(unittest.TestCase):
    def test_missing_media_uses_bounded_state_before_machine_id_bind(self):
        self.check_temporary_state(False)

    def test_maintenance_does_not_discover_or_mount_the_card(self):
        self.check_temporary_state(True)

    def check_temporary_state(self, maintenance):
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ('run', 'state', 'data', 'factory', 'var/lib', 'etc', 'proc/sys/kernel/random'):
                (root / name).mkdir(parents=True, exist_ok=True)
            (root / 'proc/sys/kernel/random/uuid').write_text('01234567-89ab-cdef-0123-456789abcdef\n')
            (root / 'proc/cmdline').write_text('mnc.storage=maintenance' if maintenance else '')
            def mapped(value):
                path = Path(value)
                return root / str(path).lstrip('/') if path.is_absolute() else path
            p = policy()
            if maintenance:
                p['runtime_root'] = '/run/test-product'
            runtime = root / p.get('runtime_root', '/run/mnc').lstrip('/')
            commands = []
            def run(*args, **kwargs):
                commands.append(args)
                return ''
            with patch.object(storage, 'Path', mapped), patch.object(storage, 'inventory', return_value=[]) as inventory, \
                 patch.object(storage, 'run', side_effect=run), patch.object(storage.time, 'sleep'), \
                 patch.object(storage.os.path, 'ismount', return_value=False):
                storage.prepare(p)
                if maintenance:
                    inventory.assert_not_called()
            for directory in (runtime, runtime / 'storage'):
                self.assertEqual(directory.stat().st_mode & 0o777, 0o755)
            status = json.loads((runtime / 'storage/status.json').read_text())
            self.assertFalse(status['capabilities']['persistent_settings'])
            self.assertFalse(status['capabilities']['recordings'])
            self.assertFalse(status['capabilities']['factory_reset'])
            self.assertEqual((root / 'state/os/machine-id').read_text(), '0123456789abcdef0123456789abcdef\n')
            state_mount = next(i for i, c in enumerate(commands) if 'mnc-state' in c)
            bind = next(i for i, c in enumerate(commands) if c[-1] == '/etc/machine-id')
            self.assertLess(state_mount, bind)
            self.assertIn('size=32m', commands[state_mount][4])
            for c in commands:
                self.assertNotIn('mkfs.ext4', c)
            for role in ('factory', 'data'):
                command = next(c for c in commands if 'mnc-' + role in c)
                self.assertIn(',ro', command[4])

    def test_reformat_refuses_unconfirmed_or_disk_root_before_stopping_services(self):
        from unittest.mock import patch
        p = policy()
        with patch.object(storage, 'verify_archive', return_value={'target': p['target']}), \
             patch.object(storage, 'quiesce') as stop, patch.object(storage, 'run', return_value='ext4'), \
             patch.object(storage, 'inventory', return_value=[disk()]):
            with self.assertRaises(RuntimeError):
                storage.reformat(p, '/dev/sda', Path('/run/test.tar'), '')
            with self.assertRaises(RuntimeError):
                storage.reformat(p, '/dev/sda', Path('/run/test.tar'), 'ERASE:/dev/sda')
            stop.assert_not_called()

    def test_reset_replays_only_data_format_and_preserves_identity(self):
        from types import SimpleNamespace
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ('state/msa/settings', 'state/mnc-system', 'state/os', 'data', 'factory',
                         'run', 'var/lib', 'etc', 'proc', 'sys/class/block/sda3/holders'):
                (root / name).mkdir(parents=True, exist_ok=True)
            (root / 'proc/cmdline').write_text('')
            (root / 'state/mnc-system/state.json').write_text('{"reset_intent":true}')
            identity = root / 'state/os/machine-id'
            identity.write_text('0123456789abcdef0123456789abcdef\n')
            (root / 'factory/productInfo.json').write_text('{"serial_number":"retained"}')
            (root / 'state/msa/settings/active.json').write_text('{"old":true}')
            nodes = [dict(name='/dev/sda', type='disk', children=[dict(
                name='/dev/sda' + str(i), type='part', pkname='/dev/sda', fstype='ext4',
                label='msa-' + role, mountpoints=[]) for i, role in enumerate(storage.ROLES, 1)])]
            def mapped(value):
                path = Path(value)
                return root / str(path).lstrip('/') if path.is_absolute() else path
            commands = []
            with patch.object(storage, 'Path', mapped), patch.object(storage, 'inventory', return_value=nodes), \
                 patch.object(storage, 'run', side_effect=lambda *args, **kw: commands.append(args)), \
                 patch.object(storage.subprocess, 'run', return_value=SimpleNamespace(returncode=0)), \
                 patch.object(storage.os.path, 'ismount', return_value=False):
                # Replay a boot interrupted before the manager clears its durable reset intent.
                for attempt in range(2):
                    storage.prepare(policy())
                    status = json.loads((root / 'run/mnc/storage/status.json').read_text())
                    self.assertTrue(status['reset_prepared'])
                    (root / 'run/mnc/os/localtime').unlink()
            formats = [c for c in commands if c[0] == 'mkfs.ext4']
            self.assertEqual(formats, [('mkfs.ext4', '-F', '-L', 'msa-data', '/dev/sda3')] * 2)
            self.assertFalse((root / 'state/msa/settings').exists())
            self.assertEqual(identity.read_text(), '0123456789abcdef0123456789abcdef\n')
            self.assertTrue((root / 'factory/productInfo.json').exists())
            self.assertTrue(json.loads((root / 'state/mnc-system/state.json').read_text())['reset_intent'])


if __name__ == '__main__':
    unittest.main()
