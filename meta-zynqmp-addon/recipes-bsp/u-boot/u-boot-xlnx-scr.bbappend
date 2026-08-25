# ZynqMP PS clock management quirk: an RPU clocked from RPLL loses its PLL
# after Linux boots. Nothing in the Linux device tree claims the RPU clock,
# so the kernel's unused-clock sweep has PMU firmware suspend RPLL, dropping
# running R5 cores onto the bypassed 33.33 MHz ps_ref_clk (/2 = ~16.7 MHz
# instead of the configured 533.33 MHz). Applies to any ZynqMP design running
# RPU firmware from RPLL; harmless (minor idle power) otherwise.
KERNEL_COMMAND_APPEND:append:zynqmp = " clk_ignore_unused"
