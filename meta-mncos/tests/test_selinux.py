# SPDX-License-Identifier: GPL-3.0-only
import importlib.machinery
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
                run.assert_called_once_with('restorecon', '-RF', '-e', '/var/lib/selinux', '-e', '/state/os/var-lib/selinux', directory)

    def test_factory_mismatch_is_visible_without_relabel_or_remount(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            with patch.object(selinux, 'loaded'), patch.object(selinux, 'safe', side_effect=lambda value: path if value == '/factory' else Path('/nonexistent-mnc-test')), patch.object(selinux.subprocess, 'run') as run, patch('sys.stderr') as stderr:
                run.return_value.returncode = 1
                selinux.restore({}, {})
                run.assert_called_once_with(['matchpathcon', '-V', directory], check=False)
                self.assertIn('factory label validation failed', ''.join(call.args[0] for call in stderr.write.call_args_list))


if __name__ == '__main__':
    unittest.main()
