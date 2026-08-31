#!/usr/bin/env xsdb
# Boot an MNCOS image through JTAG while a Provisioning Station serves the
# sibling tftp/ directory. This script never stages or modifies the TFTP root.
# MNC_STATION_TARGET_SELECTOR_V1
# MNC_STATION_TARGET_SELECTOR_V2
# MNC_STATION_TARGET_SELECTOR_V3
#
# Usage:
#   load-jtag-image.tcl <hw-server-url> <tftp-server-ipv4> [board-ipv4]
#                       [target-id] [target-cable-serial] [target-device-index]
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

proc initialize_target_selector {target_id cable_serial device_index} {
    global TARGET_ID TARGET_CABLE_CTX TARGET_CABLE_SERIAL TARGET_DEVICE_INDEX

    if { $target_id eq "" && $cable_serial eq "" } {
        return
    }
    set selected {}
    set observed {}
    for {set attempt 1} {$attempt <= 20} {incr attempt} {
        set selected {}
        set observed {}
        foreach target [targets -target-properties -nocase -filter {name =~ "*PSU*"}] {
            if { ![dict exists $target name] ||
                 ![string match -nocase "*PSU*" [dict get $target name]] } {
                continue
            }
            set observed_id "?"
            set observed_serial "?"
            set observed_index "?"
            if { [dict exists $target target_id] } {
                set observed_id [dict get $target target_id]
            }
            if { [dict exists $target jtag_cable_serial] } {
                set observed_serial [dict get $target jtag_cable_serial]
            }
            if { [dict exists $target jtag_device_index] } {
                set observed_index [dict get $target jtag_device_index]
            }
            lappend observed "id=$observed_id,cable=$observed_serial,device=$observed_index"

            if { $cable_serial ne "" } {
                if { ![dict exists $target jtag_cable_serial] ||
                     [dict get $target jtag_cable_serial] ne $cable_serial } {
                    continue
                }
                if { $device_index ne "" &&
                     (![dict exists $target jtag_device_index] ||
                      [dict get $target jtag_device_index] ne $device_index) } {
                    continue
                }
                lappend selected $target
            } elseif { [dict exists $target target_id] &&
                       [dict get $target target_id] eq $target_id } {
                lappend selected $target
            }
        }
        if { [llength $selected] == 1 || [llength $selected] > 1 } {
            break
        }
        if { $attempt < 20 } {
            puts "Selected JTAG device is not visible yet; retrying target discovery ($attempt/20)"
            after 500
        }
    }
    if { [llength $selected] != 1 } {
        set available "none"
        if { [llength $observed] > 0 } {
            set available [join $observed "; "]
        }
        if { $cable_serial ne "" } {
            set suffix ""
            if { $device_index ne "" } {
                set suffix ", device $device_index"
            }
            error "Expected one ZynqMP PSU target on JTAG cable $cable_serial$suffix, found [llength $selected] (available PSU targets: $available)"
        }
        error "XSDB target $target_id is not one ZynqMP PSU target (available PSU targets: $available); scan devices again"
    }

    set target [lindex $selected 0]
    set TARGET_ID [dict get $target target_id]
    if { ![dict exists $target jtag_device_index] } {
        error "XSDB target $TARGET_ID has no JTAG device index"
    }
    set TARGET_DEVICE_INDEX [dict get $target jtag_device_index]
    if { [dict exists $target jtag_cable_serial] } {
        set TARGET_CABLE_SERIAL [dict get $target jtag_cable_serial]
    }
    if { [dict exists $target jtag_cable_ctx] } {
        set TARGET_CABLE_CTX [dict get $target jtag_cable_ctx]
    }
    if { $TARGET_CABLE_SERIAL eq "" && $TARGET_CABLE_CTX eq "" } {
        error "XSDB target $TARGET_ID has no identifiable JTAG cable"
    }

    set cable $TARGET_CABLE_SERIAL
    if { $cable eq "" } {
        set cable $TARGET_CABLE_CTX
    }
    puts "Selected XSDB target $TARGET_ID on JTAG cable $cable, device $TARGET_DEVICE_INDEX"
}

