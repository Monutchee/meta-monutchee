"""Exercise actual BitBake override semantics with the pinned parser.

Set MNCOS_TEST_OECORE and put BitBake's lib directory on PYTHONPATH to run.
"""

import os
import shutil
import subprocess
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

    def test_gpu_ownership_preserves_other_pm_policy_and_rejects_conflicts(self):
        self.data.setVar("OVERRIDES", "zynqmp:class-target")
        self.data.setVar("ESW_MACHINE", "psu_cortexa53_0")
        self.data.setVar("MNCOS_MULTIMEDIA_OFF", "1")
        self.data.setVar("S", self.temporary.name)
        self.data.setVar("ESW_COMPONENT_SRC", "/xilpm/")
        source = Path(self.temporary.name)
        target = source / "xilpm/zynqmp/client/common/pm_cfg_obj.c"
        target.parent.mkdir(parents=True)
        generated = source / "pm_cfg_obj.c"
        apu = "PM_CONFIG_IPI_PSU_CORTEXA53_0_MASK"
        rpu = "PM_CONFIG_IPI_PSU_CORTEXR5_0_MASK"
        text = "/* SLAVE SECTION */\n" + "\n".join(
            f"{node}, PM_SLAVE_FLAG_IS_SHAREABLE, {apu} | {rpu},"
            for node in ("NODE_GPU", "NODE_GPU_PP_0", "NODE_GPU_PP_1", "NODE_USB_0")
        ) + "\n/* PREALLOC SECTION */\nNODE_DDR, unchanged_requirements,\n"
        generated.write_text(text)
        target.write_text(text)
        recipe = "meta-zynqmp-addon/recipes-bsp/embeddedsw/xilpm_2026.1.bbappend"
        self.run_policy_function("mncos_gpu_apu_ownership", recipe)
        result = generated.read_text()
        self.assertEqual(result.count(rpu), 1)  # USB ownership is preserved.
        self.assertIn(f"NODE_USB_0, PM_SLAVE_FLAG_IS_SHAREABLE, {apu} | {rpu},", result)
        self.assertTrue(result.endswith("NODE_DDR, unchanged_requirements,\n"))
        self.assertEqual(target.read_text(), result)
        self.run_policy_function("mncos_gpu_apu_ownership", recipe)
        self.assertEqual(generated.read_text(), result)  # Reconfiguration is safe.
        for bad in (text + "NODE_GPU, preallocated,", text.replace(apu, rpu)):
            generated.write_text(bad)
            with self.assertRaises(ValueError):
                self.run_policy_function("mncos_gpu_apu_ownership", recipe)
            self.assertEqual(generated.read_text(), bad)
        self.data.setVar("MNCOS_MULTIMEDIA_OFF", "0")
        generated.write_text(text)
        self.run_policy_function("mncos_gpu_apu_ownership", recipe)
        self.assertEqual(generated.read_text(), text)

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

    def test_multimedia_off_filters_codecs_and_audio_without_touching_dma(self):
        self.data.setVar("MNCOS_MULTIMEDIA_OFF", "1")
        self.data.setVar("DISTRO_FEATURES:append", " alsa pulseaudio")
        self.data.setVar("MACHINE_FEATURES:append", " alsa camera")
        self.assertEqual(self.features("MACHINE_FEATURES"), {"fpga-overlay"})
        self.assertEqual(self.features("IMAGE_FEATURES"), {"debug-tweaks"})
        self.assertNotIn("alsa", self.features("DISTRO_FEATURES"))
        self.data.setVar("MNCOS_MULTIMEDIA_OFF", "0")
        self.assertIn("vcu", self.features("MACHINE_FEATURES"))
        self.assertIn("hwcodecs", self.features("IMAGE_FEATURES"))

    def test_multimedia_off_requires_headless_and_valid_boolean(self):
        self.data.setVar("MNCOS_MULTIMEDIA_OFF", "yes")
        with self.assertRaisesRegex(ValueError, "MNCOS_MULTIMEDIA_OFF must"):
            self.run_policy_function("mncos_validate_configuration")
        self.data.setVar("MNCOS_MULTIMEDIA_OFF", "1")
        self.data.setVar("MNCOS_HEADLESS", "0")
        with self.assertRaisesRegex(ValueError, "requires MNCOS_HEADLESS"):
            self.run_policy_function("mncos_validate_configuration")

    def test_multimedia_off_does_not_filter_host_tools(self):
        self.data.setVar("MNCOS_MULTIMEDIA_OFF", "1")
        self.data.setVar("DISTRO_FEATURES:append", " alsa pulseaudio")
        for override in ("class-native", "class-nativesdk"):
            self.data.setVar("OVERRIDES", "mncos:" + override)
            self.assertIn("alsa", self.features("DISTRO_FEATURES"))
            self.assertIn("vcu", self.features("MACHINE_FEATURES"))

    def test_multimedia_off_kernel_rejects_capture_codec_and_audio(self):
        work = Path(self.temporary.name)
        self.data.setVar("WORKDIR", str(work))
        self.data.setVar("B", str(work))
        self.data.setVar("MNCOS_MULTIMEDIA_OFF", "1")
        (work / "mncos-headless.cfg").write_text("# CONFIG_DRM_LIMA is not set\n")
        (work / "mncos-multimedia-off.cfg").write_text(
            "# CONFIG_MEDIA_SUPPORT is not set\n# CONFIG_SOUND is not set\n# CONFIG_XILINX_VCU is not set\n")
        path = "meta-zynqmp-addon/recipes-kernel/linux/linux-xlnx_%.bbappend"
        for symbol in ("MEDIA_SUPPORT", "SOUND", "XILINX_VCU"):
            (work / ".config").write_text("CONFIG_%s=m\n" % symbol)
            with self.assertRaisesRegex(ValueError, "enables display/GPU"):
                self.run_policy_function("mncos_check_headless_kernel", path)
        (work / ".config").write_text("CONFIG_XILINX_DMA=y\nCONFIG_CMA=y\nCONFIG_RPMSG=y\n")
        self.run_policy_function("mncos_check_headless_kernel", path)

    @unittest.skipUnless(shutil.which("dtc") and shutil.which("fdtoverlay"), "requires native device-tree tools")
    def test_final_carrier_overlay_cannot_reenable_multimedia(self):
        work = Path(self.temporary.name)
        self.data.setVar("B", str(work))
        self.data.setVar("OVERRIDES", "mncos:zynqmp:class-target")
        base = work / "base.dts"
        base.write_text('/dts-v1/; / { gpu: gpu { status = "disabled"; }; '
                        'zynqmp_dpsub: display { status = "disabled"; }; '
                        'zynqmp_dpdma: dma { status = "disabled"; }; };')
        overlay = work / "carrier.dtso"
        overlay.write_text('/dts-v1/; /plugin/; '
                           '&zynqmp_dpsub { status = "okay"; }; '
                           '&zynqmp_dpdma { status = "okay"; };')
        subprocess.run(["dtc", "-@", "-I", "dts", "-O", "dtb", "-o", str(work / "base.dtb"), str(base)], check=True)
        subprocess.run(["dtc", "-@", "-I", "dts", "-O", "dtb", "-o", str(work / "carrier.dtbo"), str(overlay)], check=True)
        final = work / "board.dtb"
        subprocess.run(["fdtoverlay", "-i", str(work / "base.dtb"), "-o", str(final), str(work / "carrier.dtbo")], check=True)
        def status(node):
            return subprocess.check_output(["fdtget", str(final), node, "status"], text=True).strip()
        path = "meta-zynqmp-addon/recipes-bsp/device-tree/device-tree.bbappend"
        self.run_policy_function("mncos_finalize_multimedia_dt", path)
        self.assertEqual(status("/display"), "okay")  # opt-out preserves the carrier
        self.data.setVar("MNCOS_MULTIMEDIA_OFF", "1")
        self.data.setVar("OVERRIDES", "zynqmp:class-target")
        self.run_policy_function("mncos_finalize_multimedia_dt", path)
        self.assertEqual(status("/display"), "okay")  # Standalone firmware DT.
        self.data.setVar("OVERRIDES", "mncos:zynqmp:class-target")
        self.run_policy_function("mncos_finalize_multimedia_dt", path)
        for node in ("/gpu", "/display", "/dma"):
            self.assertEqual(status(node), "disabled")

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
