from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


LOADER = (
    Path(__file__).resolve().parents[1]
    / "recipes-core"
    / "images"
    / "files"
    / "load-jtag-image-station.tcl"
)


class StationLoaderTests(unittest.TestCase):
    def _execute_bound_loader(self, mode=None, *, unsupported=False, rendered="por", force=None,
                              binding="complete", rendered_force="1"):
        tclsh = shutil.which("tclsh")
        if tclsh is None:
            self.skipTest("tclsh is unavailable")

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            jtag = root / "jtag"
            tftp = root / "tftp"
            jtag.mkdir()
            tftp.mkdir()
            loader = jtag / "load-jtag-image.tcl"
            source = LOADER.read_text().replace("@JTAG_LOADER_FORCE_JTAG_BOOT@", rendered_force)
            if rendered is not None:
                source = source.replace("@JTAG_LOADER_BOARD_RESET@", rendered)
            loader.write_text(source)
            for name in ("pmufw.elf", "fsbl.elf", "tfa.elf", "u-boot.elf"):
                (jtag / name).write_bytes(b"test")
            for name in ("Image", "system.dtb", "rootfs.cpio.gz.u-boot", "boot.scr"):
                (tftp / name).write_bytes(b"test")

            harness = root / "test-loader.tcl"
            harness.write_text(
                f"""
set ::target_properties [list \\
    [dict create target_id 1 name PSU jtag_device_index 1 jtag_device_name arm_dap jtag_cable_ctx cable-a jtag_cable_serial SERIAL-A] \\
    [dict create target_id 2 name {{MicroBlaze PMU}} jtag_device_index 0 jtag_device_name xczu4ev jtag_cable_ctx cable-a jtag_cable_serial SERIAL-A] \\
    [dict create target_id 3 name {{Cortex-A53 #0}} jtag_device_index 1 jtag_device_name arm_dap jtag_cable_ctx cable-a jtag_cable_serial SERIAL-A] \\
    [dict create target_id 11 name PSU jtag_device_index 1 jtag_device_name arm_dap jtag_cable_ctx cable-b jtag_cable_serial SERIAL-B] \\
    [dict create target_id 12 name {{MicroBlaze PMU}} jtag_device_index 0 jtag_device_name xczu4ev jtag_cable_ctx cable-b jtag_cable_serial SERIAL-B] \\
    [dict create target_id 13 name {{Cortex-A53 #0}} jtag_device_index 1 jtag_device_name arm_dap jtag_cable_ctx cable-b jtag_cable_serial SERIAL-B]]
set ::selected_targets {{}}
set ::target_query_counts [dict create PSU 0 PMU 0 A53 0]
set ::pmu_download_failures 0
proc connect {{args}} {{puts "EVENT:connect"}}
proc disconnect {{args}} {{puts "EVENT:disconnect"}}
proc targets {{args}} {{
    if {{[lsearch -exact $args -target-properties] >= 0}} {{
        set query [join $args " "]
        if {{[string first "MicroBlaze PMU" $query] >= 0}} {{
            set kind PMU
            set pattern "*MicroBlaze PMU*"
        }} elseif {{[string first "A53" $query] >= 0}} {{
            set kind A53
            set pattern "*A53*#0"
        }} else {{
            set kind PSU
            set pattern "*PSU*"
        }}
        dict incr ::target_query_counts $kind
        if {{[dict get $::target_query_counts $kind] == 1}} {{
            return {{}}
        }}
        set result {{}}
        foreach target $::target_properties {{
            if {{[string match -nocase $pattern [dict get $target name]]}} {{
                lappend result $target
            }}
        }}
        return $result
    }}
    if {{[llength $args] == 1 && [string is integer -strict [lindex $args 0]]}} {{
        lappend ::selected_targets [lindex $args 0]
        return
    }}
    error "unexpected targets invocation: $args"
}}
proc dow {{args}} {{
    puts "EVENT:dow"
    if {{[string first "pmufw.elf" [join $args " "]] >= 0 &&
         $::pmu_download_failures == 0}} {{
        incr ::pmu_download_failures
        error "simulated transient PMU download failure"
    }}
}}
proc rst {{args}} {{
    puts "EVENT:rst:$args"
    if {{ $args eq "-por" && [info exists ::env(TEST_POR_UNSUPPORTED)] }} {{
        error "por not supported for target"
    }}
}}
proc mwr {{args}} {{puts "EVENT:mwr:$args"}}
foreach command {{mask_write stop con}} {{
    proc $command {{args}} {{puts "EVENT:operation"}}
}}
proc print {{args}} {{return [dict create &XFsbl_Exit 4294708232]}}
proc bpadd {{args}} {{return 7}}
proc bpremove {{args}} {{if {{$args ne "7"}} {{error "unexpected breakpoint removal"}}}}
proc rrd {{args}} {{return [dict create pc 00000000fffc0c08]}}
proc mrd {{args}} {{return 0x00000000}}
rename after tcl_after
proc after {{args}} {{puts "WAIT:$args"}}
set argv [list tcp:127.0.0.1:3121 192.0.2.10 "" 5 SERIAL-B 1]
if {{"{binding}" eq "missing-device"}} {{set argv [lrange $argv 0 4]}}
if {{"{binding}" eq "unbound"}} {{set argv [lrange $argv 0 3]}}
source {{{loader.as_posix()}}}
puts "MNC_SELECTED_TARGETS:[join $::selected_targets ,]"
""",
                encoding="utf-8",
            )
            result = subprocess.run(
                [tclsh, str(harness)],
                check=False,
                capture_output=True,
                text=True,
                env={**{key: value for key, value in os.environ.items()
                         if key not in ("MNCOS_JTAG_BOARD_RESET", "MNCOS_FORCE_JTAG_BOOT", "TEST_POR_UNSUPPORTED")},
                     **({"MNCOS_FORCE_JTAG_BOOT": str(int(force)) if isinstance(force, bool) else str(force)}
                        if force is not None else {}),
                     **({"MNCOS_JTAG_BOARD_RESET": mode} if mode is not None else {}),
                     **({"TEST_POR_UNSUPPORTED": "1"} if unsupported else {})},
            )
            return result

    def test_stable_cable_identity_survives_a_stale_target_id(self) -> None:
        result = self._execute_bound_loader()
        self.assertEqual(result.returncode, 0, result.stderr)
        selected_line = next(
            line
            for line in result.stdout.splitlines()
            if line.startswith("MNC_SELECTED_TARGETS:")
        )
        selected = selected_line.partition(":")[2].split(",")
        self.assertIn("11", selected)
        self.assertIn("12", selected)
        self.assertIn("13", selected)
        self.assertLessEqual(set(selected), {"11", "12", "13"})
        self.assertIn(
            "Selected JTAG device is not visible yet; retrying target discovery",
            result.stdout,
        )
        self.assertIn(
            "Waiting for *MicroBlaze PMU* target on selected JTAG cable",
            result.stdout,
        )
        self.assertIn(
            "Waiting for *A53*#0 target on selected JTAG device",
            result.stdout,
        )
        self.assertIn(
            "Downloading PMU firmware failed; retrying download",
            result.stdout,
        )

    def test_srst_defaults_and_override_preserve_two_second_settle(self):
        for kwargs in ({"mode": "srst"}, {"rendered": "srst"}, {"rendered": None}):
            with self.subTest(kwargs=kwargs):
                result = self._execute_bound_loader(**kwargs)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("EVENT:rst:-srst\nWAIT:2000\nEVENT:disconnect", result.stdout)
                self.assertNotIn("EVENT:rst:-por", result.stdout)
                self.assertNotIn("WAIT:5000", result.stdout)
                self.assertIn("EVENT:dow", result.stdout)

    def test_disabled_boot_override_keeps_reset_wait_without_system_reset(self):
        for mode, wait in (("por", 5000), ("srst", 2000)):
            for kwargs in ({"force": False}, {"force": "off"}, {"rendered_force": "0"}):
                with self.subTest(mode=mode, kwargs=kwargs):
                    result = self._execute_bound_loader(mode, **kwargs)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    reset_wait = f"EVENT:rst:-{mode}\nWAIT:{wait}"
                    self.assertIn(reset_wait, result.stdout)
                    self.assertLess(result.stdout.index(reset_wait), result.stdout.index("EVENT:dow"))
                    self.assertNotIn("EVENT:rst:-system", result.stdout)
                    self.assertNotIn("EVENT:mwr:0xff5e0200", result.stdout)

    def test_cable_binding_without_explicit_device_index_remains_supported(self):
        result = self._execute_bound_loader(binding="missing-device")
        self.assertEqual(result.returncode, 0, result.stderr)
        selected = result.stdout.split("MNC_SELECTED_TARGETS:", 1)[1].strip().split(",")
        self.assertLessEqual(set(selected), {"11", "12", "13"})

    def test_por_five_second_wait_precedes_override_system_reset_and_download(self):
        result = self._execute_bound_loader("por", force=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        events = [line for line in result.stdout.splitlines() if line.startswith("EVENT:")]
        self.assertEqual(events[:9], ["EVENT:connect", "EVENT:rst:-por",
                                     "EVENT:disconnect", "EVENT:connect",
                                     "EVENT:mwr:0xffca0010 0x0",
                                     "EVENT:mwr:0xff5e0200 0x0100",
                                     "EVENT:rst:-system", "EVENT:disconnect", "EVENT:connect"])
        self.assertEqual(events.count("EVENT:rst:-por"), 1)
        self.assertEqual(events.count("EVENT:rst:-system"), 1)
        self.assertLess(events.index("EVENT:rst:-system"), events.index("EVENT:dow"))
        self.assertIn("EVENT:rst:-por\nWAIT:5000\nEVENT:disconnect", result.stdout)
        self.assertIn("EVENT:rst:-cores -clear-registers", events)
        self.assertLess(events.index("EVENT:mwr:0xff5e0200 0x0100"),
                        events.index("EVENT:dow"))
        source = LOADER.read_text()
        self.assertRegex(source, r"(?m)^\s*rst -system(?:\s|$)")
        self.assertIn('if { $BOARD_RESET eq "por" } {\n    after 5000\n} else {\n    after 2000\n}', source)

    def test_unsupported_por_aborts_before_programming(self):
        result = self._execute_bound_loader("por", unsupported=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Could not pulse cable POR", result.stderr)
        events = [line for line in result.stdout.splitlines() if line.startswith("EVENT:")]
        self.assertEqual(events, ["EVENT:connect", "EVENT:rst:-por"])

    def test_invalid_reset_rejected_before_connect(self):
        for mode in ("", "POR", "por;rst -srst", "por\n", "unknown"):
            with self.subTest(mode=mode):
                result = self._execute_bound_loader(mode)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("must be srst or por", result.stderr)
                self.assertNotIn("EVENT:", result.stdout)

    def test_station_loader_never_uses_legacy_tftp_root(self) -> None:
        source = LOADER.read_text(encoding="utf-8")
        self.assertNotIn("/srv/tftp", source)
        self.assertIn('set TFTP_DIR [file join $ARTIFACT_DIR "tftp"]', source)

    def test_all_payload_downloads_use_retry_helpers(self) -> None:
        source = LOADER.read_text(encoding="utf-8")
        self.assertIn("MNC_STATION_TARGET_SELECTOR_V3", source)
        for name in ("pmufw.elf", "fsbl.elf", "tfa.elf", "u-boot.elf"):
            self.assertIn(f'[file join $JTAG_DIR "{name}"]', source)
        self.assertIn(
            'download_data_with_retry "Downloading system DTB"', source
        )
        boot_flow = source.split(
            'puts "Connecting to the Xilinx hw_server', maxsplit=1
        )[1]
        self.assertNotRegex(boot_flow, r"(?m)^\s*dow(?:\s|$)")


if __name__ == "__main__":
    unittest.main()
