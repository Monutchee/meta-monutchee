#!/usr/bin/env xsdb
# Boot an MNCOS image through JTAG and automatically start its TFTP load.
# Run this script from build/export/tftpboot to test xilinx fpga target image, or
# from build/export/jtag-tftpboot to boot a production-flash image.
#
# Usage:
#   ./load-jtag-image.tcl <hw-server-ip> <tftp-server-ip> [board-ip]
#
# If board-ip is omitted, U-Boot obtains it through DHCP before loading files.
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

# Resolve the FSBL status targets on the same physical device.
proc fsbl_target {pattern} {
    set matches {}
    foreach target [targets -target-properties -nocase -filter \
            [format {name =~ "%s"} $pattern]] {
        if { [dict exists $target name] &&
             [string match -nocase $pattern [dict get $target name]] } {
            lappend matches $target
        }
    }
    if { [llength $matches] != 1 } {
        error "expected one $pattern FSBL target, found [llength $matches]"
    }
    return [lindex $matches 0]
}

# Error lockdown also reaches XFsbl_Exit. Require the success marker there,
# and always restore the same physically bound A53 before breakpoint cleanup.
proc require_fsbl_success {} {
    set core [fsbl_target "*A53*#0"]
    set psu [fsbl_target "*PSU*"]
    foreach key {jtag_cable_ctx jtag_device_index} {
        if { ![dict exists $core $key] || ![dict exists $psu $key] ||
             [dict get $core $key] ne [dict get $psu $key] } {
            error "cannot bind FSBL status PSU and A53 using $key"
        }
    }
    set core_id [dict get $core target_id]
    set status [catch {
        targets [dict get $psu target_id]
        set value [mrd -value 0xffd80060]
        if { ![string is entier -strict $value] || $value != 0 } {
            error "FSBL did not report successful completion: $value"
        }
    } message options]
    set restore_status [catch {targets $core_id} restore_message restore_options]
    if { $status } {
        if { $restore_status } {
            puts "FSBL status target restoration failed: $restore_message"
        }
        return -options $options $message
    }
    if { $restore_status } {
        return -options $restore_options $restore_message
    }
}

# AMD's ZynqMP download flow stops at XFsbl_Exit before its parking loop.
# FSBL_STATUS == 0 alone is not completion proof: it is also the reset value,
# and psu_init runs before FSBL marks itself running.
proc run_fsbl_to_exit {} {
    set breakpoint ""
    set status [catch {
        set exit_address [dict get [print -dict &XFsbl_Exit] &XFsbl_Exit]
        if { ![string is entier -strict $exit_address] || $exit_address <= 0 ||
             ($exit_address & 3) != 0 } {
            error "Invalid XFsbl_Exit symbol address: $exit_address"
        }
        set breakpoint [bpadd -addr &XFsbl_Exit]
        con -block -timeout 60
        # XSDB -nvlist returns register values as hexadecimal without 0x.
        set pc_hex [dict get [rrd -nvlist pc] pc]
        if { ![regexp {^[0-9a-fA-F]+$} $pc_hex] } {
            error "Invalid FSBL completion PC: $pc_hex"
        }
        set pc [expr "0x$pc_hex"]
        if { $pc != $exit_address } {
            error [format "FSBL halted at 0x%x, expected XFsbl_Exit at 0x%x" $pc $exit_address]
        }
        require_fsbl_success
        puts [format "FSBL reached XFsbl_Exit at 0x%x with successful status" $pc]
    } message options]
    set cleanup_status 0
    if { $breakpoint ne "" } {
        set cleanup_status [catch {bpremove $breakpoint} cleanup_message cleanup_options]
    }
    if { $status || $cleanup_status } {
        if { $status } {
            if { $cleanup_status } {
                puts "FSBL breakpoint cleanup failed: $cleanup_message"
            }
            return -options $options $message
        }
        return -options $cleanup_options $cleanup_message
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
    # Update multiboot to zero and force JTAG boot mode for this reset.
    mwr 0xffca0010 0x0
    mwr 0xff5e0200 0x0100
    rst -system
}

proc pulse_board_reset { } {
    global BOARD_RESET
    targets -set -nocase -filter {name =~ "*PSU*"}
    puts "Pulsing board [string toupper $BOARD_RESET] through the JTAG cable"
    if { [catch {rst -$BOARD_RESET} message] } {
        error "Could not pulse cable [string toupper $BOARD_RESET]: $message"
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
set FORCE_JTAG_BOOT "@JTAG_LOADER_FORCE_JTAG_BOOT@"
set BOARD_RESET "@JTAG_LOADER_BOARD_RESET@"
set MNCOS_JTAG_MAGIC_ADDR 0x1ff00000
set MNCOS_JTAG_ENV_MAGIC_ADDR 0x1ff00004
set MNCOS_JTAG_ENV_ADDR 0x1ff00100

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

if { [string match "@*" $BOARD_RESET] && [string match "*@" $BOARD_RESET] } {
    set BOARD_RESET "srst"
}
if { [info exists ::env(MNCOS_JTAG_BOARD_RESET)] } {
    set BOARD_RESET $::env(MNCOS_JTAG_BOARD_RESET)
}
if { $BOARD_RESET ni {srst por} } {
    error "MNCOS_JTAG_BOARD_RESET must be srst or por: $BOARD_RESET"
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


#------------------  Xilinx JTAG Flashing Procedure -------------------------------------------------------

puts "Connecting to the Xilinx hw_server"
connect -url tcp:$HW_IP:3121

# Always begin from the configured board-level cable reset. Some boards also need the boot mode
# forced below before the normal JTAG download sequence can run.
if { [catch {pulse_board_reset} message] } {
    error $message
}
# Allow autonomous POR boot to settle before reconnect and JTAG takeover.
if { $BOARD_RESET eq "por" } {
    after 5000
} else {
    after 2000
}

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
targets -set -nocase -filter {name =~ "*PSU*"}
mask_write 0xFFCA0038 0x1C0 0x1C0

after 500
puts "Downloading PMU firmware"
targets -set -nocase -filter {name =~ "*MicroBlaze PMU*"}
catch {stop}
dow ./pmufw.elf
con

targets -set -nocase -filter {name =~ "*A53*#0"}
puts "Resetting A53 processor group before FSBL"
# Reset all A53 cores and their processor-group state. A core-only reset can
# leave the other Linux cores or shared APU state active, causing EDITR/cache
# timeouts on a repeated JTAG load. This does not reset the separate RPU group.
rst -cores -clear-registers
after 500

puts "Downloading FSBL"
dow ./fsbl.elf
run_fsbl_to_exit

puts "Downloading TF-A"
dow ./tfa.elf
con
after 500
stop

dow -data ./system.dtb 0x100000
after 500

# Pass the selected network mode to U-Boot as a text environment block.
# U-Boot consumes these values before boot.scr reuses this DDR area.
download_env_override $SERVER_IP $BOARD_IP $MNCOS_JTAG_ENV_ADDR
mwr $MNCOS_JTAG_ENV_MAGIC_ADDR 0x49504f56
mwr $MNCOS_JTAG_MAGIC_ADDR 0x4d4e4350

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
