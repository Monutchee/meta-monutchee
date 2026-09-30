# SPDX-License-Identifier: GPL-3.0-only
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


CORE = Path(__file__).resolve().parents[1] / 'recipes-core'
SOURCE = CORE / 'mnc-preboot/files/mnc-preboot'


class PrebootCommandTests(unittest.TestCase):
    def invoke(self, *args, backend=None):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / 'mnc-preboot'
            # Source files are installed executable by their recipes.
            if backend is None:
                storage = Path(directory) / 'mnc-storage'
                storage.write_bytes((CORE / 'mnc-storage/files/mnc-storage').read_bytes())
                storage.chmod(0o755)
                backend = storage
            script.write_text(SOURCE.read_text().replace('/usr/sbin/mnc-storage', str(backend)))
            return subprocess.run(['/bin/sh', str(script), *args],
                                  text=True, capture_output=True)

    def test_shell_invocation_only_shows_help(self):
        for args in [(), ('--help',), ('-h',)]:
            with self.subTest(args=args):
                result = self.invoke(*args)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn('Usage: mnc-preboot storage', result.stdout)
                self.assertEqual(result.stderr, '')

    def test_unknown_subcommand_does_not_start_init(self):
        result = self.invoke('unknown')
        self.assertEqual(result.returncode, 2)
        self.assertIn('unknown subcommand: unknown', result.stderr)

    def test_storage_help_and_missing_command(self):
        result = self.invoke('storage', '--help')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('usage: mnc-preboot storage', result.stdout)
        self.assertIn('reformat', result.stdout)
        result = self.invoke('storage')
        self.assertEqual(result.returncode, 2)
        self.assertIn('usage: mnc-preboot storage', result.stderr)

    def test_storage_arguments_and_exit_status_are_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            backend = Path(directory) / 'backend'
            backend.write_text('#!/usr/bin/env python3\nimport json, os, sys\n'
                               'print(json.dumps([os.environ["MNC_STORAGE_PROG"], sys.argv[1:]]))\n'
                               'sys.exit(17)\n')
            backend.chmod(0o755)
            args = ['reformat', '--backup', '/run/archive with spaces.tar',
                    '--disk', '/dev/sda', '--confirm', 'ERASE:/dev/sda']
            result = self.invoke('storage', *args, backend=backend)
            self.assertEqual(result.returncode, 17, result.stderr)
            self.assertEqual(json.loads(result.stdout), ['mnc-preboot storage', args])


if __name__ == '__main__':
    unittest.main()
