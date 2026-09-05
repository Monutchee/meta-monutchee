#!/usr/bin/env python3

from __future__ import annotations

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
GENERATOR = REPOSITORY_ROOT / "scripts" / "create-xilinx-product-layer.py"
DOCUMENTATION = REPOSITORY_ROOT / "docs" / "create-xilinx-product-layer.md"


class CreateXilinxProductLayerTests(unittest.TestCase):
    def run_generator(self, *args: str):
        return subprocess.run(
            ["python3", str(GENERATOR), *args],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def test_generates_complete_kr260_layer_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            arguments = (
                "--product", "test-product",
                "--project-prefix", "TestProduct",
                "--board", "kr260",
                "--output-root", directory,
            )
            result = self.run_generator(*arguments)
            self.assertEqual(result.returncode, 0, result.stderr)

            layer = Path(directory) / "meta-test-product"
            expected = (
                "LICENSE",
                "README.md",
                "conf/layer.conf",
                "conf/templates/default/bblayers.conf.sample",
                "conf/templates/default/conf-notes.txt",
                "conf/templates/default/conf-summary.txt",
                "conf/templates/default/local.conf.sample",
                "recipes-core/images/test-product-image.bb",
                "recipes-core/images/test-product-production-flash-image.bb",
                "recipes-firmware/test-product-dfx-firmware/test-product-dfx-firmware_1.0.bb",
                "recipes-support/test-product-production-flasher/test-product-production-flasher_1.0.bb",
                "recipes-support/test-product-production-flasher/files/mncos-flash-emmc",
                "recipes-support/test-product-production-flasher/files/mncos-production-flash.conf",
            )
            for relative in expected:
                self.assertTrue((layer / relative).is_file(), relative)

            all_text = "\n".join(
                path.read_text() for path in layer.rglob("*") if path.is_file()
            )
            self.assertNotRegex(all_text, r"@@[A-Z0-9_]+@@")
            self.assertNotIn("__PRODUCT__", all_text)
            self.assertIn('COMPATIBLE_MACHINE = "^test-product$"', all_text)
            self.assertIn("TestProduct_PL.bit", all_text)
            self.assertIn("meta-monutchee/meta-mnc-artifact", all_text)
            layers = (layer / "conf/templates/default/bblayers.conf.sample").read_text()
            self.assertIn("##OEROOT##/meta", layers)
            self.assertNotIn("meta-poky", layers)
            self.assertNotIn("meta-yocto-bsp", layers)
            self.assertTrue(
                (layer / "recipes-support/test-product-production-flasher/files/mncos-flash-emmc").stat().st_mode
                & 0o111
            )
            for path in layer.rglob("*"):
                if path.is_file() and path.name != "mncos-flash-emmc":
                    self.assertEqual(path.stat().st_mode & 0o777, 0o644, path)

            repeated = self.run_generator(*arguments)
            self.assertNotEqual(repeated.returncode, 0)
            self.assertIn("destination already exists", repeated.stderr)

    def test_rejects_invalid_identifiers(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_generator(
                "--product", "Bad_Product",
                "--project-prefix", "MSAP1",
                "--board", "kr260",
                "--output-root", directory,
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("--product must start", result.stderr)

    def test_documented_options_match_help(self):
        help_result = self.run_generator("--help")
        self.assertEqual(help_result.returncode, 0, help_result.stderr)
        documentation = DOCUMENTATION.read_text()
        for option in (
            "--product",
            "--project-prefix",
            "--board",
            "--output-root",
            "--help",
        ):
            self.assertIn(option, help_result.stdout)
            self.assertRegex(documentation, rf"`?{re.escape(option)}`?")


if __name__ == "__main__":
    unittest.main()
