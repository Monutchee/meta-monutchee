#!/usr/bin/env xsdb
# Boot an MNCOS image through JTAG and automatically start its TFTP load.
# Run this script from build/export/tftpboot to test mncos-image-minimal, or
# from build/export/jtag-tftpboot to boot the ZUBoard production-flash image.
#
# Usage:
#   ./load-jtag-image.tcl <hw-server-ip> <tftp-server-ip> [board-ip]
#
# If board-ip is omitted, U-Boot obtains it through DHCP before loading files.
# Files fetched by U-Boot are copied to /srv/tftp before JTAG loading starts.
# Set MNCOS_TFTP_ROOT in the host environment to use a different directory.

proc require_bundle_file {bundle_dir name} {
    set path [file join $bundle_dir $name]
    if { ![file isfile $path] } {
        error "Required JTAG bundle file is missing: $path"
    }
}

proc stage_tftp_files {bundle_dir tftp_root} {
    set required_files {
        Image
        system.dtb
        rootfs.cpio.gz.u-boot
        boot.scr
    }
    set optional_files {
        target.wic.xz
        target.wic.xz.sha256
    }

    foreach name $required_files {
        require_bundle_file $bundle_dir $name
    }

    if { ![file exists $tftp_root] } {
        if { [catch {file mkdir $tftp_root} message] } {
            error "Could not create TFTP directory $tftp_root: $message"
        }
    }
    if { ![file isdirectory $tftp_root] } {
        error "TFTP path is not a directory: $tftp_root"
    }
    puts "Staging TFTP files in $tftp_root"
    foreach name [concat $required_files $optional_files] {
        set source [file join $bundle_dir $name]
        if { ![file isfile $source] } {
            continue
        }

        set destination [file join $tftp_root $name]
        if { [file normalize $source] ne [file normalize $destination] } {
            if { [catch {file copy -force $source $destination} message] } {
                error "Could not copy $name to $tftp_root: $message"
            }
        }
        puts "  $name"
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

proc download_env_override {server_ip board_ip address} {
    set path [file join [pwd] ".mncos-jtag-env-[pid].txt"]
    set channel [open $path "wb"]
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
    error "Usage: ./load-jtag-image.tcl <hw-server-ip> <tftp-server-ip> \[board-ip\]"
}

set HW_IP [lindex $argv 0]
set SERVER_IP [lindex $argv 1]
set BUNDLE_DIR [file normalize [pwd]]
set TFTP_ROOT "/srv/tftp"

if { [info exists ::env(MNCOS_TFTP_ROOT)] && $::env(MNCOS_TFTP_ROOT) ne "" } {
    set TFTP_ROOT [file normalize $::env(MNCOS_TFTP_ROOT)]
}

if { [llength $argv] > 2 } {
    set BOARD_IP [lindex $argv 2]
} else {
    set BOARD_IP ""
}

validate_ipv4 "hw_server IP" $HW_IP
validate_ipv4 "TFTP server IP" $SERVER_IP
if { $BOARD_IP ne "" } {
    validate_ipv4 "board IP" $BOARD_IP
}

foreach name {pmufw.elf fsbl.elf tfa.elf u-boot.elf system.dtb} {
    require_bundle_file $BUNDLE_DIR $name
}
stage_tftp_files $BUNDLE_DIR $TFTP_ROOT

puts "Connecting to the Xilinx hw_server"
connect -url tcp:$HW_IP:3121

targets -set -nocase -filter {name =~ "*PSU*"}
mask_write 0xFFCA0038 0x1C0 0x1C0

after 500
puts "Downloading PMU firmware"
targets -set -nocase -filter {name =~ "*MicroBlaze PMU*"}
catch {stop}
dow ./pmufw.elf
con

targets -set -nocase -filter {name =~ "*A53*#0"}
puts "Resetting A53 processor"
rst -processor -clear-registers
after 500

puts "Downloading FSBL"
dow ./fsbl.elf
con
after 500
stop

puts "Downloading TF-A"
dow ./tfa.elf
con
after 500
stop

dow -data ./system.dtb 0x100000
after 500

# Pass the selected network mode to U-Boot as a text environment block. When
# BOARD_IP is empty, U-Boot runs DHCP with autoload disabled and then restores
# the requested TFTP server address before loading files.
download_env_override $SERVER_IP $BOARD_IP 0x20000100
mwr 0x20000004 0x49504f56

# MNCOS_JTAG_MAGIC ("MNCP"). U-Boot consumes and clears this marker during
# preboot, imports the optional addresses above, then runs tftpbootstep. This
# memory is reused by boot.scr after the import is complete.
mwr 0x20000000 0x4d4e4350

puts "Downloading U-Boot"
dow ./u-boot.elf
after 500

puts "Starting automatic MNCOS TFTP boot"
puts "  hw_server:  $HW_IP"
puts "  TFTP server: $SERVER_IP"
if { $BOARD_IP eq "" } {
    puts "  board:       DHCP"
} else {
    puts "  board:       $BOARD_IP (static)"
}
con
