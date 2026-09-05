import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "meta-mncos/lib"))
from mncos.reports import collect_image


class ReportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.link = "msap1-image-msap1.rootfs"
        self.output = self.root / "reports"
        self.cve = self.root / (self.link + ".json")
        self.cve.write_text(json.dumps({"version": "1", "package": [{"name": "busybox", "issue": [{"status": "Unpatched"}]}]}))
        for suffix in (".spdx.tar.zst", ".manifest"):
            (self.root / (self.link + suffix)).write_bytes(b"fixture")
        (self.root / "mncos-kernel-msap1.config").write_text("CONFIG_DRM=y\n")

    def collect(self, recipes=None):
        collect_image(self.root, self.link, "msap1", self.output, {"image": "msap1-image"}, recipes)

    def test_unpatched_cves_are_reported_and_missing_firmware_coverage_is_visible(self):
        self.collect({"fsbl-firmware": self.root / "absent.json"})
        self.assertTrue((self.output / "image.spdx.tar.zst").is_file())
        report = json.loads((self.output / "build.json").read_text())
        self.assertIn("unavailable", report["recipe_cve_coverage"]["fsbl-firmware"])

    def test_missing_required_report_preserves_previous_collection(self):
        self.collect()
        before = (self.output / "build.json").read_bytes()
        self.cve.unlink()
        with self.assertRaisesRegex(ValueError, "Missing or empty"):
            self.collect()
        self.assertEqual((self.output / "build.json").read_bytes(), before)

    def test_empty_cve_coverage_is_rejected(self):
        self.cve.write_text('{"version": "1", "package": []}')
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            self.collect()


if __name__ == "__main__":
    unittest.main()
