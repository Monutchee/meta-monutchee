"""Exercise actual BitBake override semantics with the pinned parser.

Set MNCOS_TEST_OECORE and put BitBake's lib directory on PYTHONPATH to run.
"""

import os
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch

try:
    import bb.data
    import bb.parse
    import bb.parse.parse_py.BBHandler
except ImportError:
    bb = None

ROOT = Path(__file__).resolve().parents[2]
CORE = os.environ.get("MNCOS_TEST_OECORE")


@unittest.skipUnless(bb and CORE, "requires BitBake on PYTHONPATH and MNCOS_TEST_OECORE")
class PolicyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.data = bb.data.init()
        self.data.setVar("TOPDIR", self.temporary.name)
        self.data.setVar("BBPATH", str(Path(CORE) / "meta") + ":" + str(ROOT / "meta-mncos"))
        self.data.setVar("OVERRIDES", "mncos:class-target")
        self.data.setVar("DISTRO_FEATURES_DEFAULT", "x11 wifi ipv6")
        self.data.setVar("MACHINE_FEATURES", "mali400 vcu fpga-overlay")
        self.data.setVar("EXTRA_IMAGE_FEATURES", "weston splash debug-tweaks")
        self.data.setVar("IMAGE_FEATURES", "x11 hwcodecs ${EXTRA_IMAGE_FEATURES}")
        bb.parse.handle(str(ROOT / "meta-mncos/conf/distro/include/mncos-policy.inc"), self.data, include=True)

    def features(self, variable):
        return set(self.data.getVar(variable).split())

    def run_policy_function(self, name, path="meta-mncos/classes/mncos-policy.bbclass"):
        bb.parse.parse_py.BBHandler.handle(str(ROOT / path), self.data, include=True)
        def fatal(message):
            raise ValueError(message)
        namespace = {"d": self.data, "os": os, "bb": types.SimpleNamespace(fatal=fatal)}
        exec("def check():\n" + self.data.getVar(name), namespace)
        namespace["check"]()

    def test_headless_filters_late_appends_and_keeps_non_graphics_features(self):
        self.data.setVar("DISTRO_FEATURES:append", " wayland opengl")
        self.data.setVar("MACHINE_FEATURES:append", " mali400")
        self.assertEqual(self.features("DISTRO_FEATURES"), {"wifi", "ipv6", "ptest", "multiarch"})
        self.assertEqual(self.features("MACHINE_FEATURES"), {"vcu", "fpga-overlay"})
        self.assertEqual(self.features("IMAGE_FEATURES"), {"hwcodecs", "debug-tweaks"})

    def test_opt_out_restores_inherited_and_explicit_graphics(self):
        self.data.setVar("MNCOS_HEADLESS", "0")
        self.assertTrue({"x11", "wayland", "opengl", "vulkan"} <= self.features("DISTRO_FEATURES"))
        self.assertIn("mali400", self.features("MACHINE_FEATURES"))
        self.assertTrue({"x11", "weston", "splash"} <= self.features("IMAGE_FEATURES"))

    def test_native_and_sdk_host_features_are_not_filtered(self):
        for override in ("class-native", "class-nativesdk"):
            with self.subTest(override=override):
                self.data.setVar("OVERRIDES", "mncos:" + override)
                self.assertTrue({"x11", "wayland", "opengl"} <= self.features("DISTRO_FEATURES"))

    def test_existing_product_codec_removal_survives_headless_mode(self):
        self.data.setVar("IMAGE_FEATURES:remove", "hwcodecs")
        self.assertEqual(self.features("IMAGE_FEATURES"), {"debug-tweaks"})

    def test_invalid_switch_is_rejected_by_configuration_handler(self):
        self.data.setVar("MNCOS_HEADLESS", "yes")
        with self.assertRaisesRegex(ValueError, 'must be "0" or "1"'):
            self.run_policy_function("mncos_validate_configuration")

    def test_missing_cve_database_is_an_error(self):
        self.data.setVar("CVE_CHECK_DB_FILE", self.temporary.name + "/missing.db")
        with self.assertRaisesRegex(ValueError, "requires a CVE database"):
            self.run_policy_function("mncos_require_cve_database")

    def test_rootfs_checks_recipe_ownership_of_renamed_binary_packages(self):
        self.data.setVar("PKGDATA_DIR", self.temporary.name)
        oe = types.ModuleType("oe")
        rootfs = types.ModuleType("oe.rootfs")
        packagedata = types.ModuleType("oe.packagedata")
        rootfs.image_list_installed_packages = lambda d: {"libegl-mesa0": {}}
        def read_pkgdatafile(path):
            self.assertEqual(Path(path).parent.name, "runtime-reverse")
            self.assertEqual(Path(path).name, "libegl-mesa0")
            return {"PN": "mesa"}
        packagedata.read_pkgdatafile = read_pkgdatafile
        oe.rootfs = rootfs
        oe.packagedata = packagedata
        with patch.dict("sys.modules", {"oe": oe, "oe.rootfs": rootfs, "oe.packagedata": packagedata}):
            with self.assertRaisesRegex(ValueError, r"libegl-mesa0 \(mesa\)"):
                self.run_policy_function("mncos_validate_headless_rootfs")
            self.data.setVar("MNCOS_HEADLESS", "0")
            self.run_policy_function("mncos_validate_headless_rootfs")

    def test_kernel_requires_capture_helpers_and_rejects_display_drivers(self):
        work = Path(self.temporary.name)
        self.data.setVar("WORKDIR", str(work))
        self.data.setVar("B", str(work))
        (work / "mncos-headless.cfg").write_text("# CONFIG_DRM_ZYNQMP_DPSUB is not set\n")
        config = work / ".config"
        path = "meta-zynqmp-addon/recipes-kernel/linux/linux-xlnx_%.bbappend"
        config.write_text("CONFIG_VIDEO_XILINX_DPRXSS=y\n")
        with self.assertRaisesRegex(ValueError, "requires DRM_KMS_HELPER"):
            self.run_policy_function("mncos_check_headless_kernel", path)
        config.write_text("CONFIG_VIDEO_XILINX_DPRXSS=y\nCONFIG_DRM_KMS_HELPER=y\n")
        self.run_policy_function("mncos_check_headless_kernel", path)
        with config.open("a") as stream:
            stream.write("CONFIG_DRM_ZYNQMP_DPSUB=y\n")
        with self.assertRaisesRegex(ValueError, "enables display/GPU"):
            self.run_policy_function("mncos_check_headless_kernel", path)


if __name__ == "__main__":
    unittest.main()
