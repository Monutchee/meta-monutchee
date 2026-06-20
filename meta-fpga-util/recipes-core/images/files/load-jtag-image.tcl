#!/usr/bin/env xsdb
# Boot an MNCOS image through JTAG and automatically start its TFTP load.
# Run this script from build/export/tftpboot to test xilinx fpga target image, or
# from build/export/jtag-tftpboot to boot a production-flash image.
#
# Usage:
#   ./load-jtag-image.tcl <hw-server-ip> <tftp-server-ip> [board-ip] [target-filter]
#
# If board-ip is omitted, U-Boot obtains it through DHCP before loading files.
# If multiple boards are attached to one hw_server, pass target-filter or set
# MNCOS_HW_TARGET_FILTER to an XSDB targets -filter expression that identifies
# one board, for example: jtag_device_index == 1
# Files fetched by U-Boot are copied to /srv/tftp before JTAG loading starts.
# Set MNCOS_TFTP_ROOT in the host environment to use a different directory.
# Set MNCOS_FORCE_JTAG_BOOT=1 to override the exported board default for boards
# strapped to a non-JTAG boot mode.
# The loader resets the board before using the legacy R5-safe load sequence,
# then automatically starts its TFTP boot.

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

proc is_ipv4 {value} {
    set octets [split $value "."]
    if { [llength $octets] != 4 } {
        return 0
    }

    foreach octet $octets {
        if { ![string is integer -strict $octet] || $octet < 0 || $octet > 255 } {
            return 0
        }
    }

    return 1
}

proc validate_ipv4 {label value} {
    if { ![is_ipv4 $value] } {
        error "$label is not an IPv4 address: $value"
    }
}

proc looks_like_ipv4 {value} {
    expr {[regexp {^[0-9.]+$} $value] && [string first "." $value] >= 0}
}

proc build_target_filter {name_pattern} {
    global TARGET_FILTER

    set name_filter [format {name =~ "%s"} $name_pattern]
    if { [string trim $TARGET_FILTER] eq "" } {
        return $name_filter
    }

    return "($TARGET_FILTER) && ($name_filter)"
}

proc select_target {name_pattern} {
    set filter [build_target_filter $name_pattern]
    if { [catch {targets -set -nocase -filter $filter} message] } {
        error "Could not select target matching {$filter}: $message"
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
    select_target "*PSU*"
    # Update multiboot to zero and force JTAG boot mode for this reset.
    mwr 0xffca0010 0x0
    mwr 0xff5e0200 0x0100
    rst -system
}

proc pulse_board_srst { } {
    select_target "*PSU*"
    puts "Pulsing board SRST through the JTAG cable"
    if { [catch {rst -srst} message] } {
        error "Could not pulse cable SRST: $message"
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

if { [llength $argv] < 2 || [llength $argv] > 4 } {
    error "Usage: ./load-jtag-image.tcl <hw-server-ip> <tftp-server-ip> \[board-ip\] \[target-filter\]"
}

set HW_IP [lindex $argv 0]
set SERVER_IP [lindex $argv 1]
set BUNDLE_DIR [file normalize [pwd]]
set TFTP_ROOT "/srv/tftp"
set FORCE_JTAG_BOOT "@JTAG_LOADER_FORCE_JTAG_BOOT@"
set TARGET_FILTER ""

if { [string match "@*" $FORCE_JTAG_BOOT] && [string match "*@" $FORCE_JTAG_BOOT] } {
    set FORCE_JTAG_BOOT "0"
}

if { [info exists ::env(MNCOS_TFTP_ROOT)] && $::env(MNCOS_TFTP_ROOT) ne "" } {
    set TFTP_ROOT [file normalize $::env(MNCOS_TFTP_ROOT)]
}
if { [info exists ::env(MNCOS_FORCE_JTAG_BOOT)] && [string trim $::env(MNCOS_FORCE_JTAG_BOOT)] ne "" } {
    set FORCE_JTAG_BOOT $::env(MNCOS_FORCE_JTAG_BOOT)
}
set FORCE_JTAG_BOOT [parse_bool "MNCOS_FORCE_JTAG_BOOT" $FORCE_JTAG_BOOT]

if { [info exists ::env(MNCOS_HW_TARGET_FILTER)] && [string trim $::env(MNCOS_HW_TARGET_FILTER)] ne "" } {
    set TARGET_FILTER $::env(MNCOS_HW_TARGET_FILTER)
}

set BOARD_IP ""
if { [llength $argv] == 3 } {
    set third_arg [lindex $argv 2]
    if { [is_ipv4 $third_arg] } {
        set BOARD_IP $third_arg
    } elseif { [looks_like_ipv4 $third_arg] } {
        validate_ipv4 "board IP" $third_arg
    } else {
        set TARGET_FILTER $third_arg
    }
} elseif { [llength $argv] == 4 } {
    set BOARD_IP [lindex $argv 2]
    set TARGET_FILTER [lindex $argv 3]
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


#------------------  Xilinx JTAG Flashing Procedure -------------------------------------------------------

puts "Connecting to the Xilinx hw_server"
connect -url tcp:$HW_IP:3121

if { $TARGET_FILTER ne "" } {
    puts "Using hw_server target filter: $TARGET_FILTER"
}

# Always begin from a board-level SRST. Some boards also need the boot mode
# forced below before the normal JTAG download sequence can run.
if { [catch {pulse_board_srst} message] } {
    error $message
}
after 2000

if { $FORCE_JTAG_BOOT } {
    disconnect
    connect -url tcp:$HW_IP:3121
    if { [catch {switch_to_jtag_boot_mode} message] } {
        error "Could not switch to JTAG boot mode: $message"
    }
    after 2000
}

# Reconnect so the known-good loader starts with fresh target data.
disconnect
connect -url tcp:$HW_IP:3121

# Select the refreshed PSU target and open the JTAG security gates.
select_target "*PSU*"
mask_write 0xFFCA0038 0x1C0 0x1C0

after 500
puts "Downloading PMU firmware"
select_target "*MicroBlaze PMU*"
catch {stop}
dow ./pmufw.elf
con

select_target "*A53*#0"
puts "Resetting A53 processor group before FSBL"
# Reset all A53 cores and their processor-group state. A core-only reset can
# leave the other Linux cores or shared APU state active, causing EDITR/cache
# timeouts on a repeated JTAG load. This does not reset the separate RPU group.
rst -cores -clear-registers
after 500

puts "Downloading FSBL"
dow ./fsbl.elf
con
# Match the Xilinx ZynqMP JTAG boot flow and allow FSBL initialization to
# complete before halting the A53 for the next-stage downloads.
after 4000
stop

puts "Downloading TF-A"
dow ./tfa.elf
con
after 500
stop

dow -data ./system.dtb 0x100000
after 500

# Pass the selected network mode to U-Boot as a text environment block.
# U-Boot consumes these values before boot.scr reuses this DDR area.
download_env_override $SERVER_IP $BOARD_IP 0x20000100
mwr 0x20000004 0x49504f56
mwr 0x20000000 0x4d4e4350

puts "Downloading U-Boot"
dow ./u-boot.elf
after 500

puts "Starting automatic MNCOS TFTP boot"
puts "  hw_server:  $HW_IP"
puts "  TFTP server: $SERVER_IP"
if { $TARGET_FILTER ne "" } {
    puts "  target:      $TARGET_FILTER"
}
if { $BOARD_IP eq "" } {
    puts "  board:       DHCP"
} else {
    puts "  board:       $BOARD_IP (static)"
}
con
