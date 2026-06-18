FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

EXTRA_OVERLAYS:append:zub = " enable_rpu0.dtsi"

# do_configure:append() {
#    # Ensure the openamp dtsi is included in the device tree
#    if [ -f ${S}/arch/arm/boot/dts/zub.dts ]; then
#        echo '#include "openamp.dtsi"' >> ${S}/arch/arm/boot/dts/zub.dts
#    else
#        bbwarn "zub.dts not found! Please ensure the path is correct."
#    fi
# }