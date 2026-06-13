# Boot an MNCOS image through JTAG and automatically start its TFTP load.
# Run this script from build/export/tftpboot to test mncos-image-minimal, or
# from build/export/jtag-tftpboot to boot the eMMC provisioning image.
#
# Usage:
#   xsdb load-jtag-image.tcl <hw-server-ip> <tftp-server-ip> [board-ip]
#
# If board-ip is omitted, U-Boot obtains it through DHCP before loading files.

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
    error "Usage: xsdb load-jtag-image.tcl <hw-server-ip> <tftp-server-ip> \[board-ip\]"
}

set HW_IP [lindex $argv 0]
set SERVER_IP [lindex $argv 1]

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
