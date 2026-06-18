# Common build settings for the Xilinx/AMD product line.
#
# Required from this layer's conf/layer.conf, so any build that includes
# meta-fpga-util in its bblayers picks these up automatically. Vendor-neutral
# OS policy stays in meta-mncos (conf/distro/mncos.conf); this file holds only
# the Xilinx-specific knobs that every Xilinx product shares, keeping the
# per-product local.conf.sample down to just the MACHINE-level delta.

# Accept the Xilinx license-flagged recipes.
LICENSE_FLAGS_ACCEPTED:append = " xilinx"

# Xilinx gen-machineconf / embedded-software (SDT) flow.
XILINX_WITH_ESW ?= "sdt"

# Sanity checks for layers in the Xilinx stack that we knowingly accept.
SKIP_META_VIRT_SANITY_CHECK ?= "1"
SKIP_META_SECURITY_SANITY_CHECK ?= "1"
SKIP_META_TPM_SANITY_CHECK ?= "1"
