SUMMARY = "MNC Xilinx SYSMON read-only monitoring SDK"
DESCRIPTION = "Typed voltage and temperature snapshots from the Linux Xilinx AMS IIO driver. Product selection and board policy are supplied separately."
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://LICENSE;md5=1ebbd3e34237af26da5dc08a4e440464"
SRC_URI = "file://source"
S = "${WORKDIR}/source"
inherit cmake
EXTRA_OECMAKE = "-DBUILD_TESTING=OFF"
