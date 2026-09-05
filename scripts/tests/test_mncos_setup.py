from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class SetupSDKTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.workspace = Path(self.temporary.name)
        shutil.copy(ROOT / "yocto-script/setupSDK", self.workspace / "setupSDK")
        core = self.workspace / "sources/openembedded-core"
        core.mkdir(parents=True)
        (core / "oe-init-build-env").write_text('printf "INITIALIZED:%s:%s\\n" "$1" "$2"\n')
        self.bitbake = self.workspace / "sources/bitbake/bin/bitbake"
        self.bitbake.parent.mkdir(parents=True)
        self.bitbake.touch()
        for product in ("msap1", "kr260demo", "zuboard"):
            (self.workspace / f"sources/meta-monutchee/meta-{product}/conf/templates/default").mkdir(parents=True)

    def run_setup(self, product="msap1", build="build", shell="bash"):
        env = os.environ.copy()
        env.pop("MONUTCHEE_PRODUCT", None)
        return subprocess.run(
            [shell, "-c", 'source ./setupSDK --product "$1" "$2"', "test", product, build],
            cwd=self.workspace, env=env, text=True, capture_output=True,
        )

    def test_all_products_use_split_core_and_explicit_bitbake(self):
        for product in ("msap1", "kr260demo", "zudemo"):
            with self.subTest(product=product):
                result = self.run_setup(product)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(f"INITIALIZED:build:{self.workspace}/sources/bitbake", result.stdout)

    def test_missing_bitbake_fails_before_initialization(self):
        self.bitbake.unlink()
        result = self.run_setup()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Synchronize the Yocto sources", result.stderr)
        self.assertNotIn("INITIALIZED", result.stdout)

    def test_old_layers_fail_without_modifying_user_configuration(self):
        conf = self.workspace / "custom build/conf/bblayers.conf"
        conf.parent.mkdir(parents=True)
        for layer in ("poky/meta", "meta-yocto/meta-poky", "meta-yocto/meta-yocto-bsp"):
            with self.subTest(layer=layer):
                content = f'BBLAYERS = "/workspace/sources/{layer}"\n'
                conf.write_text(content)
                result = self.run_setup(build=str(conf.parent.parent))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Move the old build directory aside", result.stderr)
                self.assertEqual(conf.read_text(), content)

    def test_current_layers_and_historical_comment_are_accepted(self):
        conf = self.workspace / "build/conf/bblayers.conf"
        conf.parent.mkdir(parents=True)
        conf.write_text('# formerly sources/poky/meta\nBBLAYERS = "/sources/openembedded-core/meta"\n')
        result = self.run_setup()
        self.assertEqual(result.returncode, 0, result.stderr)

    @unittest.skipUnless(shutil.which("zsh"), "zsh is unavailable")
    def test_zsh_setup(self):
        result = self.run_setup(shell="zsh")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("INITIALIZED:build:", result.stdout)


if __name__ == "__main__":
    unittest.main()
