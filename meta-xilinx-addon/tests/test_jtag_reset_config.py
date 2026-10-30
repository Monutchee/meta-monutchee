"""Check reset policy before BitBake shell interpolation and loader rendering."""
import re
import shutil
import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path

LAYER = Path(__file__).resolve().parents[1]


class JtagResetConfigTests(unittest.TestCase):
    def test_export_policy_validation_and_rendering(self):
        for filename, variable, loader_name in (
            ("xilinx-jtag-artifact.bbclass", "MNC_XILINX_JTAG_BOARD_RESET", "load-jtag-image-station.tcl"),
            ("export-tftpboot-file.bbclass", "JTAG_LOADER_BOARD_RESET", "load-jtag-image.tcl"),
        ):
            source = (LAYER / "classes" / filename).read_text()
            helper = "mnc_xilinx_jtag_board_reset" if filename.startswith("xilinx") else "mnc_jtag_loader_board_reset"
            self.assertIn(f'{variable} ?= "srst"', source)
            validation = next(body for body in re.findall(
                r"python __anonymous\(\) \{\n(.*?)\n\}", source, re.S
            ) if f'd.getVar("{variable}")' in body)

            class Data:
                def getVar(self, name):
                    return self.value

            class BitBake:
                @staticmethod
                def fatal(message):
                    raise ValueError(message)

            data = Data()
            helper_source = re.search(r"def " + helper + r"\(d\):\n(?:    .*\n)+", source).group()
            namespace = {"bb": BitBake}
            exec(helper_source, namespace)
            for value in ("srst", "por", "", "POR", "por\n", "por'; touch /tmp/unsafe; '"):
                with self.subTest(filename=filename, value=value):
                    data.value = value
                    if value in ("srst", "por"):
                        exec(textwrap.dedent(validation), {"d": data, "bb": BitBake})
                        self.assertEqual(namespace[helper](data), value)
                    else:
                        with self.assertRaisesRegex(ValueError, "must be srst or por"):
                            exec(textwrap.dedent(validation), {"d": data, "bb": BitBake})
                        with self.assertRaisesRegex(ValueError, "must be srst or por"):
                            namespace[helper](data)
            # Execute the class's exact sed expressions after trusted expansion.
            expressions = re.findall(r"-e '([^']+)'", source)
            loader = LAYER / "recipes-core/images/files" / loader_name
            for mode in ("srst", "por"):
                rendered = subprocess.run(
                    ["sed", *[part for expression in expressions for part in
                     ("-e", expression.replace("${@" + helper + "(d)}", mode)
                      .replace("${MNC_XILINX_JTAG_FORCE_JTAG_BOOT}", "0")
                      .replace("${JTAG_LOADER_FORCE_JTAG_BOOT}", "0"))], str(loader)],
                    check=True, capture_output=True, text=True,
                ).stdout
                self.assertIn(f'set BOARD_RESET "{mode}"', rendered)
                self.assertNotIn("@JTAG_LOADER_BOARD_RESET@", rendered)
                self.assertIn("rst -system", rendered)

    def test_missing_reset_token_rejects_por_before_loader_export(self):
        for filename, variable, loader_variable in (
            ("xilinx-jtag-artifact.bbclass", "MNC_XILINX_JTAG_BOARD_RESET", "MNC_XILINX_JTAG_LOADER_TCL"),
            ("export-tftpboot-file.bbclass", "JTAG_LOADER_BOARD_RESET", "JTAG_LOADER_TCL"),
        ):
            source = (LAYER / "classes" / filename).read_text()
            helper = "mnc_xilinx_jtag_board_reset" if filename.startswith("xilinx") else "mnc_jtag_loader_board_reset"
            guard = re.search(r'    if \[ "\$\{@' + helper +
                              r'\(d\)\}" = "por" \].*?\n    fi', source, re.S).group()
            with tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                loader = root / "loader.tcl"
                output = root / "export.tcl"
                for token in (False, True):
                    loader.write_text("@JTAG_LOADER_BOARD_RESET@" if token else "rst -srst")
                    for mode in ("srst", "por"):
                        with self.subTest(filename=filename, token=token, mode=mode):
                            output.unlink(missing_ok=True)
                            rendered_guard = guard.replace("${@" + helper + "(d)}", mode).replace(
                                "${" + loader_variable + "}", str(loader))
                            script = ('bbfatal() { echo "$*" >&2; exit 1; }\n' +
                                      rendered_guard + '\ncp "' + str(loader) + '" "' + str(output) + '"')
                            result = subprocess.run(["sh", "-c", script], capture_output=True, text=True)
                            rejected = mode == "por" and not token
                            self.assertEqual(result.returncode != 0, rejected)
                            self.assertEqual(output.exists(), not rejected)
                            if rejected:
                                self.assertIn("lacks the board-reset substitution token", result.stderr)

    def test_legacy_reset_wait_precedes_takeover_and_download(self):
        if not shutil.which("tclsh"):
            self.skipTest("tclsh is unavailable")
        source = (LAYER / "recipes-core/images/files/load-jtag-image.tcl").read_text()
        procedures = source.split('if { [llength $argv] < 2', 1)[0]
        sequence = source.split('puts "Connecting to the Xilinx hw_server"', 1)[1]
        sequence = sequence.split('puts "Downloading FSBL"', 1)[0]
        stubs = """
proc targets {args} {}
proc rst {args} {puts "RESET:$args"}
proc mwr {args} {puts "WRITE:$args"}
proc connect {args} {puts "CONNECT"}
proc disconnect {args} {puts "DISCONNECT"}
proc mask_write {args} {}
proc stop {args} {}
proc con {args} {}
proc dow {args} {puts "DOWNLOAD"}
rename after real_after
proc after {args} {puts "WAIT:$args"}
set HW_IP 127.0.0.1
"""
        with tempfile.TemporaryDirectory() as temporary:
            script = Path(temporary) / "test.tcl"
            for mode, wait in (("por", 5000), ("srst", 2000)):
                for force in (0, 1):
                    with self.subTest(mode=mode, force=force):
                        script.write_text(procedures + stubs +
                                          f"set BOARD_RESET {mode}\nset FORCE_JTAG_BOOT {force}\n" + sequence)
                        result = subprocess.run(["tclsh", str(script)], capture_output=True, text=True)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        reset_wait = f"RESET:-{mode}\nWAIT:{wait}\nDISCONNECT"
                        self.assertIn(reset_wait, result.stdout)
                        self.assertLess(result.stdout.index(reset_wait), result.stdout.index("DOWNLOAD"))
                        if force:
                            self.assertLess(result.stdout.index(reset_wait), result.stdout.index("WRITE:"))
                            self.assertLess(result.stdout.index("RESET:-system"), result.stdout.index("DOWNLOAD"))
                        else:
                            self.assertNotIn("WRITE:", result.stdout)
                            self.assertNotIn("RESET:-system", result.stdout)

    def test_legacy_loader_reset_helper_and_early_validation(self):
        if not shutil.which("tclsh"):
            self.skipTest("tclsh is unavailable")
        source = (LAYER / "recipes-core/images/files/load-jtag-image.tcl").read_text()
        # Evaluate maintained procedure definitions without staging legacy TFTP.
        procedures = source.split('if { [llength $argv] < 2', 1)[0]
        with tempfile.TemporaryDirectory() as temporary:
            script = Path(temporary) / "test.tcl"
            script.write_text(procedures + '''
proc targets {args} {}
proc rst {args} {puts "RESET:$args"; error "unsupported"}
set BOARD_RESET por
if {![catch {pulse_board_reset} message]} {error "expected failure"}
puts $message
''')
            result = subprocess.run(["tclsh", str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("RESET:-por", result.stdout)
            self.assertNotIn("RESET:-srst", result.stdout)
            self.assertIn("Could not pulse cable POR", result.stdout)
            script.write_text('set argv {127.0.0.1 192.0.2.1}\n'
                              'set ::env(MNCOS_JTAG_BOARD_RESET) invalid\n' + source)
            result = subprocess.run(["tclsh", str(script)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("must be srst or por", result.stderr)


if __name__ == "__main__":
    unittest.main()
