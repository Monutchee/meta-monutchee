#!/usr/bin/env xsdb
# Boot an MNCOS image through JTAG while a Provisioning Station serves the
# sibling tftp/ directory. This script never stages or modifies the TFTP root.
#
# Usage:
#   load-jtag-image.tcl <hw-server-url> <tftp-server-ipv4> [board-ipv4]
#
# Example:
#   load-jtag-image.tcl tcp:127.0.0.1:3121 192.168.61.63

proc require_regular_file {path} {
    if { ![file isfile $path] } {
        error "Required JTAG artifact file is missing: $path"
    }
}

proc validate_ipv4 {label value} {
    set octets [split $value "."]
    if { [llength $octets] != 4 } {
        error "$label is not an IPv4 address: $value"
    }

    foreach octet $octets {
        if { ![string is integer -strict $octet] || $octet < 0 || $octet > 255 } {
            error "$label is not an IPv4 address: $value"
        }
    }
}

proc validate_hw_server_url {value} {
    if { ![regexp {^tcp:[^[:space:]]+:[0-9]+$} $value] } {
        error "hw_server URL must use tcp:<host>:<port>: $value"
    }
}

proc parse_bool {label value} {
    set normalized [string tolower [string trim $value]]
    switch -exact -- $normalized {
        1 -
        true -
        yes -
        on {
            return 1
        }
        0 -
        false -
        no -
        off -
        "" {
            return 0
        }
        default {
            error "$label must be one of 0/1, true/false, yes/no, or on/off: $value"
        }
    }
}

proc switch_to_jtag_boot_mode { } {
    puts "Switching ZynqMP boot mode to JTAG before system reset"
    targets -set -nocase -filter {name =~ "*PSU*"}
    mwr 0xffca0010 0x0
    mwr 0xff5e0200 0x0100
    rst -system
}

proc pulse_board_srst { } {
    targets -set -nocase -filter {name =~ "*PSU*"}
    puts "Pulsing board SRST through the JTAG cable"
    if { [catch {rst -srst} message] } {
        error "Could not pulse cable SRST: $message"
    }
}

proc download_env_override {server_ip board_ip address} {
    # Keep generated state out of the extracted artifact so it may be mounted
    # read-only. Tcl chooses the platform's temporary directory.
    set channel [file tempfile path "mnc-jtag-env-"]
    fconfigure $channel -translation binary
    puts $channel "mncos_tftp_serverip=$server_ip"
    if { $board_ip eq "" } {
        puts $channel "mncos_jtag_use_dhcp=yes"
    } else {
        puts $channel "ipaddr=$board_ip"
        puts $channel "mncos_jtag_use_dhcp=no"
    }
    puts -nonewline $channel "\x00"
    close $channel

    if { [catch {dow -data $path $address} message] } {
        file delete -force $path
        error "Could not download JTAG environment override: $message"
    }
    file delete -force $path
}

if { [llength $argv] < 2 || [llength $argv] > 3 } {
    error "Usage: load-jtag-image.tcl <hw-server-url> <tftp-server-ipv4> \[board-ipv4\]"
}

set HW_SERVER_URL [lindex $argv 0]
set SERVER_IP [lindex $argv 1]
set SCRIPT_PATH [file normalize [info script]]
set JTAG_DIR [file dirname $SCRIPT_PATH]
set ARTIFACT_DIR [file dirname $JTAG_DIR]
set TFTP_DIR [file join $ARTIFACT_DIR "tftp"]
set FORCE_JTAG_BOOT "@JTAG_LOADER_FORCE_JTAG_BOOT@"
set MNCOS_JTAG_MAGIC_ADDR 0x1ff00000
set MNCOS_JTAG_ENV_MAGIC_ADDR 0x1ff00004
set MNCOS_JTAG_ENV_ADDR 0x1ff00100

if { [string match "@*" $FORCE_JTAG_BOOT] && [string match "*@" $FORCE_JTAG_BOOT] } {
    set FORCE_JTAG_BOOT "0"
}
if { [info exists ::env(MNCOS_FORCE_JTAG_BOOT)] && [string trim $::env(MNCOS_FORCE_JTAG_BOOT)] ne "" } {
    set FORCE_JTAG_BOOT $::env(MNCOS_FORCE_JTAG_BOOT)
}
set FORCE_JTAG_BOOT [parse_bool "MNCOS_FORCE_JTAG_BOOT" $FORCE_JTAG_BOOT]

if { [llength $argv] > 2 } {
    set BOARD_IP [lindex $argv 2]
} else {
    set BOARD_IP ""
}

validate_hw_server_url $HW_SERVER_URL
validate_ipv4 "TFTP server IP" $SERVER_IP
if { $BOARD_IP ne "" } {
    validate_ipv4 "board IP" $BOARD_IP
}

foreach name {pmufw.elf fsbl.elf tfa.elf u-boot.elf} {
    require_regular_file [file join $JTAG_DIR $name]
}
foreach name {Image system.dtb rootfs.cpio.gz.u-boot boot.scr} {
    require_regular_file [file join $TFTP_DIR $name]
}

puts "Connecting to the Xilinx hw_server at $HW_SERVER_URL"
connect -url $HW_SERVER_URL

if { [catch {pulse_board_srst} message] } {
    error $message
}
after 2000

if { $FORCE_JTAG_BOOT } {
    disconnect
    connect -url $HW_SERVER_URL
    if { [catch {switch_to_jtag_boot_mode} message] } {
        error "Could not switch to JTAG boot mode: $message"
    }
    after 2000
}

disconnect
connect -url $HW_SERVER_URL

targets -set -nocase -filter {name =~ "*PSU*"}
mask_write 0xFFCA0038 0x1C0 0x1C0

after 500
puts "Downloading PMU firmware"
targets -set -nocase -filter {name =~ "*MicroBlaze PMU*"}
catch {stop}
dow [file join $JTAG_DIR "pmufw.elf"]
con

targets -set -nocase -filter {name =~ "*A53*#0"}
puts "Resetting A53 processor group before FSBL"
rst -cores -clear-registers
after 500

puts "Downloading FSBL"
dow [file join $JTAG_DIR "fsbl.elf"]
con
after 4000
stop

puts "Downloading TF-A"
dow [file join $JTAG_DIR "tfa.elf"]
con
after 500
stop

dow -data [file join $TFTP_DIR "system.dtb"] 0x100000
after 500

download_env_override $SERVER_IP $BOARD_IP $MNCOS_JTAG_ENV_ADDR
mwr $MNCOS_JTAG_ENV_MAGIC_ADDR 0x49504f56
mwr $MNCOS_JTAG_MAGIC_ADDR 0x4d4e4350

puts "Downloading U-Boot"
dow [file join $JTAG_DIR "u-boot.elf"]
after 500

puts "Starting automatic MNCOS TFTP boot"
puts "  hw_server:  $HW_SERVER_URL"
puts "  TFTP root:  $TFTP_DIR"
puts "  TFTP server: $SERVER_IP"
if { $BOARD_IP eq "" } {
    puts "  board:       DHCP"
} else {
    puts "  board:       $BOARD_IP (static)"
}
con
