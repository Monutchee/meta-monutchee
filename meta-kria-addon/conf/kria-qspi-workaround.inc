# meta-kria v2026.1 keeps k26-smk.inc's unconditional kria-qspi dependency,
# but kria-qspi itself is compatible only with multidomain machines. Drop the
# unbuildable dependency for single-domain Kria images until upstream aligns the
# include and recipe compatibility.
KRIA_QSPI_WORKAROUND_RELEASES ?= "v2026.1"
EXTRA_IMAGEDEPENDS:remove:kria = "${@'kria-qspi' if (d.getVar('XILINX_RELEASE_VERSION') in (d.getVar('KRIA_QSPI_WORKAROUND_RELEASES') or '').split() and 'multidomain' not in (d.getVar('MACHINEOVERRIDES') or '')) else ''}"