proc target_matches_selected_hardware {target scope} {
    global TARGET_CABLE_CTX TARGET_CABLE_SERIAL TARGET_DEVICE_INDEX

    # ZynqMP exposes the PSU/APU through the ARM DAP and the PMU through a
    # separate device in the same JTAG cable. Device scope is correct for PSU
    # and A53 targets; cable scope is required for the MicroBlaze PMU target.
    if { $TARGET_CABLE_SERIAL ne "" } {
        set cable_matches [expr {
            [dict exists $target jtag_cable_serial] &&
            [dict get $target jtag_cable_serial] eq $TARGET_CABLE_SERIAL
        }]
    } else {
        set cable_matches [expr {
            [dict exists $target jtag_cable_ctx] &&
            [dict get $target jtag_cable_ctx] eq $TARGET_CABLE_CTX
        }]
    }
    if { !$cable_matches || $scope eq "cable" } {
        return $cable_matches
    }
    return [expr {
        [dict exists $target jtag_device_index] &&
        [dict get $target jtag_device_index] eq $TARGET_DEVICE_INDEX
    }]
}

proc summarize_targets {target_list} {
    set summaries {}
    foreach target $target_list {
        set target_id "?"
        set target_name "?"
        set cable_serial "?"
        set device_index "?"
        if { [dict exists $target target_id] } {
            set target_id [dict get $target target_id]
        }
        if { [dict exists $target name] } {
            set target_name [dict get $target name]
        }
        if { [dict exists $target jtag_cable_serial] } {
            set cable_serial [dict get $target jtag_cable_serial]
        }
        if { [dict exists $target jtag_device_index] } {
            set device_index [dict get $target jtag_device_index]
        }
        lappend summaries "id=$target_id,name=$target_name,cable=$cable_serial,device=$device_index"
    }
    if { [llength $summaries] == 0 } {
        return "none"
    }
    return [join $summaries "; "]
}

proc select_target {name_pattern {scope "device"}} {
    global TARGET_ID

    if { $scope ne "device" && $scope ne "cable" } {
        error "Invalid target selection scope: $scope"
    }

    set matches {}
    set observed {}
    set query_error ""
    for {set attempt 1} {$attempt <= 20} {incr attempt} {
        set matches {}
        set observed {}
        set query_error ""
        if { [catch {
            set observed [targets -target-properties -nocase -filter \
                [format {name =~ "%s"} $name_pattern]]
        } message] } {
            set query_error $message
        } else {
            foreach target $observed {
                if { ![dict exists $target name] ||
                     ![string match -nocase $name_pattern [dict get $target name]] } {
                    continue
                }
                if { $TARGET_ID eq "" ||
                     [target_matches_selected_hardware $target $scope] } {
                    lappend matches $target
                }
            }
            if { [llength $matches] == 1 } {
                set selected_id [dict get [lindex $matches 0] target_id]
                if { ![catch {targets $selected_id} message] } {
                    return $selected_id
                }
                set query_error "could not select XSDB target $selected_id: $message"
            }
        }

        if { $attempt < 20 } {
            puts "Waiting for $name_pattern target on selected JTAG $scope ($attempt/20)"
            after 500
        }
    }

    set suffix ""
    if { $query_error ne "" } {
        set suffix "; last XSDB error: $query_error"
    }
    error "Expected one $name_pattern target on selected JTAG $scope, found [llength $matches] (available matching targets: [summarize_targets $observed])$suffix"
}

proc download_elf_with_retry {label name_pattern path {scope "device"}} {
    set last_message ""
    puts $label
    for {set attempt 1} {$attempt <= 3} {incr attempt} {
        select_target $name_pattern $scope
        catch {stop}
        if { ![catch {dow $path} message] } {
            return
        }
        set last_message $message
        if { $attempt < 3 } {
            puts "$label failed; retrying download ($attempt/3): $message"
            after 500
        }
    }
    error "$label failed after 3 attempts: $last_message"
}

