# Board identification needs ipmi-fru, not the complete BMC administration suite.
# Keep the original freeipmi package complete for existing consumers.
PACKAGES =+ "${PN}-fru ${PN}-libfreeipmi ${PN}-libipmidetect"
FILES:${PN}-fru = "${sbindir}/ipmi-fru ${sysconfdir}/freeipmi/freeipmi.conf"
FILES:${PN}-libfreeipmi = "${libdir}/libfreeipmi.so.*"
FILES:${PN}-libipmidetect = "${libdir}/libipmidetect.so.* ${sysconfdir}/freeipmi/ipmidetect.conf"
RDEPENDS:${PN} += "${PN}-fru ${PN}-libfreeipmi ${PN}-libipmidetect"
# shlibs supplies the FRU reader's library dependencies automatically.
