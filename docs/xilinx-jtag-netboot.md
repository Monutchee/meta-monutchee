# Xilinx JTAG network boot

The shared U-Boot JTAG environment imports the loader's TFTP-server address and
optional board address. A supplied board address skips DHCP; otherwise DHCP
must complete before downloads begin. DHCP and each of `Image`, `system.dtb`,
`rootfs.cpio.gz.u-boot` and `boot.scr` receive at most five command attempts,
with a one-second delay between failures. The lwIP U-Boot network stack does
not implement the classic `netretry` setting, so retries are explicit. The
JTAG path also sets `netretry=no` for builds using the classic network stack.
The retry loop guards remaining iterations after success instead of using
`break`, which the pinned Hush parser does not provide. Completed commands
are neither repeated nor reported as failures.

All four transfers must succeed before any script, FDT operation or kernel
handoff. The downloaded script runs with `boot_targets=jtag` and `devtype=jtag`,
which retains its vendor setup but skips media branches. U-Boot then supplies
RAM-root boot arguments (`rdinit=/init`, console and TFTP-server address) and
calls `booti` with the downloaded Image, legacy-header ramdisk and DTB. The
minimal RAM root's `/init` mounts the immutable image and switches to its
`/sbin/mnc-preboot`; it does not depend on a root filesystem label.

Consuming the JTAG marker replaces `bootcmd` in RAM with a recovery message.
An exhausted retry, failed environment import, invalid script/FDT or returned
kernel handoff therefore reaches the U-Boot prompt instead of executing stale
memory or trying an unrelated media/PXE server. No environment is saved; a
normal boot without the marker retains its existing boot command. At the
recovery prompt, inspect the network and artifacts before explicitly running
`mncos_jtag_boot` again or loading another complete bundle.

Run the failure-injection checks with:

```sh
python3 -m unittest discover -s meta-xilinx-addon/tests -p 'test_*.py' -q
```

These checks execute the command lists with shell stubs, including transient
failures and exhaustion at every transfer, DHCP, environment import, script
and FDT failures, and normal-boot isolation. The affected U-Boot/image build
and a JTAG hardware boot remain required to validate the pinned Hush parser,
legacy ramdisk handling and real network behavior.
