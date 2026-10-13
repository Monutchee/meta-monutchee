"""Execute the environment command lists with injected network failures.

The commands use the common Hush/POSIX shell subset. Bash supplies command
stubs here; the pinned U-Boot image build and board test validate Hush itself.
"""
import os
import re
from pathlib import Path
import shlex
import subprocess
import unittest


ENV = Path(__file__).resolve().parents[1] / 'recipes-bsp/u-boot-xlnx/files/mnc_u-boot-xlnx-addon.env'


class JtagNetbootTests(unittest.TestCase):
    def execute(self, *, failure='', transient=False, dhcp=False, marker=True):
        assignments = '\n'.join(
            key + '=' + shlex.quote(value)
            for line in ENV.read_text().splitlines()
            if line and not line.startswith(('/*', ' *', ' */')) and '=' in line
            for key, value in [line.split('=', 1)]
        )
        harness = r'''
declare -A attempts
setenv() { local name="$1"; shift; printf -v "$name" '%s' "$*"; }
run() { local name; for name in "$@"; do eval "${!name}" || return $?; done; }
itest.l() { test "$MARKER" = yes; }
mw.l() { :; }
env() {
    echo IMPORT
    if test "$FAILURE" = missing_server; then mncos_tftp_serverip=; fi
    test "$FAILURE" != import
}
sleep() { echo DELAY; }
dhcp() {
    attempts[dhcp]=$(( ${attempts[dhcp]:-0} + 1 )); echo "DHCP ${attempts[dhcp]}"
    serverip=wrong-dhcp-server
    if test "$FAILURE" = dhcp; then return 1; fi
    if test "$TRANSIENT" = yes && test "${attempts[dhcp]}" -lt 3; then return 1; fi
}
tftpboot() {
    local name="$2"
    attempts[$name]=$(( ${attempts[$name]:-0} + 1 ))
    echo "TRANSFER $name ${attempts[$name]} server=$serverip"
    if test "$FAILURE" = "$name"; then return 1; fi
    if test "$TRANSIENT" = yes && test "${attempts[$name]}" -lt 3; then return 1; fi
}
source() {
    echo "SOURCE $* targets=$boot_targets devtype=$devtype"
    test "$FAILURE" != script
}
fdt() { echo "FDT $*"; test "$FAILURE" != fdt; }
booti() { echo "BOOTI $* args=$bootargs"; test "$FAILURE" != kernel; }
bootcmd='echo GENERIC_FALLBACK'
mncos_tftp_serverip=172.30.19.20
ipaddr=172.30.19.99
run mncos_jtag_preboot
echo "PREBOOT_STATUS=$?"
run bootcmd
'''
        result = subprocess.run(
            ['bash', '-c', assignments + ('\n' if dhcp else '\nmncos_jtag_use_dhcp=no\n') + harness], capture_output=True,
            text=True, env={**os.environ, 'FAILURE': failure,
                           'TRANSIENT': 'yes' if transient else 'no',
                           'MARKER': 'yes' if marker else 'no'},
        )
        self.assertEqual(result.stderr, '')
        return result.stdout

    def test_retry_uses_supported_hush_commands(self):
        retry = next(line for line in ENV.read_text().splitlines()
                     if line.startswith('mncos_net_retry='))
        # The pinned Hush parser supports loops but has no break/continue command.
        self.assertIsNone(re.search(r'\b(?:break|continue)\b', retry))

    def test_first_success_skips_remaining_network_calls_and_failure_logs(self):
        for dhcp in [False, True]:
            with self.subTest(dhcp=dhcp):
                out = self.execute(dhcp=dhcp)
                for name in ['Image', 'system.dtb', 'rootfs.cpio.gz.u-boot', 'boot.scr']:
                    self.assertEqual(out.count('TRANSFER ' + name + ' '), 1)
                self.assertEqual(sum(line.startswith('DHCP ') for line in out.splitlines()),
                                 1 if dhcp else 0)
                self.assertNotIn('network attempt ', out)
                self.assertNotIn('DELAY', out)
                self.assertIn('BOOTI ', out)

    def test_static_transient_failures_retry_then_handoff(self):
        out = self.execute(transient=True)
        self.assertNotIn('DHCP ', out)
        self.assertEqual(out.count('network attempt '), 8)
        self.assertEqual(out.count('DELAY'), 8)
        for name in ['Image', 'system.dtb', 'rootfs.cpio.gz.u-boot', 'boot.scr']:
            self.assertEqual(out.count('TRANSFER ' + name + ' '), 3)
        self.assertLess(out.index('TRANSFER boot.scr 3'), out.index('SOURCE '))
        self.assertIn('SOURCE 0x20000000 targets=jtag devtype=jtag', out)
        self.assertIn('BOOTI 0x00200000 0x04000000 0x00100000', out)
        self.assertIn('rdinit=/init ro mncos.tftp_server=172.30.19.20', out)
        self.assertNotIn('GENERIC_FALLBACK', out)

    def test_each_required_transfer_exhaustion_stops_before_source_and_fdt(self):
        names = ['Image', 'system.dtb', 'rootfs.cpio.gz.u-boot', 'boot.scr']
        for index, name in enumerate(names):
            with self.subTest(name=name):
                out = self.execute(failure=name)
                self.assertEqual(out.count('TRANSFER ' + name + ' '), 5)
                self.assertEqual(out.count('DELAY'), 4)
                for later in names[index + 1:]:
                    self.assertNotIn('TRANSFER ' + later + ' ', out)
                for forbidden in ['SOURCE ', 'FDT ', 'BOOTI ', 'GENERIC_FALLBACK']:
                    self.assertNotIn(forbidden, out)
                self.assertIn('PREBOOT_STATUS=1', out)

    def test_dhcp_retries_and_restores_station_server(self):
        out = self.execute(dhcp=True, transient=True)
        self.assertEqual(out.count('DHCP '), 3)
        self.assertIn('server=172.30.19.20', out)
        self.assertNotIn('server=wrong-dhcp-server', out)
        self.assertIn('BOOTI ', out)

    def test_dhcp_exhaustion_and_import_failure_disable_fallback(self):
        for failure in ['dhcp', 'import', 'missing_server']:
            with self.subTest(failure=failure):
                out = self.execute(dhcp=True, failure=failure)
                if failure == 'dhcp':
                    self.assertEqual(sum(line.startswith('DHCP ') for line in out.splitlines()), 5)
                self.assertNotIn('TRANSFER ', out)
                self.assertNotIn('GENERIC_FALLBACK', out)
                self.assertIn('PREBOOT_STATUS=1', out)

    def test_invalid_script_or_dtb_never_calls_kernel(self):
        for failure in ['script', 'fdt']:
            with self.subTest(failure=failure):
                out = self.execute(failure=failure)
                self.assertNotIn('BOOTI ', out)
                self.assertNotIn('GENERIC_FALLBACK', out)
                self.assertIn('PREBOOT_STATUS=1', out)

    def test_normal_boot_keeps_generic_bootcmd(self):
        out = self.execute(marker=False)
        self.assertIn('GENERIC_FALLBACK', out)
        self.assertNotIn('TRANSFER ', out)
        self.assertNotIn('IMPORT', out)

    def test_returned_kernel_handoff_stays_in_recovery(self):
        out = self.execute(failure='kernel')
        self.assertIn('BOOTI ', out)
        self.assertIn('PREBOOT_STATUS=1', out)
        self.assertIn('recovery prompt', out)
        self.assertNotIn('GENERIC_FALLBACK', out)


if __name__ == '__main__':
    unittest.main()
