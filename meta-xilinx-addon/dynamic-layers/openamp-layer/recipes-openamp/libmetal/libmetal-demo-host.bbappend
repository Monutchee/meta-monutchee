# discover_platform.sh uses hexdump's formatted output to read device-tree cells.
RDEPENDS:${PN}:append:class-target = " util-linux-hexdump"
