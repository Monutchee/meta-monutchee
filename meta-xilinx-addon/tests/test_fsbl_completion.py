"""Exercise production FSBL completion and failure handling without hardware."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

FILES = Path(__file__).resolve().parents[1] / 'recipes-core/images/files'


@unittest.skipUnless(shutil.which('tclsh'), 'tclsh is unavailable')
class FsblCompletionTests(unittest.TestCase):
    def execute(self, station, scenario):
        source = (FILES / ('load-jtag-image-station.tcl' if station else
                           'load-jtag-image.tcl')).read_text()
        helpers = source.split('proc fsbl_target', 1)[1].split(
            'proc parse_bool', 1)[0]
        harness = r'''
set scenario SCENARIO
set station STATION
set TARGET_ID 1
set selected 3
set selections {}
set operations {}
set reads {}
set properties [list \
 [dict create target_id 1 name PSU jtag_cable_ctx cable-a jtag_device_index 1] \
 [dict create target_id 3 name {Cortex-A53 #0} jtag_cable_ctx cable-a jtag_device_index 1]]
if {$station || $scenario eq "ambiguous"} {
 lappend properties [dict create target_id 11 name PSU jtag_cable_ctx cable-b jtag_device_index 1]
 lappend properties [dict create target_id 13 name {Cortex-A53 #0} jtag_cable_ctx cable-b jtag_device_index 1]
}
if {$scenario eq "mismatched-device"} {
 set psu [lindex $properties 0]
 dict set psu jtag_device_index 9
 lset properties 0 $psu
}
proc target_matches_selected_hardware {target scope} {
 return [expr {[dict get $target jtag_cable_ctx] eq "cable-a"}]
}
proc targets {args} {
 if {[lsearch -exact $args -target-properties] >= 0} {return $::properties}
 if {[llength $args] != 1} {error "unexpected selection $args"}
 set id [lindex $args 0]
 lappend ::selections $id
 if {$id == 3 && $::scenario eq "status-restore-failure"} {
  error "status target restore failed"
 }
 set ::selected $id
}
proc select_target {args} {set ::selected 3}
proc mrd {args} {
 if {$::selected != 1} {error "wrong physical target"}
 if {[llength $args] != 2 || [lindex $args 0] ne "-value"} {error "unexpected read"}
 lappend ::reads [lindex $args 1]
 if {[lindex $args 1] eq "0xffd80060"} {
  if {[lsearch -glob $::operations "rrd:*"] < 0} {error "read unavailable"}
  if {$::scenario eq "status-read-failure"} {error "status read failed"}
  if {$::scenario eq "error-exit"} {return 0x1234}
  if {$::scenario eq "invalid-status"} {return N/A}
  return 0x00000000
 }
 return 0x1234
}
proc print {args} {
 lappend ::operations "symbol:$args"
 if {$args ne "-dict &XFsbl_Exit"} {error "unexpected symbol lookup"}
 if {$::scenario eq "missing-symbol"} {error "symbol unavailable"}
 if {$::scenario eq "invalid-symbol"} {return [dict create &XFsbl_Exit N/A]}
 return [dict create &XFsbl_Exit 4294708232]
}
proc bpadd {args} {
 lappend ::operations "bpadd:$args"
 if {$args ne "-addr &XFsbl_Exit"} {error "unexpected breakpoint"}
 if {$::scenario eq "bpadd-failure"} {error "breakpoint unavailable"}
 return 7
}
proc bpremove {args} {
 lappend ::operations "bpremove:$args"
 if {$args ne "7"} {error "removed another client's breakpoint"}
 if {$::scenario in {cleanup-failure timeout-cleanup-failure}} {
  return -code error -errorcode {CLEANUP ORIGINAL} "cleanup failed"
 }
}
proc con {args} {
 if {$::selected != 3} {error "resumed wrong target"}
 if {$args ne "-block -timeout 60"} {error "unbounded continue"}
 lappend ::operations "con:$args"
 if {$::scenario ni {success wrong-pc invalid-pc pc-read-failure cleanup-failure error-exit invalid-status status-read-failure status-restore-failure mismatched-device ambiguous}} {
  return -code error -errorcode {WAIT ORIGINAL} -errorinfo ORIGINAL_STACK "original completion timeout"
 }
}
proc rrd {args} {
 lappend ::operations "rrd:$args"
 if {$args ne "-nvlist pc"} {error "unexpected register read"}
 if {$::scenario eq "pc-read-failure"} {error "PC unavailable"}
 if {$::scenario eq "wrong-pc"} {return [dict create pc 00000000fffc0c6c]}
 if {$::scenario eq "invalid-pc"} {return [dict create pc N/A]}
 return [dict create pc 00000000fffc0c08]
}
proc stop {args} {error "unexpected asynchronous halt"}
proc after {args} {error "unexpected fixed wait"}
foreach forbidden {mwr mask_write rst dow disconnect connect} {
 proc $forbidden {args} {error "unexpected boot mutation"}
}
HELPERS
proc boundary {} {
run_fsbl_to_exit
}
set code [catch {boundary} message options]
puts "RESULT:$code:$message"
if {$code} {
 puts "ERRORCODE:[dict get $options -errorcode]"
 puts "STACK:[dict get $options -errorinfo]"
}
puts "OPERATIONS:$operations"
puts "SELECTIONS:$selections"
puts "READS:$reads"
puts "SELECTED:$selected"
'''.replace('SCENARIO', scenario).replace('STATION', str(int(station))).replace(
            'HELPERS', 'proc fsbl_target' + helpers)
        with tempfile.TemporaryDirectory() as temporary:
            script = Path(temporary) / 'completion.tcl'
            script.write_text(harness)
            result = subprocess.run(['tclsh', str(script)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout

    def test_success_requires_exit_pc_and_removes_only_owned_breakpoint(self):
        for station in (False, True):
            with self.subTest(station=station):
                output = self.execute(station, 'success')
                self.assertIn('RESULT:0:', output)
                self.assertIn('FSBL reached XFsbl_Exit at 0xfffc0c08', output)
                self.assertIn('OPERATIONS:{symbol:-dict &XFsbl_Exit} {bpadd:-addr &XFsbl_Exit} {con:-block -timeout 60} {rrd:-nvlist pc} bpremove:7', output)
                self.assertIn('READS:0xffd80060\n', output)
                self.assertIn('SELECTIONS:1 3\n', output)
                self.assertIn('SELECTED:3', output)

    def test_completion_timeout_preserves_exception_and_cleans_up(self):
        for station in (False, True):
            with self.subTest(station=station):
                output = self.execute(station, 'timeout')
                self.assertIn('RESULT:1:original completion timeout', output)
                self.assertIn('ERRORCODE:WAIT ORIGINAL', output)
                self.assertIn('STACK:ORIGINAL_STACK', output)
                self.assertIn('bpremove:7', output)
                self.assertNotIn('rrd:', output)
                self.assertIn('READS:\n', output)
                self.assertIn('SELECTIONS:\n', output)
                self.assertIn('SELECTED:3', output)

    def test_unrelated_halt_or_unavailable_pc_aborts_and_cleans_up(self):
        for station in (False, True):
            for scenario, message in (
                    ('wrong-pc', 'FSBL halted at 0xfffc0c6c, expected XFsbl_Exit at 0xfffc0c08'),
                    ('invalid-pc', 'Invalid FSBL completion PC: N/A'),
                    ('pc-read-failure', 'PC unavailable')):
                with self.subTest(station=station, scenario=scenario):
                    output = self.execute(station, scenario)
                    self.assertIn('RESULT:1:' + message, output)
                    self.assertIn('bpremove:7', output)

    def test_exit_requires_readable_success_status_and_restored_core(self):
        for station in (False, True):
            for scenario, message in (
                    ('error-exit', 'FSBL did not report successful completion: 0x1234'),
                    ('invalid-status', 'FSBL did not report successful completion: N/A'),
                    ('status-read-failure', 'status read failed'),
                    ('status-restore-failure', 'status target restore failed')):
                with self.subTest(station=station, scenario=scenario):
                    output = self.execute(station, scenario)
                    self.assertIn('RESULT:1:' + message, output)
                    self.assertIn('bpremove:7', output)
                    self.assertNotIn('with successful status', output)
                    if scenario != 'status-restore-failure':
                        self.assertIn('SELECTED:3', output)

    def test_symbol_or_breakpoint_failure_prevents_resume(self):
        for station in (False, True):
            for scenario in ('missing-symbol', 'invalid-symbol', 'bpadd-failure'):
                with self.subTest(station=station, scenario=scenario):
                    output = self.execute(station, scenario)
                    self.assertIn('RESULT:1:', output)
                    self.assertNotIn('con:', output)
                    self.assertNotIn('bpremove:', output)

    def test_cleanup_failure_aborts_or_preserves_primary_exception(self):
        for station in (False, True):
            with self.subTest(station=station):
                output = self.execute(station, 'cleanup-failure')
                self.assertIn('RESULT:1:cleanup failed', output)
                self.assertIn('ERRORCODE:CLEANUP ORIGINAL', output)
                output = self.execute(station, 'timeout-cleanup-failure')
                self.assertIn('RESULT:1:original completion timeout', output)
                self.assertIn('ERRORCODE:WAIT ORIGINAL', output)
                self.assertIn('STACK:ORIGINAL_STACK', output)
                self.assertIn('FSBL breakpoint cleanup failed: cleanup failed', output)

    def test_mismatched_device_does_not_select_or_read(self):
        for station in (False, True):
            with self.subTest(station=station):
                output = self.execute(station, 'mismatched-device')
                self.assertIn('cannot bind FSBL status PSU and A53', output)
                self.assertIn('READS:\n', output)
                self.assertIn('SELECTIONS:\n', output)
                self.assertIn('RESULT:1:', output)
                self.assertIn('bpremove:7', output)

    def test_legacy_ambiguous_hardware_does_not_select_or_read(self):
        output = self.execute(False, 'ambiguous')
        self.assertIn('expected one *A53*#0 FSBL target, found 2', output)
        self.assertIn('READS:\n', output)
        self.assertIn('SELECTIONS:\n', output)
        self.assertIn('RESULT:1:', output)
        self.assertIn('bpremove:7', output)


if __name__ == '__main__':
    unittest.main()