proc download_data_with_retry {label name_pattern path address {scope "device"}} {
    set last_message ""
    puts $label
    for {set attempt 1} {$attempt <= 3} {incr attempt} {
        select_target $name_pattern $scope
        if { ![catch {dow -data $path $address} message] } {
            return
        }
        set last_message $message
        if { $attempt < 3 } {
            puts "$label failed; retrying download ($attempt/3): $message"
            after 500
        }
    }
    error "$label failed after 3 attempts: $last_message"
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

    set status [catch {
        download_data_with_retry "Downloading JTAG environment override" \
            "*A53*#0" $path $address
    } message options]
    file delete -force $path
    if { $status } {
        return -options $options $message
    }
}

if { [llength $argv] < 2 || [llength $argv] > 6 } {
    error "Usage: load-jtag-image.tcl <hw-server-url> <tftp-server-ipv4> \[board-ipv4\] \[target-id\] \[target-cable-serial\] \[target-device-index\]"
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
set TARGET_ID ""
set TARGET_CABLE_CTX ""
set TARGET_CABLE_SERIAL ""
set TARGET_DEVICE_INDEX ""

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
if { [llength $argv] > 3 } {
    set TARGET_ID [lindex $argv 3]
    if { ![string is integer -strict $TARGET_ID] || $TARGET_ID <= 0 } {
        error "XSDB target ID must be a positive decimal integer: $TARGET_ID"
    }
}
if { [llength $argv] > 4 } {
    set TARGET_CABLE_SERIAL [lindex $argv 4]
    if { $TARGET_CABLE_SERIAL eq "" || [regexp {[[:cntrl:]]} $TARGET_CABLE_SERIAL] } {
        error "JTAG cable serial must be a non-empty single-line string"
    }
}
if { [llength $argv] > 5 } {
    set TARGET_DEVICE_INDEX [lindex $argv 5]
    if { ![string is integer -strict $TARGET_DEVICE_INDEX] || $TARGET_DEVICE_INDEX < 0 } {
        error "JTAG device index must be a non-negative decimal integer: $TARGET_DEVICE_INDEX"
    }
    if { $TARGET_CABLE_SERIAL eq "" } {
        error "JTAG device index requires a target cable serial"
    }
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
initialize_target_selector $TARGET_ID $TARGET_CABLE_SERIAL $TARGET_DEVICE_INDEX

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

select_target "*PSU*"
mask_write 0xFFCA0038 0x1C0 0x1C0

after 500
download_elf_with_retry "Downloading PMU firmware" "*MicroBlaze PMU*" \
    [file join $JTAG_DIR "pmufw.elf"] cable
select_target "*MicroBlaze PMU*" cable
con
after 500

select_target "*A53*#0"
puts "Resetting A53 processor group before FSBL"
rst -cores -clear-registers
after 500

download_elf_with_retry "Downloading FSBL" "*A53*#0" \
    [file join $JTAG_DIR "fsbl.elf"]
select_target "*A53*#0"
con
after 4000
select_target "*A53*#0"
stop

download_elf_with_retry "Downloading TF-A" "*A53*#0" \
    [file join $JTAG_DIR "tfa.elf"]
select_target "*A53*#0"
con
after 500
select_target "*A53*#0"
stop

download_data_with_retry "Downloading system DTB" "*A53*#0" \
    [file join $TFTP_DIR "system.dtb"] 0x100000
after 500

download_env_override $SERVER_IP $BOARD_IP $MNCOS_JTAG_ENV_ADDR
mwr $MNCOS_JTAG_ENV_MAGIC_ADDR 0x49504f56
mwr $MNCOS_JTAG_MAGIC_ADDR 0x4d4e4350

download_elf_with_retry "Downloading U-Boot" "*A53*#0" \
    [file join $JTAG_DIR "u-boot.elf"]
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
select_target "*A53*#0"
con
