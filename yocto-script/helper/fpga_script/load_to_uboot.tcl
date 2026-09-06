# Develop by Lester Lo
# Replacement of "petalinux-boot jtag --prebuilt 3 --hw_server-url" command
# Boot the linux with your yocto meta-xilinx layer build
# Reference from: https://github.com/Xilinx/meta-xilinx/blob/master/docs/README.booting.zynqmp.md

if { [llength $argv] > 0 } {
    set HW_IP [lindex $argv 0]
} else {
    set HW_IP "127.0.0.1"  ;# Default IP
}

puts "Start the loading procedure"

puts "Connecting to the Xilinx hw_server"
connect -url tcp:$HW_IP:3121


# Select Processor unit group and expose the PMU
puts "Switch to PSU"
targets -set -nocase -filter {name =~ "*PSU*"}

puts "Disabling JTAGsecurity gates"
mask_write 0xFFCA0038 0x1C0 0x1C0

# PMU
after 500
puts "Downloading PMU firmware"
targets -set -nocase -filter {name =~ "*MicroBlaze PMU*"}
catch {stop}
dow ./pmufw.elf
con

# Reset Processor
targets -set -nocase -filter {name =~ "*A53*#0"}
puts "Reset A53 processor"
rst -processor -clear-registers
after 500

# FSBL
puts "Downloading FSBL"
dow fsbl.elf
con
after 500
stop


# arm trusted-firmware (TF-A/BL31)
# The TF-A firmware must be loaded and executed before U-Boot.
puts "Download the ARM Trusted-firmware"
dow tfa.elf
con
after 500
stop

# system devicetree
dow -data system.dtb 0x100000
after 500

# uboot
puts "Downloading U-boot"
dow u-boot.elf
after 500


#Start the uboot
puts "Starting u-boot"
puts "If you want to load the kernel/rootfs via tftp, prepare to interrupt the auto-boot count down!!"
after 500
con


puts "Script Ended."
