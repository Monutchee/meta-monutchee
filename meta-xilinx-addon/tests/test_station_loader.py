from __future__ import annotations

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
    def test_stable_cable_identity_survives_a_stale_target_id(self) -> None:
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
            shutil.copyfile(LOADER, loader)
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
proc connect {{args}} {{}}
proc disconnect {{args}} {{}}
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
    if {{[string first "pmufw.elf" [join $args " "]] >= 0 &&
         $::pmu_download_failures == 0}} {{
        incr ::pmu_download_failures
        error "simulated transient PMU download failure"
    }}
}}
foreach command {{mwr rst mask_write stop con}} {{
    proc $command {{args}} {{}}
}}
rename after tcl_after
proc after {{args}} {{}}
set argv [list tcp:127.0.0.1:3121 192.0.2.10 "" 5 SERIAL-B 1]
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
            )
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
