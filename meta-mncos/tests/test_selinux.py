# SPDX-License-Identifier: GPL-3.0-only
import importlib.machinery
import ctypes
import ctypes.util
import os
import shutil
import stat
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1] / 'recipes-core/mnc-selinux/files/mnc-selinux'
loader = importlib.machinery.SourceFileLoader('mnc_selinux', str(SOURCE))
spec = importlib.util.spec_from_loader(loader.name, loader)
selinux = importlib.util.module_from_spec(spec)
loader.exec_module(selinux)


class SelinuxTests(unittest.TestCase):
    def test_symlink_components_and_traversal_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'link').symlink_to(root, target_is_directory=True)
            for path in [root / 'link/store', root / '../store', Path('relative')]:
                with self.subTest(path=path), self.assertRaises(RuntimeError):
                    selinux.safe(path)

    def test_capture_requires_immutable_existing_image_store(self):
        with patch.object(selinux, 'readonly', return_value=False), patch.object(selinux, 'run') as run:
            with self.assertRaises(RuntimeError):
                selinux.store('capture', {})
            run.assert_not_called()

    def test_capture_and_finalize_use_current_image_readonly_bind(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'image-store'
            source.mkdir()
            captured = Path(directory) / 'runtime/selinux/store'
            def paths(value):
                return source if str(value) == '/var/lib/selinux' else captured
            with patch.object(selinux, 'safe', side_effect=paths), patch.object(selinux, 'readonly', return_value=True), patch.object(selinux, 'run') as run:
                selinux.store('capture', {})
                selinux.store('finalize', {})
                self.assertEqual(run.call_args_list[0].args, ('mount', '--bind', str(source), str(captured)))
                self.assertEqual(run.call_args_list[2].args, ('mount', '--bind', str(captured), str(source)))
                self.assertEqual(run.call_args_list[3].args, ('mount', '-o', 'remount,bind,ro', str(source)))
                self.assertEqual(captured.parent.stat().st_mode & 0o777, 0o700)

    def test_disabled_does_not_pass_loaded_policy_check(self):
        for mode in ['Disabled']:
            with patch.object(selinux, 'run', return_value=mode), self.assertRaises(RuntimeError):
                selinux.loaded()
        for mode in ['Permissive', 'Enforcing']:
            with patch.object(selinux, 'run', return_value=mode):
                selinux.loaded()

    def test_maintenance_never_relabels_readonly_mount(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(selinux, 'readonly', return_value=True), patch.object(selinux, 'run') as run:
                with self.assertRaises(RuntimeError):
                    selinux.relabel(directory)
                run.assert_not_called()

    def test_restorecon_excludes_immutable_nested_policy_store(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(selinux, 'readonly', return_value=False), patch.object(selinux, 'run') as run:
                selinux.relabel(directory)
                run.assert_called_once_with('restorecon', '-RF', '-e', '/var/lib/selinux', '-e', '/state/os/var-lib/selinux', '-e', '/run/mnc/selinux/store', directory)

    def test_factory_mismatch_is_visible_without_relabel_or_remount(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            with patch.object(selinux, 'loaded'), patch.object(selinux, 'safe', side_effect=lambda value: path if value == '/factory' else Path('/nonexistent-mnc-test')), patch.object(selinux.subprocess, 'run') as run, patch('sys.stderr') as stderr:
                run.return_value.returncode = 1
                selinux.restore({}, {})
                run.assert_called_once_with(['matchpathcon', '-V', directory], check=False)
                self.assertIn('factory label validation failed', ''.join(call.args[0] for call in stderr.write.call_args_list))


class SelinuxPolicyAliasTests(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('MNC_SELINUX_TEST_CONTEXTS'),
                         'set MNC_SELINUX_TEST_CONTEXTS to built refpolicy file_contexts')
    def test_real_policy_resolves_persistent_and_immutable_aliases(self):
        source = Path(os.environ['MNC_SELINUX_TEST_CONTEXTS'])
        aliases = SOURCE.parents[3] / 'dynamic-layers/selinux/recipes-security/refpolicy/files/mncos-file_contexts.subs_dist'
        lib = ctypes.CDLL(ctypes.util.find_library('selinux'), use_errno=True)
        class Option(ctypes.Structure):
            _fields_ = [('type', ctypes.c_int), ('value', ctypes.c_char_p)]
        lib.selabel_open.argtypes = [ctypes.c_uint, ctypes.POINTER(Option), ctypes.c_uint]
        lib.selabel_open.restype = ctypes.c_void_p
        lib.selabel_lookup_raw.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p), ctypes.c_char_p, ctypes.c_int]
        lib.selabel_close.argtypes = [ctypes.c_void_p]
        lib.freecon.argtypes = [ctypes.c_void_p]
        with tempfile.TemporaryDirectory() as directory:
            context = Path(directory) / 'file_contexts'
            shutil.copyfile(source, context)
            for sidecar in ['.local', '.homedirs']:
                if source.with_suffix(sidecar).exists():
                    shutil.copyfile(source.with_suffix(sidecar), context.with_suffix(sidecar))
            original = source.with_suffix('.subs_dist').read_bytes()
            upstream = original.split(b'# Persistent backing inodes')[0]
            context.with_suffix('.subs_dist').write_bytes(upstream + aliases.read_bytes())
            option = Option(3, os.fsencode(context))
            handle = lib.selabel_open(0, ctypes.byref(option), 1)
            self.assertTrue(handle, 'real policy file backend failed to load')
            cases = {'/run/ssh': (stat.S_IFDIR, 'etc_t'),
                     '/state/os/ssh': (stat.S_IFDIR, 'etc_t'),
                     '/run/mnc/selinux/store': (stat.S_IFDIR, 'semanage_store_t'),
                     '/var/lib/selinux': (stat.S_IFDIR, 'semanage_store_t'),
                     '/state/os/root-ssh': (stat.S_IFDIR, 'ssh_home_t'),
                     '/root/.ssh': (stat.S_IFDIR, 'ssh_home_t')}
            for prefix in ['/run/ssh', '/state/os/ssh']:
                for algorithm in ['rsa', 'ed25519', 'ecdsa']:
                    for suffix in ['', '.pub']:
                        cases[prefix + '/ssh_host_' + algorithm + '_key' + suffix] = (stat.S_IFREG, 'sshd_key_t')
            try:
                for path, (mode, expected) in cases.items():
                    with self.subTest(path=path):
                        value = ctypes.c_char_p()
                        self.assertEqual(lib.selabel_lookup_raw(handle, ctypes.byref(value), os.fsencode(path), mode), 0,
                                         'alias has no expected context: ' + path)
                        try:
                            self.assertEqual(value.value.decode().split(':')[2], expected)
                        finally:
                            lib.freecon(ctypes.cast(value, ctypes.c_void_p))
            finally:
                lib.selabel_close(handle)


if __name__ == '__main__':
    unittest.main()
