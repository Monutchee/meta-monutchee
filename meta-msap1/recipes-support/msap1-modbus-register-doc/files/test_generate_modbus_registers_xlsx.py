#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Focused tests for the dependency-free Modbus workbook generator."""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from xml.etree import ElementTree


GENERATOR_PATH = Path(__file__).with_name("generate_modbus_registers_xlsx.py")
SPEC = importlib.util.spec_from_file_location("modbus_xlsx_generator", GENERATOR_PATH)
assert SPEC is not None and SPEC.loader is not None
GENERATOR = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = GENERATOR
SPEC.loader.exec_module(GENERATOR)


class GeneratorTests(unittest.TestCase):
    def test_generates_expected_single_sheet_workbook(self) -> None:
        document = {
            "schema": "mnc.modbus-register-map.v1",
            "registers": [
                {
                    "function_code": 3,
                    "address": 0,
                    "last_address": 1,
                    "data_type": "float32",
                    "source": "voltage.ln.a.rms",
                },
                {
                    "function_code": 4,
                    "address": 16,
                    "last_address": 17,
                    "data_type": "uint32",
                    "source": "frequency",
                },
            ],
        }

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            input_path = root / "map.json"
            output_path = root / "map.xlsx"
            input_path.write_text(json.dumps(document), encoding="utf-8")

            rows = GENERATOR.load_register_rows(input_path)
            GENERATOR.write_workbook(rows, output_path)

            with zipfile.ZipFile(output_path) as archive:
                self.assertEqual(archive.testzip(), None)
                self.assertIn("xl/worksheets/sheet1.xml", archive.namelist())
                worksheet = archive.read("xl/worksheets/sheet1.xml").decode()
                workbook = archive.read("xl/workbook.xml")

            ElementTree.fromstring(workbook)
            ElementTree.fromstring(worksheet)
            self.assertIn("0x0000", worksheet)
            self.assertIn("0x0011", worksheet)
            self.assertIn("0x03", worksheet)
            self.assertIn("0x04", worksheet)
            self.assertIn("voltage.ln.a.rms", worksheet)
            self.assertIn("frequency", worksheet)

    def test_rejects_unknown_schema(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            input_path = Path(directory) / "map.json"
            input_path.write_text(
                json.dumps({"schema": "unknown", "registers": [{}]}),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "unsupported Modbus map schema"):
                GENERATOR.load_register_rows(input_path)


if __name__ == "__main__":
    unittest.main()
