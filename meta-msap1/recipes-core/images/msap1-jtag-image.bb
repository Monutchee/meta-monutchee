SUMMARY = "Monutchee Station JTAG boot artifact for MSAP1"
DESCRIPTION = "Packages the MSAP1 Xilinx boot firmware and TFTP RAM-boot image for a Provisioning Station."
LICENSE = "MIT"

COMPATIBLE_MACHINE = "^msap1$"

MNC_XILINX_JTAG_IMAGE_RECIPE = "msap1-image"

inherit xilinx-jtag-artifact

MNC_ARTIFACT_NAME = "msap1-jtag-image"
MNC_ARTIFACT_PRODUCT = "msap1"
MNC_XILINX_JTAG_FORCE_JTAG_BOOT = "1"
